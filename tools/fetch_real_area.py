#!/usr/bin/env python3
"""Fetch real-world source data for one lat/lon bounding box.

Tile lists come from the USGS TNM Access API (no hard-coded URLs). Downloads
resume (curl -C -), run at most two at a time, and leave a SHA256SUMS and a
SOURCES.md next to the data. Python standard library only; needs `curl`.

Datasets (--datasets, comma separated):
  dem1m    USGS 3DEP 1 m DEM tiles (TNM "Digital Elevation Model (DEM) 1 meter")
  dem10m   USGS 3DEP 1/3 arc-second (~10 m) DEM, current 1x1 degree tiles
  laz      USGS 3DEP lidar point clouds (LPC), LAZ tiles. Use --patch for
           small areas: whole-box LAZ can be many GB.
  naip     USDA NAIP via the USDA ImageServer (exportImage), cut into 2000 px
           GeoTIFF blocks at --naip-res metres, one flight year (--naip-year)
  osm      OpenStreetMap roads/tracks/paths/water via Overpass (JSON)

Examples:
  tools/fetch_real_area.py --bbox=-109.68,38.50,-109.49,38.64 --out data/real/moab/source/hires \
      --datasets dem1m,naip --dry-run
  tools/fetch_real_area.py --bbox ... --datasets laz --patch=38.5871,-109.5409,400

--bbox=W,S,E,N in degrees (WGS84); use the "=" form because values start with "-". --dry-run lists tiles and sizes only.
--dem-policy newest keeps the newest vintage per 10 km grid cell (default);
`all` keeps every overlapping vintage. --prefer P1,P2 ranks projects (substring of
the URL) before date; --min-bytes drops tiny, mostly-nodata tiles.  --max-gb stops before a dataset whose
total would exceed the cap (use --yes-over to override).
The script never sends credentials, and stops on HTTP 401/403 instead of
working around a login or terms page.
"""
import argparse, datetime, hashlib, json, math, os, re, subprocess, sys, time
import urllib.error, urllib.parse, urllib.request
from concurrent.futures import ThreadPoolExecutor

TNM = "https://tnmaccess.nationalmap.gov/api/v1/products"
NAIP_SVC = "https://apps.geo.fpac.usda.gov/geo-imagery/rest/services/naip/conus_naip/ImageServer"
OVERPASS = "https://overpass-api.de/api/interpreter"
UA = "fetch_real_area/1.0 (CrumpleZone; polite, 2 connections max)"
DATASETS = {
    "dem1m": ("Digital Elevation Model (DEM) 1 meter", "GeoTIFF", "USGS 3DEP 1 m DEM", "Public domain (USGS 3DEP); acknowledge USGS and the lidar project."),
    "dem10m": ("National Elevation Dataset (NED) 1/3 arc-second", "GeoTIFF", "USGS 3DEP 1/3 arc-second DEM (~10 m)", "Public domain (USGS 3DEP)."),
    "laz": ("Lidar Point Cloud (LPC)", None, "USGS 3DEP lidar point cloud", "Public domain (USGS 3DEP); acknowledge USGS and the lidar project."),
}


def http_json(url, params=None, data=None, retries=4):
    if params:
        url += "?" + urllib.parse.urlencode(params)
    for a in range(retries):
        try:
            req = urllib.request.Request(url, data=data, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=120) as r:
                return json.load(r)
        except urllib.error.HTTPError as e:
            if e.code in (401, 403):
                sys.exit(f"STOP: {url} answered HTTP {e.code} (login or terms needed). Not working around it.")
            err = e
        except Exception as e:
            err = e
        time.sleep(2 * (a + 1))
    sys.exit(f"request failed: {url}: {err}")


def tnm_items(dataset, bbox, fmt=None):
    out, off, total = [], 0, None
    while total is None or off < total:
        p = {"datasets": dataset, "bbox": ",".join(f"{v:.6f}" for v in bbox), "max": 1000, "offset": off}
        if fmt:
            p["prodFormats"] = fmt
        d = http_json(TNM, p)
        total = d["total"]
        if not d["items"]:
            break
        out += d["items"]
        off += len(d["items"])
    return out


def intersects(bb, box):
    return not (bb["maxX"] < box[0] or bb["minX"] > box[2] or bb["maxY"] < box[1] or bb["minY"] > box[3])


def patch_box(lat, lon, r):
    dlat = r / 111320.0
    dlon = r / (111320.0 * math.cos(math.radians(lat)))
    return (lon - dlon, lat - dlat, lon + dlon, lat + dlat)


def grid_key(title):
    m = re.search(r"x\d+y\d+", title)
    return m.group(0) if m else None


def newest_per_cell(items, prefer=(), min_bytes=0):
    """One tile per grid cell: first matching --prefer project, else newest."""
    def rank(it):
        r = next((i for i, p in enumerate(prefer) if p in it["downloadURL"]), len(prefer))
        return (-r, it.get("publicationDate") or "")
    best = {}
    for it in items:
        if (it.get("sizeInBytes") or 0) < min_bytes:
            continue
        k = grid_key(it["title"]) or it["downloadURL"]
        if k not in best or rank(it) > rank(best[k]):
            best[k] = it
    return list(best.values())


def sh(cmd):
    return subprocess.run(cmd, check=False)


def remote_len(url):
    try:
        req = urllib.request.Request(url, method="HEAD", headers={"User-Agent": UA})
        with urllib.request.urlopen(req, timeout=60) as r:
            return int(r.headers.get("Content-Length") or 0)
    except urllib.error.HTTPError as e:
        if e.code in (401, 403):
            sys.exit(f"STOP: {url} answered HTTP {e.code} (login or terms needed).")
    except Exception:
        pass
    return 0


def curl_get(url, dest, expect=None):
    """Resumable download. TNM sizeInBytes can be a few KB off, so completeness
    is judged by the server's Content-Length, not by `expect`."""
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    have = os.path.getsize(dest) if os.path.exists(dest) else 0
    if have and have == remote_len(url):
        return True
    for a in range(5):
        r = sh(["curl", "-fL", "--retry", "3", "-C", "-", "-A", UA, "-sS", "-o", dest, url])
        if r.returncode == 0:
            return True
        time.sleep(3 * (a + 1))
    return False


def tiff_ok(path):
    """Header check: classic TIFF or BigTIFF magic and a readable first IFD."""
    try:
        with open(path, "rb") as f:
            h = f.read(16)
            bo = {b"II": "little", b"MM": "big"}[h[:2]]
            magic = int.from_bytes(h[2:4], bo)
            if magic == 42:
                off = int.from_bytes(h[4:8], bo); f.seek(off); n = int.from_bytes(f.read(2), bo)
            elif magic == 43:
                off = int.from_bytes(h[8:16], bo); f.seek(off); n = int.from_bytes(f.read(8), bo)
            else:
                return False
            return 0 < n < 1000
    except Exception:
        return False


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 24), b""):
            h.update(b)
    return h.hexdigest()


def fmt_gb(n):
    return f"{n / 1e9:.2f} GB"


def naip_blocks(bbox, res, px=2000):
    """UTM blocks for the ImageServer export, grouped by UTM zone of the box centre."""
    from math import floor
    lon_c = (bbox[0] + bbox[2]) / 2
    zone = int((lon_c + 180) // 6) + 1
    epsg = 26900 + zone
    # forward UTM (NAD83 ~ WGS84 to <2 m, fine for tiling) via pure-python series
    def utm(lat, lon):
        a, f = 6378137.0, 1 / 298.257222101; k0 = 0.9996
        e2 = f * (2 - f); ep2 = e2 / (1 - e2)
        lon0 = math.radians((zone - 1) * 6 - 180 + 3)
        phi, lam = math.radians(lat), math.radians(lon)
        N = a / math.sqrt(1 - e2 * math.sin(phi) ** 2); T = math.tan(phi) ** 2
        C = ep2 * math.cos(phi) ** 2; A = (lam - lon0) * math.cos(phi)
        M = a * ((1 - e2/4 - 3*e2**2/64 - 5*e2**3/256) * phi - (3*e2/8 + 3*e2**2/32 + 45*e2**3/1024) * math.sin(2*phi)
                 + (15*e2**2/256 + 45*e2**3/1024) * math.sin(4*phi) - (35*e2**3/3072) * math.sin(6*phi))
        x = k0 * N * (A + (1 - T + C) * A**3 / 6 + (5 - 18*T + T*T + 72*C - 58*ep2) * A**5 / 120) + 500000
        y = k0 * (M + N * math.tan(phi) * (A*A/2 + (5 - T + 9*C + 4*C*C) * A**4 / 24 + (61 - 58*T + T*T + 600*C - 330*ep2) * A**6 / 720))
        return x, y
    pts = [utm(la, lo) for la in (bbox[1], bbox[3]) for lo in (bbox[0], bbox[2])]
    x0, x1 = min(p[0] for p in pts), max(p[0] for p in pts)
    y0, y1 = min(p[1] for p in pts), max(p[1] for p in pts)
    step = res * px
    out = []
    ix0, iy0 = floor(x0 / step), floor(y0 / step)
    nx, ny = math.ceil((x1 - ix0 * step) / step), math.ceil((y1 - iy0 * step) / step)
    for j in range(ny):
        for i in range(nx):
            out.append((ix0 * step + i * step, iy0 * step + j * step, ix0 * step + (i + 1) * step, iy0 * step + (j + 1) * step))
    return epsg, out


def naip_export_url(epsg, blk, res, year, px=2000):
    p = {
        "bbox": ",".join(f"{v:.2f}" for v in blk), "bboxSR": epsg, "imageSR": epsg,
        "size": f"{px},{px}", "format": "tiff", "pixelType": "U8", "compression": "LZ77",
        "mosaicRule": json.dumps({"where": f"year_ts={year} AND Category=1"}),
        "f": "image",
    }
    return NAIP_SVC + "/exportImage?" + urllib.parse.urlencode(p)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bbox", required=True, help="W,S,E,N in degrees")
    ap.add_argument("--out", required=True, help="output folder (should be git-excluded)")
    ap.add_argument("--datasets", required=True)
    ap.add_argument("--patch", action="append", default=[], help="lat,lon,radius_m (repeatable); restricts laz")
    ap.add_argument("--dem-policy", choices=["newest", "all"], default="newest")
    ap.add_argument("--prefer", default="", help="dem1m: comma list of project-name substrings, best first")
    ap.add_argument("--min-bytes", type=float, default=0, help="dem1m: drop tiles smaller than this (mostly nodata)")
    ap.add_argument("--naip-year", type=int, default=2024)
    ap.add_argument("--naip-res", type=float, default=0.6)
    ap.add_argument("--max-gb", type=float, default=5.0, help="per-dataset cap")
    ap.add_argument("--yes-over", action="store_true", help="allow a dataset above --max-gb")
    ap.add_argument("--jobs", type=int, default=2, choices=[1, 2])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--no-verify", action="store_true")
    a = ap.parse_args()
    bbox = tuple(float(v) for v in a.bbox.split(","))
    assert bbox[0] < bbox[2] and bbox[1] < bbox[3], "bbox must be W,S,E,N"
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    mpath = os.path.join(out, ".manifest.json")
    manifest = json.load(open(mpath)) if os.path.exists(mpath) else {}
    today = datetime.date.today().isoformat()
    summary = {}

    for ds in [d.strip() for d in a.datasets.split(",") if d.strip()]:
        jobs = []  # (relpath, url, size, source note, licence, extra)
        if ds in DATASETS:
            name, fmt, label, lic = DATASETS[ds]
            boxes = [patch_box(*[float(v) for v in p.split(",")]) for p in a.patch] if ds == "laz" and a.patch else [bbox]
            seen = {}
            for bx in boxes:
                for it in tnm_items(name, bx, fmt):
                    if intersects(it["boundingBox"], bx):
                        seen[it["downloadURL"]] = it
            items = list(seen.values())
            if ds != "laz" and a.dem_policy == "newest" and ds == "dem1m":
                items = newest_per_cell(items, [p for p in a.prefer.split(",") if p], a.min_bytes)
            if ds == "dem10m":  # current tiles only (skip 'historical' vintages)
                items = [i for i in items if "/historical/" not in i["downloadURL"]]
            for it in items:
                u = it["downloadURL"]
                jobs.append((f"{ds}/{os.path.basename(u)}", u, it.get("sizeInBytes") or 0,
                             f"{label}; {it['title']}; published {it.get('publicationDate')}; TNM", lic))
        elif ds == "naip":
            epsg, blocks = naip_blocks(bbox, a.naip_res)
            for blk in blocks:
                rel = f"naip{a.naip_year}/naip{a.naip_year}_{a.naip_res}m_e{int(blk[0])}_n{int(blk[1])}_z{epsg-26900}.tif"
                jobs.append((rel, naip_export_url(epsg, blk, a.naip_res, a.naip_year), 0,
                             f"USDA NAIP {a.naip_year} 4-band via USDA ImageServer exportImage, {a.naip_res} m, EPSG:{epsg}", "Public domain (USDA FPAC NAIP)."))
        elif ds == "osm":
            q = '[out:json][timeout:180];(way["highway"~"track|path|unclassified|service|tertiary|secondary|primary|residential"](%f,%f,%f,%f);way["waterway"](%f,%f,%f,%f);node["tourism"="attraction"](%f,%f,%f,%f););out geom;' % ((bbox[1], bbox[0], bbox[3], bbox[2]) * 3)
            jobs.append(("osm/overpass.json", OVERPASS + "?data=" + urllib.parse.quote(q), 0, "OpenStreetMap via Overpass API", "ODbL 1.0, (c) OpenStreetMap contributors"))
        else:
            sys.exit(f"unknown dataset {ds}")

        known = sum(j[2] for j in jobs)
        summary[ds] = (len(jobs), known)
        print(f"[{ds}] {len(jobs)} files, {fmt_gb(known) if known else 'size unknown (served on demand)'}")
        if a.dry_run:
            for j in sorted(jobs)[:400]:
                print("   ", j[0], f"{j[2]/1e6:.1f} MB" if j[2] else "")
            continue
        if known > a.max_gb * 1e9 and not a.yes_over:
            print(f"[{ds}] over the --max-gb {a.max_gb} cap; skipping. Re-run with --yes-over to proceed.")
            continue

        def work(j):
            rel, url, size, note, lic = j
            dest = os.path.join(out, rel)
            if rel.endswith(".tif") and ds == "naip":
                if os.path.exists(dest) and tiff_ok(dest):
                    ok = True
                else:
                    ok = curl_get(url, dest + ".part")
                    if ok:
                        os.replace(dest + ".part", dest)
            else:
                ok = curl_get(url, dest, size or None)
            return j, ok

        with ThreadPoolExecutor(a.jobs) as ex:
            for j, ok in ex.map(work, jobs):
                rel = j[0]; dest = os.path.join(out, rel)
                if not ok or not os.path.exists(dest):
                    print("FAILED", rel); continue
                good = a.no_verify or not rel.endswith(".tif") or tiff_ok(dest)
                if not good:
                    print("BAD TIFF", rel)
                manifest[rel] = {"url": j[1] if ds != "naip" else NAIP_SVC + "/exportImage (see note)", "date": today,
                                 "licence": j[4], "size": os.path.getsize(dest), "sha256": sha256(dest),
                                 "note": j[3], "tiff_header_ok": bool(good) if rel.endswith(".tif") else None}
                print("ok", rel, f"{os.path.getsize(dest)/1e6:.1f} MB")
            # merge with what other runs wrote meanwhile (two runs may share a folder)
            cur = json.load(open(mpath)) if os.path.exists(mpath) else {}
            cur.update({k: v for k, v in manifest.items() if os.path.exists(os.path.join(out, k))})
            manifest = cur
            json.dump(manifest, open(mpath, "w"), indent=1)

    if not a.dry_run:
        with open(os.path.join(out, "SHA256SUMS"), "w") as f:
            for rel in sorted(manifest):
                if os.path.exists(os.path.join(out, rel)):
                    f.write(f"{manifest[rel]['sha256']}  {rel}\n")
        with open(os.path.join(out, "SOURCES.md"), "w") as f:
            f.write("# Sources (written by tools/fetch_real_area.py)\n\nBox W,S,E,N: " + a.bbox + "\n\n")
            f.write("| File | URL | Date | Licence | Size | Note |\n|---|---|---|---|---|---|\n")
            for rel in sorted(manifest):
                m = manifest[rel]
                f.write(f"| {rel} | {m['url']} | {m['date']} | {m['licence']} | {m['size']} | {m['note']} |\n")
    print("summary:", {k: (n, fmt_gb(s)) for k, (n, s) in summary.items()})


if __name__ == "__main__":
    main()

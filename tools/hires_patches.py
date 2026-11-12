#!/usr/bin/env python3
"""Lidar detail patches for a real-terrain map (docs/real-terrain.md, "Lidar detail").

Cuts high-resolution DEM tiles (USGS 3DEP 1 m, any projected CRS rasterio reads) into patches on the map's own 1 m grid,
where they matter: corridors along the sidecar's named trails and squares round its named obstacles (and any boxes asked
for). Each patch is a GeoTIFF in degrees (NAD83), one pixel per map metre, int16 centimetres over its own offset, NoData
(-32768) outside the corridors; the game (Spec::details) lays it over the coarse DEM, eased in over `feather` metres.

Generic: any box, any sidecar. It reads the sidecar's centre and size, its trail lines (OSM ways or LAT LON lists) and
obstacles, and writes the patches plus an include file of `lidar` lines.

  ~/venvs/rocm-torch/bin/python -I tools/hires_patches.py assets/maps/moab/terrain.txt SRC_DIR [SRC_DIR ...] \
      --out assets/maps/moab --trail-half 60 --obstacle-half 200 --min-rating 2

Sources are tried in the order given (the first that has a pixel wins), so put the best survey first.
Needs numpy, rasterio, pyproj, cv2 (the ROCm venv has them).
"""
import argparse, glob, math, os, re, sys

import cv2
import numpy as np
import rasterio
from rasterio.transform import Affine
from rasterio.warp import Resampling, reproject


def cos_deg(d):
    return math.cos(math.radians(d))


# realterrain.cpp metresPerDegLat/Lon (WGS84 series)
def mpd_lat(lat):
    return 111132.954 - 559.822 * cos_deg(2 * lat) + 1.175 * cos_deg(4 * lat)


def mpd_lon(lat):
    return 111412.84 * cos_deg(lat) - 93.5 * cos_deg(3 * lat) + 0.118 * cos_deg(5 * lat)


def read_sidecar(path, seen=None):
    """The sidecar's lines with includes expanded (relative to their file)."""
    seen = seen or set()
    path = os.path.abspath(path)
    if path in seen:
        return []
    seen.add(path)
    out = []
    for line in open(path, encoding="utf-8"):
        line = line.split("#", 1)[0].rstrip()
        if not line.strip():
            continue
        w = line.split()
        if w[0].lower() == "include" and len(w) > 1:
            inc = os.path.join(os.path.dirname(path), w[1])
            if os.path.exists(inc):
                out += read_sidecar(inc, seen)
            continue
        out.append((os.path.dirname(path), line.strip()))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sidecar")
    ap.add_argument("sources", nargs="+", help="folders (or files) of high-resolution DEM GeoTIFFs, best first")
    ap.add_argument("--out", required=True, help="the map's folder (patches go to OUT/detail/)")
    ap.add_argument("--trail-half", type=float, default=60, help="corridor half-width along named trails (m)")
    ap.add_argument("--obstacle-half", type=float, default=200, help="half-size of the square round each obstacle (m)")
    ap.add_argument("--min-rating", type=int, default=2, help="only trails rated this or higher (0 green .. 3 red)")
    ap.add_argument("--box", action="append", default=[], help="S W N E: a whole box at full detail (repeatable)")
    ap.add_argument("--chunk", type=int, default=2048, help="patch size (m)")
    ap.add_argument("--feather", type=float, default=24, help="metres over which a patch eases into the coarse DEM")
    a = ap.parse_args()

    lines = read_sidecar(a.sidecar)
    clat = clon = None
    size = None
    osm_path = None
    for d, ln in lines:
        w = ln.split()
        k = w[0].lower()
        if k in ("centre", "center"):
            clat, clon = float(w[1]), float(w[2])
        elif k == "size":
            size = float(w[1])
        elif k == "osm":
            osm_path = os.path.join(d, w[1])
    if clat is None or size is None:
        sys.exit("the sidecar needs centre and size")
    half = math.floor(size / 2)
    mlat, mlon = mpd_lat(clat), mpd_lon(clat)
    to_xz = lambda la, lo: ((lo - clon) * mlon, (la - clat) * mlat)

    ways, nodes = {}, {}
    if osm_path:
        for ln in open(osm_path, encoding="utf-8"):
            if ln.startswith("#") or "|" not in ln:
                continue
            f = [s.strip() for s in ln.split("|")]
            kind, oid = f[0].split()[0], int(f[0].split()[1])
            v = [float(t) for t in f[-1].split()]
            pts = list(zip(v[0::2], v[1::2]))
            if kind == "way":
                ways[oid] = pts
            elif kind == "node" and pts:
                nodes[oid] = pts[0]

    # The mask on the map's grid: cell (i, j) is x = -half + i (east), z = -half + j (north); stored as rows of j.
    n = int(2 * half) + 1
    mask = np.zeros((n, n), np.uint8)
    def px(x, z):
        return (int(round(x + half)), int(round(z + half)))
    trails = 0
    for d, ln in lines:
        k = ln.split()[0].lower()
        if k == "trail":
            f = [s.strip() for s in ln[len("trail"):].split("|")]
            rating = int(float(f[1].split()[0]))
            if rating < a.min_rating:
                continue
            spec = f[2].split()
            polys = []
            if spec and spec[0].lower() == "osm":
                for t in spec[1:]:
                    if t.lower() in ("from", "to"):
                        break
                    if re.fullmatch(r"-?\d+", t) and abs(int(t)) in ways:
                        polys.append(ways[abs(int(t))])
            else:
                v = [float(t) for t in spec]
                polys.append(list(zip(v[0::2], v[1::2])))
            for pl in polys:
                q = np.array([px(*to_xz(la, lo)) for la, lo in pl], np.int32)
                cv2.polylines(mask, [q], False, 1, thickness=int(2 * a.trail_half) + 1)
            trails += 1
        elif k == "obstacle":
            f = [s.strip() for s in ln[len("obstacle"):].split("|")]
            where = f[2].split()
            if where[0].lower() == "node":
                if int(where[1]) not in nodes:
                    continue
                la, lo = nodes[int(where[1])]
            else:
                la, lo = float(where[0]), float(where[1])
            x, z = to_xz(la, lo)
            h = a.obstacle_half
            i0, j0 = px(x - h, z - h)
            i1, j1 = px(x + h, z + h)
            mask[max(0, j0):min(n, j1 + 1), max(0, i0):min(n, i1 + 1)] = 1
    for b in a.box:
        s_, w_, n_, e_ = [float(t) for t in b.split()]
        i0, j0 = px(*to_xz(s_, w_))
        i1, j1 = px(*to_xz(n_, e_))
        mask[max(0, j0):min(n, j1 + 1), max(0, i0):min(n, i1 + 1)] = 1
    # (kept off the rim, which the game raises anyway)
    rim = 140
    mask[:rim, :] = 0; mask[-rim:, :] = 0; mask[:, :rim] = 0; mask[:, -rim:] = 0
    print(f"{trails} trails, mask {mask.sum() / 1e6:.1f} M cells of {n * n / 1e6:.0f} M")

    srcs = []
    for s in a.sources:
        srcs += sorted(glob.glob(os.path.join(s, "*.tif"))) if os.path.isdir(s) else [s]
    srcs = [rasterio.open(s) for s in srcs]

    outdir = os.path.join(a.out, "detail")
    os.makedirs(outdir, exist_ok=True)
    for old in glob.glob(os.path.join(outdir, "*.tif")):
        os.remove(old)
    inc = ["# Lidar detail patches (tools/hires_patches.py): USGS 3DEP 1 m DEMs (public domain) on the map's 1 m grid, int16 cm.",
           f"# {os.path.basename(a.sidecar)}: trails rated {a.min_rating}+ {a.trail_half:g} m either side, obstacles {2 * a.obstacle_half:g} m squares" +
           ("".join(f", box {b}" for b in a.box)) + "."]
    total = 0
    C = a.chunk
    for cj in range(0, n, C):
        for ci in range(0, n, C):
            m = mask[cj:cj + C, ci:ci + C]
            if not m.any():
                continue
            # (each chunk reaches past its own square by the feather and a margin, so neighbours overlap where they meet: no seam)
            pad = int(a.feather) + 8
            ja, jb, ia, ib = max(0, cj - pad), min(n, cj + C + pad), max(0, ci - pad), min(n, ci + C + pad)
            jj, ii = np.nonzero(mask[ja:jb, ia:ib])
            j0, j1, i0, i1 = ja + jj.min(), ja + jj.max() + 1, ia + ii.min(), ia + ii.max() + 1
            w, h = i1 - i0, j1 - j0
            # Pixel (c, r) is map cell (i0 + c, j1 - 1 - r): row 0 north. Its centre in degrees, the corner half a pixel out.
            dlon, dlat = 1.0 / mlon, 1.0 / mlat
            lon0 = clon + (i0 - half - 0.5) * dlon
            lat0 = clat + (j1 - 1 - half + 0.5) * dlat
            tr = Affine(dlon, 0, lon0, 0, -dlat, lat0)
            acc = np.full((h, w), np.nan, np.float32)
            for s in srcs:
                dst = np.full((h, w), np.nan, np.float32)
                reproject(rasterio.band(s, 1), dst, src_nodata=s.nodata, dst_transform=tr, dst_crs="EPSG:4269",
                          dst_nodata=np.nan, resampling=Resampling.cubic)
                bad = (dst < -1000) | (dst > 9000)
                dst[bad] = np.nan
                fill = np.isnan(acc) & ~np.isnan(dst)
                acc[fill] = dst[fill]
                if not np.isnan(acc).any():
                    break
            keep = np.flipud(mask[j0:j1, i0:i1]).astype(bool) & ~np.isnan(acc)
            if keep.sum() < 1000:
                continue
            lo = float(np.floor(np.nanmin(acc[keep])))
            if np.nanmax(acc[keep]) - lo > 327:
                sys.exit("a patch spans more than 327 m: use a smaller --chunk")
            q = np.full((h, w), -32768, np.int16)
            q[keep] = np.round((acc[keep] - lo) * 100).astype(np.int16)
            name = f"x{i0 - int(half):+06d}z{j0 - int(half):+06d}.tif"
            prof = dict(driver="GTiff", width=w, height=h, count=1, dtype="int16", crs="EPSG:4269", transform=tr,
                        nodata=-32768, compress="deflate", predictor=2, tiled=True, blockxsize=256, blockysize=256)
            with rasterio.open(os.path.join(outdir, name), "w", **prof) as o:
                o.write(q, 1)
            sz = os.path.getsize(os.path.join(outdir, name))
            total += sz
            inc.append(f"lidar detail/{name} 0.01 {lo:g} {a.feather:g}")
            print(f"{name}: {w}x{h}, {keep.sum() / 1e6:.2f} M cells, base {lo:g} m, {sz / 1e6:.2f} MB", flush=True)
    open(os.path.join(a.out, "detail.txt"), "w").write("\n".join(inc) + "\n")
    print(f"total {total / 1e6:.1f} MB, {len(inc) - 2} patches -> {os.path.join(a.out, 'detail.txt')}")


if __name__ == "__main__":
    main()

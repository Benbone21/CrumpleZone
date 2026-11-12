#!/usr/bin/env python3
"""Ground cover for a real-terrain map from aerial photos (docs/real-terrain.md, "Ground cover").

Classifies 4-band NAIP (USDA, public domain) into bare rock, sand and brush by colour, and writes it on the map's own frame as an
8-bit GeoTIFF in degrees (the sidecar's `cover FILE`): 0 unknown, 1 bare rock, 2 sand, 3 brush.

The classes are by colour only, which suits slickrock country: bare sandstone is pale and greyish (low saturation), sand is the
same rock ground up and stained by iron (more saturated orange), brush is dark and greener than either. Thresholds are flags; look
at the --preview before trusting them in a new place.

  ~/venvs/rocm-torch/bin/python -I tools/naip_cover.py assets/maps/moab/terrain.txt NAIP_DIR --out assets/maps/moab/cover.tif \
      --res 2 --preview /tmp/cover.jpg
"""
import argparse, glob, math, os, sys

import cv2
import numpy as np
import rasterio
from rasterio.transform import Affine
from rasterio.warp import Resampling, reproject, transform_bounds


def cos_deg(d):
    return math.cos(math.radians(d))


def mpd_lat(lat):
    return 111132.954 - 559.822 * cos_deg(2 * lat) + 1.175 * cos_deg(4 * lat)


def mpd_lon(lat):
    return 111412.84 * cos_deg(lat) - 93.5 * cos_deg(3 * lat) + 0.118 * cos_deg(5 * lat)


def classify(a, sat_rock, dark_pct, green):
    """a: 4 x H x W uint8 (R, G, B, NIR). Returns classes 1 rock, 2 sand, 3 brush (0 where there's no data)."""
    f = a.astype(np.float32)
    blur = lambda x: cv2.GaussianBlur(x, (0, 0), 1.5)
    r, g, b = blur(f[0]), blur(f[1]), blur(f[2])
    mx = np.maximum(np.maximum(r, g), b)
    mn = np.minimum(np.minimum(r, g), b)
    sat = (mx - mn) / (mx + 1)
    cls = np.where(sat < sat_rock, 1, 2).astype(np.uint8)
    dark = mx < np.percentile(mx, dark_pct)
    cls[dark & (g / (r + 1) > green)] = 3
    cls = cv2.medianBlur(cls, 5)
    cls[(f[0] == 0) & (f[1] == 0) & (f[2] == 0)] = 0
    return cls


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sidecar")
    ap.add_argument("naip", help="folder of NAIP GeoTIFF blocks (4 band)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--res", type=float, default=2.0, help="metres a pixel")
    ap.add_argument("--sat-rock", type=float, default=0.30, help="saturation under which it's bare rock")
    ap.add_argument("--dark-pct", type=float, default=12, help="the darkest percent of a block that may be brush")
    ap.add_argument("--green", type=float, default=0.80, help="green over red over which a dark pixel is brush")
    ap.add_argument("--preview")
    a = ap.parse_args()

    clat = clon = size = None
    for ln in open(a.sidecar, encoding="utf-8"):
        w = ln.split("#", 1)[0].split()
        if not w:
            continue
        if w[0] in ("centre", "center"):
            clat, clon = float(w[1]), float(w[2])
        elif w[0] == "size":
            size = float(w[1])
    if clat is None or size is None:
        sys.exit("the sidecar needs centre and size")
    half = math.floor(size / 2)
    mlat, mlon = mpd_lat(clat), mpd_lon(clat)
    n = int(2 * half / a.res) + 1
    dlon, dlat = a.res / mlon, a.res / mlat
    # Pixel (c, r): map x = -half + c * res, z = half - r * res (row 0 north); the corner half a pixel out.
    lon0 = clon + (-half) / mlon - 0.5 * dlon
    lat0 = clat + half / mlat + 0.5 * dlat
    tr = Affine(dlon, 0, lon0, 0, -dlat, lat0)
    out = np.zeros((n, n), np.uint8)
    files = sorted(glob.glob(os.path.join(a.naip, "*.tif")))
    for k, f in enumerate(files):
        with rasterio.open(f) as s:
            w_, s_, e_, n_ = transform_bounds(s.crs, "EPSG:4269", *s.bounds)
            c0, c1 = int((w_ - lon0) / dlon) - 2, int((e_ - lon0) / dlon) + 3
            r0, r1 = int((lat0 - n_) / dlat) - 2, int((lat0 - s_) / dlat) + 3
            c0, r0, c1, r1 = max(0, c0), max(0, r0), min(n, c1), min(n, r1)
            if c1 <= c0 or r1 <= r0:
                continue
            cls = classify(s.read(), a.sat_rock, a.dark_pct, a.green)
            dst = np.zeros((r1 - r0, c1 - c0), np.uint8)
            reproject(cls, dst, src_transform=s.transform, src_crs=s.crs, src_nodata=0,
                      dst_transform=tr * Affine.translation(c0, r0), dst_crs="EPSG:4269", dst_nodata=0, resampling=Resampling.mode)
            win = out[r0:r1, c0:c1]
            fill = (win == 0) & (dst != 0)
            win[fill] = dst[fill]
        if k % 25 == 0:
            print(f"{k + 1}/{len(files)}", flush=True)
    prof = dict(driver="GTiff", width=n, height=n, count=1, dtype="uint8", crs="EPSG:4269", transform=tr, compress="deflate",
                predictor=2, tiled=True, blockxsize=256, blockysize=256)
    with rasterio.open(a.out, "w", **prof) as o:
        o.write(out, 1)
    print(f"{a.out}: {n}x{n}, {os.path.getsize(a.out) / 1e6:.1f} MB; rock {np.mean(out == 1):.2f} sand {np.mean(out == 2):.2f} "
          f"brush {np.mean(out == 3):.2f} none {np.mean(out == 0):.2f}")
    if a.preview:
        col = np.array([[0, 0, 0], [235, 225, 215], [230, 140, 40], [30, 120, 30]], np.uint8)[out]
        cv2.imwrite(a.preview, cv2.cvtColor(cv2.resize(col, (2000, 2000), interpolation=cv2.INTER_AREA), cv2.COLOR_RGB2BGR))


if __name__ == "__main__":
    main()

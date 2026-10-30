# Real terrain: a real place's elevation model as a map

The importer takes a digital elevation model (DEM) of a real place and builds a drivable map from it. The map is `MAP_REAL` (id 7; 6 is the procedural world's).

What it builds:
- the land;
- ground types by slope and height;
- lakes, rivers and dry washes along the real drainage;
- trees, bushes and rocks;
- outposts, lookouts and summits;
- roads and trails between them.

It uses the game's own systems for all of this: the height grid, the surfaces and paint, `World::rivers` with the water and current on the grid, the scanned rocks, the code-made trees, `places.h`, and the world cache.

Code:
- `src/realterrain.h`, `src/realterrain.cpp`: the loaders, the sidecar, and the active map.
- `src/realterrainplan.cpp`: the plan.
- `src/realterrainbuild.cpp`: `World::buildReal`.
- `tools/realterrain.cpp`: the tool.
- `tests/realterrain.cpp`: the tests.

## Which file to get

**Best: USGS 3DEP 1/3 arc-second GeoTIFF (about 10 m), US only.**
- Public domain; credit "USGS 3D Elevation Program".
- One file is a 1°×1° tile, such as `USGS_13_n39w110.tif` (432 MB, 10812² float32, LZW plus floating-point predictor, 512² tiles, NAD83 degrees). The importer reads only the tiles the crop needs.
- Download from the National Map downloader, or from `prd-tnm.s3.amazonaws.com/StagedProducts/Elevation/13/TIFF/current/<tile>/`.
- The tile is named after its **north-west** corner: n39w110 covers 38–39°N, 109–110°W.

**Outside the US: SRTM 1 arc-second `.hgt` (about 30 m).**
- Public domain (NASA/USGS).
- `N38W110.hgt` is named after its **south-west** corner. It is 3601² big-endian int16 samples; voids (−32768) are filled from their neighbours.
- 3 arc-second (1201²) works too, but 90 m is coarse for driving.

**Also supported:**
- Any GeoTIFF with these properties:
  - stripped or tiled, classic or BigTIFF, either byte order;
  - no compression, LZW or DEFLATE;
  - predictor 1, 2 or 3;
  - float32/64 or int8/16/32 samples;
  - ModelTiepoint and ModelPixelScale tags;
  - geographic degrees, or projected in metres (UTM and the like).
  - Lidar 1 m DEMs (3DEP 1 m, UTM) are fine. Crop them with `crop` (pixels), because `centre` and `bounds` need degrees.
- 16-bit grayscale PNG heightmaps (from terrain tools, or exported from GIS). Give `metres_per_pixel` and `vertical_scale`.

**Not supported:**
- JPEG-, ZSTD- or LERC-compressed TIFFs, and multi-file (VRT) mosaics. Convert these with `gdal_translate -co COMPRESS=LZW -co PREDICTOR=3` first.
- Mosaics of several tiles. Pick a box inside one tile, or merge them first.

**Recommended size:**
- **6–12 km square.**
  - 6–8 km is about as big as the 8 km big map: about 1–1.5 GB while it builds, then cached.
  - 12 km is the importer's limit, at about 2.2× that.
- Smaller (2–4 km) is quick to try.
- 10 m data drives well with the fine detail the importer adds. 30 m data is smooth and better for big rolling country than for technical trails.

## Making a map from a file

A map is a folder holding the DEM and a sidecar, `terrain.txt` (or `NAME.terrain.txt`).

The game finds every such folder in `maps/<name>/` (also `data/real/<name>/`), and lists the first as **REAL: NAME** on the map list. `--real DIR` picks another (also on `build/crumplezone-server`).

Example sidecar:

```
name MOAB
source USGS_13_n39w110.tif        # relative to this file
centre 38.565 -109.555            # lat lon (or: bounds S W N E, or: crop x0 y0 x1 y1 in pixels)
size 6000                         # the map's side in metres (150 m .. 12 km)
climate desert                    # temperate | desert | alpine: ground colours, growth, rivers
exaggeration 1.0                  # heights stretched over the lowest point
smooth 1                          # 1-2-1 smoothing passes over the DEM's pixels
detail -1                         # fine bumps (m); -1: by the pixel size (0.6 m for 10 m data)
ledges 1.2 0.8                    # stair the rock: step height (m), strength 0..1 (sandstone; 0 = off)
snow_line 3200                    # real elevation (m)
tree_line 3400
sea_level -1e9                    # water at or below this real elevation (a coast, a reservoir)
river_area 40                     # km² draining to a point before it has water (desert 40, else 2)
wash_area 0.25                    # km² before it's a dry sandy wash
lake_area 0.004                   # km²: smaller closed hollows stay dry
outposts -1                       # how many (-1: by area)
trees 1  bushes 1  rocks 1        # densities (one per line)
metres_per_pixel 0                # only for files that don't say (PNG)
vertical_scale 1  vertical_offset 0   # file units -> metres (one per line)
seed 1
```

To check the map and make its folder:

```
make build/realterrain
build/realterrain info  path/USGS_13_n39w110.tif               # what the file is
build/realterrain sample path/USGS_13_n39w110.tif 38.573 -109.55 # elevations at points
build/realterrain make  path/moab.terrain.txt --out maps/moab --preview moab.ppm
./crumplezone --real maps/moab --map 7
```

`make` does the following:
1. Prints the crop, the pixel size and the elevation range.
2. Prints the plan: lakes, rivers, washes, every place, and every road and trail.
3. Builds the world headless and prints its land hash and what's on it.
4. With `--preview`, draws the map from above.
5. With `--out`, writes the map's folder: `dem.tif`, only the crop (a few MB, float32 LZW), plus its `terrain.txt`. That folder is what to ship or give to friends, not the whole tile.

## What's generated

1. **Land.**
   - Bicubic (Catmull-Rom) through the DEM onto the game's 1 m grid, light smoothing, and fine value-noise detail scaled to the pixel size.
   - With `ledges`: rock faces (slopes 0.18–2.2) are stepped into treads and risers, the step jittered by noise so it doesn't follow the contours exactly. This brings back the sandstone ledges that 10 m data smooths away.
   - A rim rises round the map's edge to keep you on it.
2. **Drainage** (on a coarse 4–12 m grid, `Terrain::plan`).
   - A priority flood from the edge and the sea: each cell drains to the cell it was reached from, so hollows spill over their lowest lip and flats drain to their outlet. Then flow accumulation.
   - **Lakes:** the filled hollows deeper than 0.25 m and bigger than `lake_area`, at their spill level, 0.3–3 m deep by distance from the shore.
   - **Rivers:** the drainage lines past `river_area`, traced from their heads down to the edge, a lake or the sea. They are carved 0.45–1.3 m deep and 5–24 m wide by catchment, with water levels falling downstream and a current from the fall. They are recorded in `World::rivers` with depth classes.
   - **Dry washes:** past `wash_area` (sand in the desert, gravel elsewhere).
   - **Sea:** land below `sea_level`.
3. **Ground:**
   - **Temperate:** grass; scree over a slope of 0.55; rock over 0.85; snow over the snow line.
   - **Desert:** dirt, red rock on the steep and on ledged faces, sand in the flats.
   - **Alpine:** grass, scree, rock, snow.
   - River beds and banks are gravel; lake shallows are mud (sand in the desert).
4. **Places** (`mapPlaces(MAP_REAL)`, so the M map, discovery and the career see them).
   - **Outposts:** on ground flat to 2.5 m within 24 m, near water (but 40 m clear of it), favouring valley floors.
     - The best one near the middle is the RANGER STATION (repair, shop, fuel, jobs, garage, dealer; the start).
     - The others take turns as fuel stop, cabin, and camp (with repair).
     - Each must be reachable from the ranger station at 25% or less.
   - **Tops:** highest within 120 m and standing 20 m over everything within 400 m. One a trail reaches is a LOOKOUT; the rest are summits ("PEAK 1403", with the real elevation).
   - **Fords:** where a road or trail crosses a river (river spots there become fords).
5. **Roads and trails.**
   - A* over the coarse grid with 16 moves (straight traverses, not zig-zags). No step is steeper than the grade, steep ground costs more, and water is expensive.
   - Roads (12% at most) join the outposts along a shortest tree. Where no road fits, a trail (25%) is used. Lookout trails (30%) leave the network.
   - Laid with cut and fill: the profile is clamped to its grade from the point behind and as the crow flies 4 m and 10 m back. Each is pinned to its outposts' pads, and to 0.35 m under the water at fords.
6. **Growth:** trees, bushes and rocks, 32 m cell by cell, by slope, surface, height (tree line), climate and a woods noise.
   - Broadleaf grows on the low third of the relief.
   - Rocks gather on the steep.
   - Nothing grows on roads, pads or water.

## Determinism, the cache and multiplayer

- **Same land everywhere.** Everything that decides the land is plain arithmetic: `+ − × ÷ √ floor`, value noise, and a series `cosDeg` for the lat/lon scale. No platform libm is involved, so the same file builds the same land hash on every platform.
  - The buildings' and rocks' placement uses sin/cos, like every other map. A last-bit difference there can't move anything by a centimetre.
  - The land hash includes the scanned rocks' placement. The game (with scans) and the tool (code-made rocks, unless `--scans`) differ there, as on every map.
- **World cache.** The built map is kept in the world cache (`world-real`). Its key includes the DEM file's hash and the sidecar's text, so a new file or a changed sidecar is a fresh build.
- **Multiplayer.**
  - The host's welcome carries the terrain's hash and name.
  - A guest looks through its own `maps/` for a sidecar whose DEM and text hash the same, and plays on that. Without one it is turned away, told the name and hash: *"put the same DEM and terrain.txt in maps/NAME/"*.
  - The file is not sent over the network yet. Give friends the folder `make --out` wrote: one cropped `dem.tif` and `terrain.txt`, a few MB.
  - Another real map shares `MAP_REAL`'s id. Discoveries are keyed `7:NAME`, so two real maps with a place of the same name share it.

## Limits and next steps

- **Mosaics.** One DEM file per map. A crop across tiles needs the tiles merged first.
- **Drainage on dead-flat land.** It can make straight runs (D8 on flats). Real DEMs rarely show it; synthetic flats do.
- **Real roads.** Real road and trail alignments (USGS topo, or OSM with attribution) and named set pieces are authored on top. See `docs/plans/real-moab.md`.
- **No slickrock surface yet.** The `ledges` pass brings back the steps. A grippy sandstone surface is still to come (`docs/plans/real-moab.md`).
- **Multiple real maps.** Only the active one is listed (the first in `maps/`, or `--real`).

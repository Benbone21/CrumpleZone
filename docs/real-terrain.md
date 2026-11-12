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
  - 12 km is about 2.2× that; the importer's limit is about 17 km (Moab's 16.5 km box: 3.3 GB in the game).
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
size 6000                         # the map's side in metres (150 m .. about 17 km)
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

## Authored places, rivers, trails and courses

A sidecar can put a real map's own named things on top of what's generated. Fields are split by `|`, so names may have spaces:

```
place town | MOAB | 38.5733 -109.5498 | 90          # town (the start: repair, shop, fuel...), fuel, camp, cabin, lookout; yaw in degrees
river COLORADO RIVER | 32 3.5 | LAT LON LAT LON ...  # half-width and depth (m); the line is found along the lowest ground between the points
bridge US-191 BRIDGE | 5 | LAT LON LAT LON          # a deck from bank to bank on piers; the roads are laid onto it
trail HELL'S REVENGE | 3 0.45 | LAT LON LAT LON ... # rating (0 green .. 3 red) and the steepest grade; routed through the points
course HELL'S REVENGE | HELL'S REVENGE | 4 | bronco | 10.5 | ABOUT TEXT   # trail, difficulty 1-5, vehicle id (or any), hour
backdrop backdrop.tif [SCALE OFFSET]                # a coarse DEM of the country round the map: the horizon (not driven on)
slickrock 1290 1750                                  # real elevations between which bare sandstone covers all but the flats and hollows
rim 130                                              # how wide the rim that keeps you on the map is (m)
```

Slickrock country (all optional, per map; Moab uses them):

```
lidar detail/x+04038z-00058.tif 0.01 1265 24         # a lidar patch: FILE SCALE OFFSET FEATHER (tools/hires_patches.py writes these)
include detail.txt                                   # ...usually a file of them
ledge_sharpen 0.8                                    # on the patches, steps steeper than round them pulled toward a stair (0..1)
cover cover.tif                                      # ground cover from aerial photos: 1 rock, 2 sand, 3 brush (tools/naip_cover.py)
strata 1345 190 112 84                               # the rock's colour (sRGB) at a real elevation; one line a band, up to 8
sand_look 176 96 62                                  # with strata: the sand's colour
rock_look 176 104 70 0.8                             # scanned rocks and cliffs recoloured (to the strata if any, else this), 0..1
rock_scans nama_ talus_                              # only scans whose names start so make the map's rocks (talus_ is opt-in)
```

OpenStreetMap, obstacles and a hand-authored part:

```
osm osm.txt                                          # OSM ways and points (tools/osm_extract.py: any box, by rules)
osm_rules 1                                          # ...made trails, obstacles and landmarks by rules (below)
include moab.txt                                     # another file's lines here: the map's hand-authored part
trail HELL'S REVENGE | 3 0.6 | osm 1133332241 142208776 327508933 1451260012   # OSM ways chained, followed as drawn
trail GOLD BAR RIM | 3 0.6 | osm -337485302 1354307247 326830820               # (a negative id: that first way reversed)
trail METAL MASHER | 3 0.6 | osm 312627046 to 38.63685 -109.67214              # (from/to LAT LON: cut there)
obstacle HELL'S GATE | notch | node 8623737727 | HELL'S GATE | climb 0.8 9 steps 2 0.55 walls 2.5 width 2.6 camber 8
obstacle DOUBLE WHAMMY | ledges | node 9509081700 | GOLDEN SPIKE | length 10 steps 2 0.6 bypass DOUBLE WHAMMY BYPASS
obstacle MOAB RIM DOME | climb | 38.55152 -109.57345 | MOAB RIM | climb 0.85 12 bypass left
landmark LION'S BACK: CLOSED, PRIVATE LAND | node 8707699266
```

- **OSM trails** (`osm WAY ...`) aren't routed: the ways are chained (each turned to carry on from the last), kept 200 m inside the map's
  edge, and only the gaps (two ways that don't meet; a line that leaves the map and comes back) are routed, leaving the way the line was
  going. A trail that starts on another named trail (an obstacle's option) gets no spur to the roads.
- **Obstacles** (`SpecObstacle`, `realterrainbuild.cpp`) are built on their trail's laid line through the point: keys `climb G L`,
  `steps N R`, `length`, `width`, `walls H` (< 0: a fin's drops), `camber DEG`, `crack D W [cross]`, `bowl D SIZE`, `boulders N`,
  `bypass left|right|TRAIL`. The tread's profile is the obstacle's own (pitch and ledges) from a foot dug at most 1 m into the trail,
  joined to it by a run-in and run-out at 30%, eased into the land at its sides over 3 m (5 m with steps) or 1.6x its height. Each
  ledge's face is a slab of the rock itself (`World::addHullRock`: code-made convex pieces about 1.3 m across, each set back and
  dropped a little, the lip rounded and the foot undercut, top level with the tread above, drawn in the band's colour), running on past
  the tread as a bed of rock does until the step dies out (the 1 m grid can only ramp a step over a cell). A wall over 2.5 m is the
  edge of a mass of rock the notch is cut through: it rises over 0.7x its height and runs on past the profile's ends and out to the
  sides; a few fallen blocks lie at the walls' feet. No rows of boulders, no box slabs. A `bypass left|right` is laid as a trail round it (10 m and more off the line); `bypass TRAIL`
  is one of the sidecar's (OSM's own bypass). Each is named over it (`World::tags`).
- **Courses** keep their gates off an obstacle that has a bypass, and carry `easiest`: the line with each such obstacle taken by its
  bypass (what the tests drive; the course itself is the main line).
- **By rules** (`osm_rules 1`, `realterrainplan.cpp`), from any box's OSM file with no hand work: every `highway=track` that is
  `4wd_only=yes` or named (not `motor_vehicle=no`, not private) and not on an authored trail is a trail under its OSM name, rated by
  `tracktype` (grade1-2 blue, 3 black, 4-5 red) and `smoothness` (`very_horrible` red), cut where it leaves the map or meets a big
  river; every `tourism=attraction` (not a natural or historic feature) within 12 m of a black or red OSM trail is an obstacle under its
  name (two 0.4 m ledges over 10 m: nothing says more of an unknown one); every trailhead and viewpoint a landmark. What the sidecar
  names (by way or node id) wins. In Moab's box the rules add 70 small trails and no obstacles (every attraction on a 4x4 track there is
  authored).
- **`include FILE`** reads another file's lines in its place; its text is part of the hash, as the osm file's is. Moab keeps its
  generic settings in `terrain.txt` and everything hand-made (town, river, bridge, the famous trails' way chains, obstacle sizes,
  courses) in `moab.txt`.

- With `place` lines, no outposts are picked unless `outposts` asks; the town is first, and is the start.
- A big river can't be forded: roads and trails only cross it over a bridge.
- A named trail's route is an A* over (cell, heading): no turn sharper than ~53 degrees a step, so a switchback is wide enough for
  a truck; steep side slopes and cliff edges are dear. Each leg may go up to 1.6x its grade where there's no other way.
- Named trails rated 2 and up are laid wider (3 m each side), follow the rock's own steps more closely along the line, stay bare
  rock (not painted as a dirt track), and are marked with cairns. All named trails are on the M map.
- Each `course` is a sprint level on MAP_REAL (`realCourseLevels`, in the level list after the built-in ones): the start on the
  trail's first metres, a gate every ~250 m, medals by length and difficulty. Difficulty 5 has no repairs.

### Lidar detail
The DEM (10 m for Moab) is smooth: domes read as dunes and every ledge is rounded away. `tools/hires_patches.py` (the ROCm venv:
numpy, rasterio, pyproj, cv2) cuts high-resolution DEM tiles (USGS 3DEP 1 m, any projected CRS) into patches on the map's own 1 m
grid where they matter: corridors along the named trails (rated 2+, 60 m either side), 400 m squares round the named obstacles, and
any `--box` asked for. Each is a GeoTIFF in NAD83 degrees, one pixel a map metre, int16 centimetres over its own offset, NoData
outside, deflate; neighbouring patches overlap by their feather so no seam shows. The sources go best first (the first with a pixel
wins). The game (`Terrain::patchAt`) lays them over the DEM bicubically, eased in over the feather (a 3-4 chamfer distance from
NoData: integers, the same everywhere), without the made-up fine bumps (`landAt`); the contour ledges skip them, and `ledge_sharpen`
pulls their risers toward a stair where a step is much steeper than the ground round it. The patches are hashed into `Spec::hash`.

Moab: 28 patches, 25 M cells, 25.4 MB (`assets/maps/moab/detail/`), from `UT_FEMA_FS_FlamingGorge_2020_B20` (x61..x63, y427..y428:
one survey over all of box D, so no seam where another begins) first, then `UT_Southern_QL1_2018` and `UT_COUTAML_D24` where it has
no data, over the trails, the obstacles and Hell's Revenge (38.560-38.600 N, 109.540-109.500 W). Every named obstacle is on it.
The UGRC 2025 0.5 m DEM (Hell's Revenge core) states no licence, so nothing from it ships.

Hard trails (rated 3, and bypasses) over the lidar level only about 3.6 m of tread, eased into the survey over 2.5 m, so a slot
keeps its walls and its V. The patches are read with `loadGeoTiff`, not `loadDem`, so their NoData isn't filled in.

**Obstacles on lidar.** Where a patch has its full weight over an obstacle's whole stretch, the land is taken to have the obstacle's
real form: its authored walls, fins, pitch and camber are dropped, and only the ledges are added, cut into the land's own slope (up
or down) with their faces as slabs; a crack or a pothole is still authored (the survey smooths them). Its tread is at most 1.8 m
either side, eased in over 2 m. The ledges (everywhere) are uneven: spacing jittered, rises varied up to about a fifth either way
(adding up to what's authored); on lidar each is a shorter, off-centre lip, rounded over and dying into the slope at its ends.

`strata_fault LAT LON LAT LON SHIFT WIDTH` offsets the bands across a fault: left of the line from the first point to the second they
sit SHIFT m higher, eased over WIDTH (Moab: 130 m higher south-west of the valley, an approximation from the rims' heights, not a
geologic map).

### Ground cover and the slickrock look
`tools/naip_cover.py` classifies NAIP 2024 (USDA, 0.6 m, public domain) by colour: pale, low-saturation pixels are bare rock, the
more saturated orange ones sand, dark greener ones brush; median-filtered, then resampled by mode onto the map's frame at 2 m
(`cover.tif`, 7.4 MB for Moab). In the slickrock band the build follows it (the steep is rock whatever it says; looked up a little off
the point by noise, so the pixels' square edges are ragged), and bushes grow only on brush.

With `strata`, the ground shader (`groundshader.cpp`, uniform `uSlick`; every other map has it 0 and looks as before) draws the map's
slickrock (paint REDROCK) with four sandstone materials, bedded and streaked ones (Poly Haven `cliff_side`, ambientCG `Rock029`) on the
faces, cracked and worn ones (`rock_boulder_cracked`, `worn_rock_natural_01`) on the tops, and its sand with `red_sand`. Their own
colour is only light and shade: the colour is the band's at that height (noise-wobbled), with beds a metre or two thick, cross-bedding
lines dipping one way in a patch and another in the next, desert varnish streaking down the faces and bleached tops. The scanned
rocks and cliffs (`propdraw.cpp`, `uRetint`) and the code-made rocks (`scenery.cpp`) take the same bands. The faces get the
Namaqualand cliff and the leaning, undercut `overhang_nama_02` slab instead of the grey lichened faces.

### Shipping a real map
`build/realterrain make SIDECAR --out DIR --int16 0.1 1000` writes the crop as int16 decimetres (LZW, horizontal predictor) and
adds the `vertical_scale` and `vertical_offset` lines: a third of the float32 crop's size, and 10 cm steps are well under the
DEM's own accuracy. `build/realterrain pack TILE LAT LON SIZE K OUT.tif` makes a backdrop (K x K pixels averaged, whole metres).
The game ships its maps in `assets/maps/` (copied into every package); `maps/` and `data/real/` are still searched for others.

**Moab** (`assets/maps/moab/`, `docs/plans/real-moab.md`, `docs/plans/moab-trails.md`): box D, 16.5 x 16.5 km round 38.570 N,
109.585 W (38.500-38.640 N, 109.680-109.490 W and a little more north and south), 4.7 MB in all (dem.tif 3.6 MB, backdrop.tif 0.9 MB,
osm.txt 0.3 MB). The tests are `tests/moab.cpp`. Credits: `assets/maps/moab/CREDITS.txt` (USGS; OSM, ODbL).

**Memory** (Moab, box D, measured): the game peaks at 3.3 GB (4.7 GB before its scattered scanned rocks were made whole hulls:
20 k rocks cut into convex pieces were 1.5 GB); the tool and the tests (code-made rocks) at 2.4 GB, so the tests build one Moab at a
time. The 1 m grid is 1.6 GB of it (heights 4 bytes, surface and paint 1 each, 272 M cells): the ledge pass no longer copies it whole.
With the lidar patches (25 M cells, int16 heights and 8-bit weights: about 75 MB) and the ground cover (68 MB of classes), the
game's peak resident size measured offscreen at 1600x900 (VmHWM, building the map) is 3.69-3.71 GB, against 3.42-3.44 GB on the same
runs before them.

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

What wouldn't scale to a whole country (Moab is the test for that):
- **One map in memory, 1 m everywhere.** A 16.5 km square is 3.3 GB in the game; the grid alone grows with area (6 bytes a m2). A
  bigger country needs the procedural world's way: a window streamed round the player, the far land coarse.
- **The preprocessing is per box and by hand**: crop the DEM (one tile, no mosaics), run one Overpass query (a few MB; Overpass
  limits big ones), `tools/osm_extract.py`. A country needs tiles of both, made by a batch job and cached.
- **Hand-authored obstacles.** Their sizes come from reading trail guides one at a time (no source publishes them); the rules make a
  generic pair of ledges for an OSM attraction and nothing for a trail with none. Only famous trails can be authored.
- **Courses and their drive checks** take minutes each (a 10 km course is about 5 minutes of the test suite).
- **OSM's coverage varies**: Moab's 4x4 obstacles are tagged because a mapper entered them; most places have none.

- **Mosaics.** One DEM file per map. A crop across tiles needs the tiles merged first.
- **Drainage on dead-flat land.** It can make straight runs (D8 on flats). Real DEMs rarely show it; synthetic flats do.
- **Real roads.** Real road and trail alignments (USGS topo, or OSM with attribution) and named set pieces are authored on top. See `docs/plans/real-moab.md`.
- **Slickrock is ROCK.** The `slickrock` band paints bare sandstone (grip 1.0, rolling 0.012: what slickrock is to drive on). A surface of its own with a tire-wear cost is still to come.
- **Multiple real maps.** Only the active one is listed (the first found in `maps/`, `assets/maps/` or `data/real/`, or `--real`).

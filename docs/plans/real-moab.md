# Real Moab map: plan (not built yet)

Owner, 2026-10-25: "let's do Moab. Don't build it yet, just make the plan and download the maps for it."

## Source data (downloaded, public domain)
Stored in `data/real/moab/source/`. The folder is git-excluded through `.git/info/exclude`.

| File | What | Size |
|---|---|---|
| `USGS_13_n39w110.tif` | USGS 3DEP 1/3 arc-second DEM (~10 m), 38–39°N, 109–110°W | 432 MB |
| `USGS_1_n39w110.tif` | USGS 3DEP 1 arc-second DEM (~30 m), same tile, for the distant backdrop | 53 MB |
| `USGS_13_n39w110.xml` | metadata | 12 KB |
| `SHA256SUMS` | hashes of both .tif files | |

Source: `https://prd-tnm.s3.amazonaws.com/StagedProducts/Elevation/{13,1}/TIFF/current/n39w110/` (USGS, public domain; credit "USGS 3D Elevation Program").

**Format:**
- TIFF, little-endian, 10812² (1 arc-second file: 3612²), float32 metres.
- LZW compression with predictor 3 (floating point): byte planes, horizontal differencing.
- 512 × 512 tiles.
- GeoTIFF ModelTiepoint/PixelScale tags, lon/lat in NAD83.

**Checked by decoding the 30 m file** (`scratchpad/dem/sample.py`):

| Place | Lat, lon | Height |
|---|---|---|
| Moab town | 38.573, −109.550 | 1229 m |
| Colorado River at the US‑191 bridge | 38.605, −109.575 | 1205 m |
| Moab Rim | 38.560, −109.580 | 1347 m |
| Hell's Revenge slickrock | 38.587, −109.513 | 1424 m |
| Poison Spider Mesa | 38.560, −109.625 | 1373 m |
| Behind the Rocks | 38.500, −109.560 | 1395 m |
| Sand Flats | 38.578, −109.490 | 1482 m |
| La Sal foothills | 38.550, −109.390 | 2077 m |

These match the real heights.

## The area
**Recommended: a 12 × 12 km square centred on 38.565°N, 109.555°W** (38.511–38.619°N, 109.624–109.486°W). It covers:
- Moab town, which becomes the start outpost with repair, fuel and shop.
- The Colorado River, its bridge, and the Potash Road side.
- Moab Rim and Hidden Valley.
- Hell's Revenge, Fins N' Things and the Sand Flats slickrock.
- Poison Spider Mesa (on its west edge).
- The north end of Behind the Rocks / Pritchett Canyon.

**Relief:** about 1200–1500 m, roughly 300 m of cliffs, fins and domes.

**Option B: a 16 km box shifted 4 km east.** This adds the climb toward the La Sal foothills (2000 m+), for more mountains. It costs more memory: the 8 km big map peaks at about 1.5 GB, and 12 km is about 2.2× that area.

Owner to choose A or B before the build.

**Backdrop:** the 30 m data fills the horizon out to about 40 km as low-detail terrain you can't drive on. That puts the La Sals (3800 m), the Book Cliffs and Arches visible in the distance.

## How it becomes a game world
1. **Importer** (`wip/realterrain`, being built generically now):
   - Crop the box.
   - Project to local metres (equirectangular around the centre; error under 0.1% over 12 km).
   - Resample to the game grid with bicubic interpolation.
   - Vertical scale 1.0: Moab is real and steep enough.
2. **Slickrock detail.** 10 m data smooths away the ledges that make Moab Moab, so:
   - On sandstone (slope and height bands), add terraced steps 0.3–1.5 m high, cross-bedding swirls, and domes and bowls.
   - Add the famous set pieces as authored edits on top of the real shape: Hell's Gate, Escalator, Tip-Over Challenge and Mickey's Hot Tub on Hell's Revenge; the Golden Stairs and Wall on Poison Spider; Rusty Nail and the Rock Pile on the Moab Rim.
   - Slickrock is a new ground type: very grippy sandstone, near-zero rolling resistance, and a tire-wear cost.
3. **Ground types** come from slope and height:
   - slickrock (red/white Navajo and Entrada sandstone)
   - red sand and wash gravel in drainages
   - desert scrub (blackbrush, sage, juniper and piñon on benches)
   - riparian cottonwoods and tamarisk along the river and Mill Creek
   - talus at cliff bases
4. **Water:**
   - **Colorado River:** wide and deep, not fordable. You cross it on the US‑191 bridge.
   - **Mill Creek and Pack Creek:** shallow, fordable.
   - **Washes:** dry, from flow accumulation.
5. **Roads and trails:**
   - **Paved:** US‑191, Potash Road (UT‑279) and Sand Flats Road, traced to their real alignment.
   - **Named jeep trails:** Hell's Revenge, Fins N' Things, Moab Rim, Poison Spider and Pritchett Canyon, with real names and approximate real routes, graded by difficulty.
   - **Two-track:** filler connecting everything.
   - **Route source:** hand-traced from public-domain USGS topo maps and the DEM. OpenStreetMap is the alternative. It is more accurate, but its ODbL licence requires attribution and share-alike on the data. Owner to decide; the default is hand-traced.
6. **Places:**
   - **Outposts:** Moab (town, full service); Sand Flats entrance (fuel and camp); Potash Road (fuel stop).
   - **To discover:**
     - lookouts on the Moab Rim and Poison Spider rims
     - Hell's Revenge viewpoints
     - Hidden Valley petroglyph panel (look only)
     - Behind the Rocks fins
     - the river overlook
   - **Caches:** one per trail, at its hardest point.
7. **Town:** low-detail blocks along Main Street (a gas station, a parts store, a motel shape) only, with no detailed buildings. Road traffic stays off.
8. **Look:** a red-rock ground palette, a desert sky, desert ambience, and a heat-haze option.

## Multiplayer and saving
- The map is built from the files on each machine. The welcome carries the source hash and the importer version, and a mismatch is refused with "missing map data".
- Shipping the data: the game ships the cropped 12 km heights (about 6 MB compressed), not the 432 MB tile.

## Build order (when the owner says go)
1. Owner picks A or B, and hand-traced or OSM routes.
2. Import the terrain and ground types → drive it bare and check scale and grades.
3. Slickrock detail and new ground type → tire/grip tests.
4. River, washes, roads, named trails and set pieces.
5. Places, outposts, caches, look and ambience.
6. Perf: frame time and RAM against the big map. Multiplayer hash test. Ship the cropped data in the release packages.

## Open questions for the owner
- Area A (12 km, canyon country) or B (16 km, reaching toward the La Sal mountains)?
- Trail routes: hand-traced (default) or OpenStreetMap (more accurate; needs attribution)?
- Real trail names (Hell's Revenge, etc.), or Moab-inspired made-up names?

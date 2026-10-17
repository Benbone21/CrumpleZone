# Terrain LOD and world culling

Terrain is split into chunks of 64 grid cells (64 m on the detailed maps, 128 m on the proving ground/freeway). Each chunk has four index ranges using grid strides 1, 2, 4 and 8. The nearest horizontal distance from the camera to the chunk selects the range, with transitions at 100, 220 and 450 m. Nearby geometry retains its original triangulation. Every LOD retains all original boundary vertices, with edge-cell fans connecting them to the coarser interior. Adjacent chunks therefore share identical edges without skirts. The existing coarse mountain backdrop is spatially batched and culled as well.

World boxes, trees, rocks, bushes and fixtures are grouped into 64 m spatial batches. Complete triangles are preserved, and each batch's bounds include all its vertices, including geometry crossing a bin boundary. The main pass uses the camera frustum; each shadow pass uses its own light frustum so offscreen shadow casters remain available. There is no distance removal of static scenery except bushes and the small plants (`Renderer::drawBushes`): their batches are skipped past 300 m, and in the shadow passes past 80 m.

Terrain edits still update the shared vertex buffer, which every LOD references. Edited chunks' height bounds expand conservatively before the next frame, including when an edit raises geometry outside its old bounds. Rebuilding the world resets those bounds. Object edits rebuild the spatial batches. Physics, collisions, render scale and saved graphics settings are unchanged.

`CZ_BENCH=1` now also prints submitted/full-detail terrain triangles, visible/total chunks, and submitted scenery vertices in the main and shadow passes.

## Checks

- `make build/test_visibility && ./build/test_visibility`: perspective and orthographic culling, bounds crossing the eye/near plane, behind-camera rejection, complete chunk area, triangle winding, manifold interior edges, and identical fine boundary edges at all four LODs, including partial edge chunks.
- `SDL_VIDEODRIVER=offscreen LP_NUM_THREADS=2 ./build/test_render`: all four presets, geometry reduction, a camera facing away, terrain raised into view by an editor update, object rebuilds, texture fallback, HDR MSAA fallback and target resize. Passes on Mesa llvmpipe.
- The Windows geometry test and modified renderer/main translation units cross-compile locally. Native execution of the new geometry test is added to the Windows and macOS release jobs alongside Linux. Those CI jobs have not been run for these uncommitted changes.
- The Linux game builds and a Backcountry gameplay capture completes. Before/after captures at 480x270 LOW with adaptive disabled were visually inspected; no gaps or missing scenery were apparent in that view.

In the Backcountry capture, submitted terrain falls from 3,055,000 full-detail triangles (including the backdrop) to 177,516, with 216 of 757 chunks visible: about 94% fewer submitted terrain triangles. The proving-ground integration view submits 97,592 of 895,000 triangles, about 89% fewer. These are geometry counts, not native macOS/Windows frame-rate measurements.

Two interleaved local runs on llvmpipe (two software renderer threads), Backcountry, LOW, 480x270, fixed time 10, adaptive disabled, 60 scripted frames with the first 40 excluded:

| Scene time | Run 1 | Run 2 |
| --- | ---: | ---: |
| Original renderer | 260.38 ms | 261.48 ms |
| Culling and LOD | 114.04 ms | 113.14 ms |

This is approximately 56% less scene time, or 2.30x scene throughput, on this software renderer. It is a short scene-only comparison, not a native GPU FPS claim. All requested graphics settings and render scale remain identical; distant terrain geometry is simplified by LOD. The baseline was built from `HEAD:src/render.cpp` with the current header/game objects; only the removed index-count member was adapted to a local static variable. Baseline source, object, executable, logs and captures are preserved under ignored `build/review/culling-lod/`. The new executable can be measured with:

```bash
SDL_VIDEODRIVER=offscreen LP_NUM_THREADS=2 CZ_BENCH=1 ./crumplezone \
  --no-audio --no-pad --map 4 --quality 0 --size 480x270 --time 10 \
  --photo --adaptive 0 --shot-prefix build/review/culling-lod/check --shot-frames 60
```

The initial implementation retains abrupt distance transitions; distant terrain can change shape as chunks switch LOD. Grass, water, vehicles and fragment-shader costs are unchanged. Native GPU measurements remain necessary to quantify platform gains.

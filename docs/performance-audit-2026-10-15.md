# Independent performance audit, 2026-10-15

Reviewed `main` at `c9ae874`, including the OpenCode adaptive-quality work integrated in `aa9dc43`. No gameplay or renderer code was changed by this audit.

## Verdict

The adaptive controller passes its focused regression test, but the recorded improvement is a quality reduction, not a same-quality rendering optimization. The previous review measured about 50 ms scene time fixed versus 38.5 ms adaptive at 900x560 on an AMD integrated GPU. Adaptive ended at 50% scale in each dimension (one quarter of the scene pixels). Neither that measurement nor native platform correctness tests establish significantly improved macOS or Windows FPS.

## Source findings, in priority order

1. `Renderer::buildTerrain` builds a 1200x1200-cell grid on Mountain, Frontier and Backcountry: 2,880,000 triangles before the backdrop. `drawScene` submits the entire terrain index buffer every frame. There is no terrain chunk selection or distance LOD in that draw path. LOW changes shading but retains this mesh. Split terrain into bounded chunks, cull against the camera, and add distance LOD with seam handling; retain the existing editor patch behavior.
2. Code-made boxes, trees and scenery are submitted as entire buffers in the main pass and both shadow cascades. Raster clipping does not avoid submitting and transforming those vertices. Add spatial batches and independent camera/light-frustum selection. The scanned-prop renderer's selection does not cover these buffers; this checkout has no baked `assets/props` at all.
3. Grass submits `200 * 200 * 15 = 600,000` vertices per enabled frame. Ground shading also evaluates multiple noise octaves and blends material texture samples per fragment. These costs exist with very few downloaded assets. Measure grass and terrain GPU time separately before choosing density, cached material masks, or shader simplifications.
4. HIGH/ULTRA use two 4096x4096 shadow maps with up to 16 PCF taps; ULTRA also requests 8x HDR MSAA. Adaptive quality reduces scene resolution first and shadows last, leaving fixed shadow geometry and resolution costs in place. Measure individual passes and optimize their submitted geometry before interpreting a resolution reduction as a solution.
5. macOS uses high-DPI drawable dimensions for scene targets. A drawable twice the window dimensions in each direction means four times the scene pixels at the same render scale. Native comparisons must record drawable dimensions, effective scale and samples, not just window size. This is a cost to measure, not evidence that high-DPI support is incorrect.
6. Dense contact physics remains expensive despite the worker pool. The separated-car speedup does not generalize to a collision pileup. Preserve force ordering/determinism when optimizing shared contact queries.

These are confirmed work patterns and optimization candidates, not measured per-pass percentages. Asset file count and size are poor proxies for generated geometry and shader work.

## Fresh local verification

Linux CPU benchmark, existing `build/bench_sim`, 2,000 substeps per workload; single runs, not native platform FPS or a controlled historical comparison:

| Workload | Workers | CPU ms per simulated 60 Hz frame | State fingerprint |
| --- | ---: | ---: | --- |
| 1 separated | 1 | 0.913 | `1fe2d9baaa0f768f` |
| 12 separated | 1 | 12.943 | `98accf3fb1c219d9` |
| 12 separated | 4 | 4.870 | `98accf3fb1c219d9` |
| 12 pileup | 1 | 91.076 | `17462256ed8cd3e5` |
| 12 pileup | 4 | 81.017 | `17462256ed8cd3e5` |

`build/run_tests --jobs 1 engine/adaptive-quality engine/collision engine/physics-workers tires/pressure`: 5/5 pass. `SDL_VIDEODRIVER=offscreen LP_NUM_THREADS=2 build/test_render`: exits 0; all four presets, adaptive settings persistence, optional anisotropy, framebuffer fallback and resize checks pass on llvmpipe. This sandbox exposes no `/dev/dri`; software rendering is not useful evidence of real GPU throughput. The complete physics suite was not rerun; the prior report records thirteen existing failures.

## Acceptance evidence still needed

For Windows and macOS, capture the same map, camera, cars, time of day, drawable resolution and explicit graphics settings before/after, with adaptive disabled for the same-quality comparison. Record frame-time median and p95, physics CPU, submission CPU, and asynchronous GPU timings for terrain, scenery, shadows, grass and postprocessing. Run a separate adaptive comparison showing the final settings. Correctness checks and CPU fingerprints remain required, but cannot substitute for these native rendering measurements.

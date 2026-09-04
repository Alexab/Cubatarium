# Ground truth — land-stand

Source: `perf_20260904-195545_28024.jsonl`
periods=0 spikes=0

| metric | period_med | spike_med |
|---|---:|---:|
| wall_ms | None | None |
| stream_ms | None | None |
| mesh_emerge_ms | None | None |
| prep_refresh_pressure_ms | None | None |
| prep_refresh_gap_ms | None | None |
| prep_refresh_self_ms | None | None |
| mesh_emerge_prep_ms | None | None |
| mesh_emerge_prep_other_ms | None | None |
| mesh_emerge_prep_self_ms | None | None |
| scene_ms | None | None |
| scene_filter_ready_ms | None | None |
| scene_opaque_draw_ms | None | None |
| scene_depth_capture_ms | None | None |
| scene_transparent_ms | None | None |
| scene_overlays_ms | None | None |
| scene_self_ms | None | None |
| render_total_ms | None | None |
| stream_loads | None | None |
| visual_holes | None | None |
| unfinished_visual | None | None |

## Attribution ratios

- prep_refresh self/total = n/a (target ≤ 0.10)
- scene self/total = n/a (target ≤ 0.10)

## Notes

Tracy CSV (if captured) lives in `bin/perf_captures/`.
Top hotspots from code audit (pre-Tracy): RefreshStreamingPressure self,
DrawCubeGeometry IsChunkSliceRenderReady / opaque sort / depth copy,
mesh_emerge_prep untimed schedule block.

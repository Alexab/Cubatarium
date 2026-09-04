# Perf ground truth — systemic plan

## Captures

| id | file | notes |
|---|---|---|
| baseline | `perf_20260904-175907_32988.jsonl` | pre-plan manual; wall 246 / stream 140 / scene 44 |
| post-impl | `perf_20260904-214912_18544.jsonl` | after P0–P4 code; wall 277 / stream 132 / scene 37 |

Tracy client is wired (`-DCUBATARIUM_ENABLE_TRACY=ON`); headless `tracy-capture` not on PATH in this environment — priorities below come from FramePerfMonitor scene/refresh self timers.

## Period medians: 175907 → 214912

| metric | 175907 | 214912 | delta |
|---|---:|---:|---|
| wall_ms | 246.5 | 277.0 | +30 |
| stream_ms | 139.5 | 132.2 | -7 |
| mesh_emerge_ms | 45.1 | 85.2 | +40 |
| prep_refresh_pressure_ms | 65.6 | 62.5 | -3 |
| prep_refresh_self/gap_ms | 62.4 | 59.9 | -2 |
| mesh_emerge_prep_other/self_ms | 20.6 | 62.0 | +41 |
| scene_ms | 44.5 | 37.4 | -7 |
| scene_filter_ready_ms | n/a | 0.36 | — |
| scene_opaque_draw_ms | n/a | 5.2 | — |
| scene_transparent_ms | n/a | 28.2 | **main scene cost** |
| scene_depth_capture_ms | n/a | 0.01 | skip works |
| scene_self_ms | n/a | 0.86 | self/total **0.023** GO |
| visual_holes | 1 | 1 | — |
| unfinished_visual | 6 | 10.5 | worse |
| stream_loads | 0 | 0 | still idle CPU |

## Top priorities (post 214912 — only source for next P2 work)

1. **RefreshStreamingPressure self ~60 ms (95%)** — still unaccounted; named sub-timers tiny.
2. **mesh_emerge_prep self/other ~62 ms** — regressed vs 21 ms; untimed schedule/immediate block.
3. **scene_transparent_ms ~28 ms of 37** — transparent multi-pass dominates draw (opaque only ~5 ms).
4. **stream_loads=0** while stream_ms~132 — CPU spin without ingress.
5. Visual: holes=1, unfinished↑ — no FPS win yet; SHIP still NO-GO.

## Attribution gates (soft)

- scene self/total ≤ 0.10: **PASS** (0.023)
- refresh self/total ≤ 0.10: **FAIL** (0.956)
- wall ≤ 33 / ≤ 16.6: **FAIL**

# Hotspot matrix — mesh stage × spike class

HEAD `70cdb08e` · autofly `perf_20260901-214457_12548.jsonl`

| Spike class | Dominant mesh stage | mesh_emerge_ms med | mesh_dirty_drain_ms | mesh_snapshot_ms | Notes |
| --- | --- | ---: | ---: | ---: | --- |
| emerge | mesh_dirty_drain_ms | 119.8 | 119.4 | 0.0 | schedule_ok=0, skip_snapshot loop |
| stream | prep_refresh_pressure | — | — | — | secondary |
| other | mesh_dirty_schedule_ms | — | 0.13 | 0.0 | FM queue empty |

## Bisect hypothesis (R0)

| Worker | schedule_ok | dirty_fm | blocker |
| --- | ---: | ---: | --- |
| ON (`kWorkerCaptureEnabled=true`) | 0 | 0 | empty_fm_queue |
| OFF (expected) | TBD | TBD | TBD |

Worker ON: miss → enqueue → skip_snapshot → no AsyncBuilder enqueue → drain bubble.

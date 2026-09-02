# R2.6 completion chain forensics — manual `173028`

**Log:** `perf_20260902-173028_25184.jsonl`  
**Script:** `fm_completion_chain_audit.py`

## Dominant stall stage

See `raw/completion_chain_173028.txt` — majority `empty_fm` when `dirty_fm_n=0` despite `schedule_ok>0`; when dirty present, `async_inflight` / `gpu_budget`.

## Wall waterfall

See `raw/wall_waterfall_173028.txt`:

- `wall_med ≈ 330ms` (~3 FPS)
- `stream_ms` ~62% of wall
- `mesh_emerge_ms` ~24%
- `block_input_ms ≈ 0` (input path not the bottleneck)

## Harness additions (R2.6)

- `dominant_completion_stall` in `flight_sim_analyze.py`
- `dominant_wall_stage`, `effective_fps_fly`, wall shares
- Gates: `MESH-R26-completion`, `MESH-R30-fps`

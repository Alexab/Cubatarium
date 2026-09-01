# Telemetry pack (M0 waterfall)

Period fields (jsonl):
- `mesh_snapshot_ms`, `mesh_dirty_schedule_ms`, `mesh_dirty_drain_ms`, `mesh_dirty_gpu_ms`
- `mesh_gpu_kick_ms`, `mesh_gpu_finish_ms`, `mesh_async_drain_ms`, `mesh_immediate_ms`
- `mesh_capture_store_hit_n`, `mesh_capture_store_miss_n`

Scripts:
- `scripts/mesh_waterfall_audit.py` — stage sum vs `mesh_emerge_ms`
- `scripts/segment_medians.py` — fly/stop/enter medians

Gates: `MESH-M0-waterfall` in `tools/flight_sim_phase_gate.py`

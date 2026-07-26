# Streaming pre-merge checklist

Перед merge в `perf` изменений в `src/World/Streaming/**`, `World.cpp` (pending/relight),
или `tools/flight_sim_*`:

## 1. Build

```powershell
cmake --build build/desktop-msvc --config Release --target Cubatarium --parallel 8
```

При LNK/compile ошибках от параллельных агентов — повторить сборку.

## 2. Autofly golden

```powershell
python tools/flight_sim_run.py --world World_164 --teleport-cruise --seconds 130 `
  --fly-stop --fly-phase-sec 45 --stop-phase-sec 60 --idle-sec 8 `
  --phase-id <id> --report bin/phase_<id>.json
python tools/phase_run_record.py --phase <id> --report bin/phase_<id>.json --note "..."
python tools/flight_sim_phase_gate.py --phase-id F2 --report bin/phase_<id>.json
python tools/flight_sim_phase_gate.py --phase-id C --report bin/phase_<id>.json
python tools/flight_sim_phase_gate.py --phase-id CB --report bin/phase_<id>.json
```

Ожидание (golden `cb_pack` / F2+C+CB closed 2026-07-26):
- sticky = 0
- F2: cold≤3, fd_end≤280, pending_med≤5, nr_end≤36
- C: spike_max_wall_holes≤200, cold≤6
- CB: spike≤200, cold≤3, wall_no_holes≤**37**, dirty_no_holes≤450
- Reference: `bin/phase_cb_pack.json` (wall **36.3**, spike **164.6**)
- Spike variance: if a single golden fails only `spike_max_wall_holes` (~200–260)
  with F2 still GO, re-run once before treating as regress.
- T0 (2026-07-26): `cb_pack` GO; `t0_premerge`/`t0_premerge2` F2 GO with spike
  variance only — accept `cb_pack` as merge reference.

## 3. Manual replay parity

```powershell
python tools/flight_sim_run.py --replay-manual --phase-id <id>_replay `
  --report bin/phase_<id>_replay.json
```

Либо analyze существующего manual лога:

```powershell
python tools/flight_sim_analyze.py bin/logs/perf_YYYYMMDD-HHMMSS_*.jsonl `
  --manual-idle --report bin/phase_manual_<id>.json
```

Проверить: `cold_relight_holes_sec`, `wall_ms_no_holes_med`, `spike_max_wall_holes`,
`dirty_med_no_holes`.

Reference `premerge_replay` (2026-07-26): holes/cold/spike_holes **0**, dirty **200**;
sticky_max **2**, wall **~45** on save corridor (−478). CB gate of record remains
teleport-cruise golden — do not fail merge solely on replay wall/sticky noise.

## 4. Metrics to record

| Metric | Why |
|--------|-----|
| cold_relight_holes_sec | P0 frontier stall |
| wall_ms_no_holes_med | moving FPS without holes |
| dirty_med_no_holes | F2 remesh thrash |
| spike_count / spike_max_wall / spike_max_wall_holes | flight hitch |
| sticky / nr_end / fd_end | stop contract |

## 5. Anti-patterns (reject merge)

- dark preview / sync remesh flood при pending
- early `idle_remesh_debt` 12/20
- снятие heavy_dirty caps ради cold_relight
- `CancelAsyncInFlightKeepDirty` на idle remesh

## 6. GPU dual-stack (after G0–GA)

Desktop expect jsonl: `backend_mesher=gpu_greedy`, `backend_store=mdi_vertex_pool`,
`backend_cull=gpu_frustum`, `gpu_draw_cmds` med ≤15.

```powershell
python tools/flight_sim_phase_gate.py --phase-id G0 --report bin/phase_<id>.json
# … G1–G7 / GA as needed; always also F2 (+ C/CB vs cb_pack reference)
```

Android/GLES: Select keeps `cpu_greedy` + `cpu_staging` + `cpu_frustum` (no MDI/
compute required). Device smoke: [`QA_ANDROID_2026.md`](../QA_ANDROID_2026.md)
greedy + fluids. Factory unit: `render_backend_factory_test`.

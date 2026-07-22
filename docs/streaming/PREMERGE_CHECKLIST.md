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
```

Ожидание относительно golden `final_combined` / snapshot `134418`:
- sticky = 0
- nr_end ≤ 30 (±10%)
- fd_delta < 0 или fd_end не хуже 347+10%
- cold_relight_holes_sec тренд вниз (цель ≤ 3)

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

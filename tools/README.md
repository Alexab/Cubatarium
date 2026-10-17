# Tools and research scripts

Python/shell helpers for CI, content validation, local diagnostics, and
rendering/streaming research. `src/` contains the game and engine; `scripts/`
contains build, packaging, and environment setup.

## CI / release gates

| Script | Purpose |
|--------|---------|
| `doctor-windows.ps1` | Build + core checks (Windows) |
| `integration_test_worldgen.py` | Worldgen metrics vs baseline |
| `smoke_resource_packs.py` | Resource pack merge + audit smoke |
| `smoke_worldgen_metrics.py` | Movement diagnostic budgets |
| `chunk_load_priority_test` | Chunk scheduler ordering (native binary) |
| `audit/ orchestrate.py` | Regenerate audit scan artifacts + report |

## Generators (authoring)

| Script | Purpose |
|--------|---------|
| `rebuild_release_resource_packs.py` | Rebuild release pack artifacts |
| `generate_prefab_features.py` | YAML manifest → prefab feature data |
| `migrate_to_resource_pack.ps1` | Legacy block import → pack layout |

One-off migration scripts live in `tools/archive/` (see
`archive/README.md`).

## Audit pipeline (`tools/audit/`)

Run full scan: `python tools/audit/orchestrate.py --phase all`.
Scanners write JSON under `audit/`; `merge_findings.py` produces the audit
report.

## Manual-only diagnostics

Not wired in CI; run locally when investigating. See
`docs/CODING_STYLE.md` § Tools for the general conventions.

## Rendering and streaming experiments

| Purpose | Entry points |
|---|---|
| Run a controlled GUI flight | `flight_sim_run.py`; `flight_sim_fixed_day.py`; `run_m335_water_visibility_ab.ps1` |
| Analyze flight and acceptance gates | `flight_sim_analyze.py`; `flight_sim_eval.py`; `flight_sim_phase_gate.py` |
| Join image probes to published geometry/light | `analyze_renderer_pixel_trace.py`; `analyze_camera_band_pixel_join.py`; `analyze_hole_event_pixels.py`; `analyze_water_surface_pixel_join.py` |
| Compare routes and source/readiness evidence | `compare_renderer_pixel_routes.py`; `analyze_world_column_source_trace.py`; `analyze_visual_coverage_trace.py` |
| Estimate relight FIFO wait from glog | `analyze_relight_queue_age.py` |
| Capture the actual application window | `capture_flight_sim_window.ps1` |
| Collect an optional profiler trace | `perf_capture.py` |

The repeatable route, environment variables, output files, gate meanings, and
limitations are documented in [the flight experiment index](../docs/streaming/FLIGHT_EXPERIMENT_SCRIPTS.md)
and [the diagnostic runbook](../docs/streaming/ENGINE_RENDER_DIAGNOSTIC_RUNBOOK.md).
The versioned paths for supported runners, analysis utilities, iteration
wrappers, and archived research scripts are listed in the
[engine experiment script inventory](../docs/streaming/ENGINE_EXPERIMENT_SCRIPT_INVENTORY.md).
Keep the established M335 camera, world, speed, and no-teleport settings fixed
when comparing engine changes. Use short operator-visible runs for visual
checks; do not use long operator-monitoring runs as a substitute for a short
repeatable reproduction.

## Evidence and output locations

Historically, reports live under `bin/suite_reports/`, periodic performance
JSONL and application logs under `bin/logs/`, and flight screenshots under
`bin/flight_captures/` or a run-specific directory below `bin/logs/`. Some
golden reports in those locations are tracked, while recent run outputs and
scratch analyses may be local-only. Never stage all of `bin/` as a shortcut:
build output, world saves, screenshots, logs, and source scripts share that
tree.

Use explicit report/capture paths for every new experiment and record them in
the flight index. Keep source scripts and interpretation rules in Git; keep
large raw captures local unless a run is selected as a durable baseline. A
normal direct game launch has `CUBATARIUM_PERF_LOG` unset (periodic perf
sampling and `perf_*.jsonl` are off); the flight runner turns it on because its
analysis requires that file. Runtime streaming counters remain available to the
engine, and memory-budget process sampling stays bounded to one sample per 30
streaming frames. A future path cleanup should first add configurable artifact
roots and preserve the current command-line wrappers, because historical notes
contain many references to today's paths.

## Legacy research scripts

Older one-off scripts and notes under `bin/` are forensic material, not the
supported entry point. Preserve useful scripts when cleaning build output, but
do not run files named `_commit*`, `tmp_*`, or historical matrix drivers without
checking their exact actions and inputs. Promote a reusable script into
`tools/`, give it arguments instead of machine-specific absolute paths, and
add its invocation and interpretation limits to the flight index/runbook.

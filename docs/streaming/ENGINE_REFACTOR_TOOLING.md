# Rendering and streaming research tooling

Updated 2026-10-17. This is the handoff guide for continuing the engine
refactor without rebuilding its experiment harness from memory.

## Current ownership map

| Area | Current location | Role |
|---|---|---|
| Engine and renderer | `src/World/`, `src/Render/` | Product implementation and runtime scheduling/telemetry |
| Build and packaging | `scripts/build/`, `tools/build_release.ps1`, root CMake files | Release build setup |
| Flight runners | `tools/flight_sim_run.py`, `tools/flight_sim_fixed_day.py`, `tools/run_m335_water_visibility_ab.ps1` | Visible GUI flight orchestration and report generation |
| Offline analysis | `tools/flight_sim_analyze.py`, `tools/analyze_renderer_pixel_trace.py`, `tools/analyze_relight_queue_age.py`, and the other `tools/analyze_*` / `tools/compare_*` utilities | Interpret saved evidence; they do not prove a visual defect without a joined framebuffer/depth/world witness |
| Experiment method and history | `docs/streaming/FLIGHT_EXPERIMENT_SCRIPTS.md`, `docs/streaming/ENGINE_RENDER_DIAGNOSTIC_RUNBOOK.md`, current audit and plan | Canonical route, procedures, evidence, caveats, and remaining work |
| Runtime output | `bin/logs/`, `bin/suite_reports/`, `bin/flight_captures/`, `bin/iter_reports/` | Mixed build/runtime output, some tracked historical baselines and many local runs |

There are 326 tracked files under `tools/`, 17 under `scripts/`, and 164 under
`docs/streaming/` including this catalog. Naming and placement grew
incrementally, but the current script source inventory is versioned. It lists
85 engine-research scripts under `tools/` and 171 historical/research scripts
under `bin/` (157 Python,
6 PowerShell, 7 batch, and one C++ source). See
[`ENGINE_EXPERIMENT_SCRIPT_INVENTORY.md`](ENGINE_EXPERIMENT_SCRIPT_INVENTORY.md)
for every tracked path. Earlier untracked source scripts were curated into
Git by `6d5e40ee`; the remaining untracked files are generated output/world
data or local Git-commit wrappers. Do not clean or bulk-stage `bin/`.

## Investigation protocol

1. Start from a question that can be falsified, and name the expected evidence
   before changing code. Separate renderer output, world voxel data, light
   fields, mesh publication, queue ownership, disk load, and procedural
   generation; a dark pixel by itself does not identify which failed.
2. Preserve the canonical M335 conditions for comparisons: same warm saved
   world, start position, view, speed, daylight, visible GUI, and no teleport.
   Use the short established route for operator-visible checks. Extend flight
   distance only when a specific bug requires a far boundary, and keep
   long-running evidence collection unattended rather than waiting on an
   operator to watch it.
3. Change one variable per comparison. Keep cold-load/new-world checks as a
   separate experiment because they exercise generation and persistence paths
   absent from a warm replay.
4. Capture a Release build identity, command line, selected environment,
   world/save identity, route completion, and all report/log/frame paths. Record
   host load and sleep/hibernate separately; neither may silently become a
   product acceptance condition.
5. Interpret gates by what they observe. Product movement gates, proxy hole
   counts, luma thresholds, draw readiness, and saved screenshots are distinct
   signals. A dark/empty-looking screenshot is not proof that the voxel column
   is empty; a proxy count is not framebuffer proof. Join time, position,
   camera, depth, drawable geometry, current light revision, and relevant queue
   state before attributing a symptom.
6. After a code change, build only Release, run the shortest route that
   exercises the changed path, compare against the last matching baseline, and
   update the audit, plan, runbook, and script index. Commit source and curated
   evidence explicitly; never use `git add bin/`.
7. For `CUBATARIUM_RELIGHT_AUDIT` runs, use
   `tools/analyze_relight_queue_age.py` on the matching glog INFO file. It
   estimates first-insert to next capture dequeue; coalesced requests do not
   reset the age, and missing insert events remain explicitly unknown. Once
   job-id submission records are available, `--jobs` pairs submit/apply; that
   wall interval still combines worker execution and completed-result wait.

## Release build

Use the tracked app-only wrapper so Windows builds compile in multiple
processes:

```powershell
tools/build_release.ps1 -Jobs 8
```

`cmake --build bin --config Release --target Cubatarium --parallel 8` alone can
still leave MSVC at `/MP1`: CMake uses `CL_MPCount=1` while enabling MSBuild's
project-level `/m`. `tools/build_release.ps1` forwards the chosen job count as
`/p:CL_MPCount=N` too. Confirm compiler-level concurrency from the active `cl`
response files (`/MPN`) or multiple `cl.exe` workers. Keep this separate from
runtime worker counts, which must be read from the engine's generation/stream
diagnostics.

## Metrics and disk writes

`UFramePerfMonitor` is gated by the process environment variable
`CUBATARIUM_PERF_LOG`. When unset, empty, or set to `0`, `false`, `off`, or
`no`, `OnInGameFrame` exits before the expensive frame snapshot and JSONL
write; no `perf_*.jsonl` session is opened. A normal game launch therefore
has periodic FramePerf collection and disk output off. Set it to `1` before
launch only for an experiment that needs that trace. `tools/flight_sim_run.py`
sets it to `1` because its gate analysis requires the perf file; this is a
measurement-harness behavior, not the normal runtime default.

Runtime telemetry that drives scheduling, memory budgets, and rendering stays
available in memory; it is not the same thing as periodic perf logging. When
FramePerf logging is off, the memory-budget fallback samples process memory at
a bounded 30 streaming-frame cadence, which is required by runtime policy and
does not emit perf JSONL. The normal INFO application log remains a separate
log channel. Trace channels such as `CUBA_VISUAL_BLACK_TRACE`,
`CUBA_WORLD_COLUMN_SOURCE_TRACE`, and `CUBATARIUM_RELIGHT_AUDIT` are separately
opt-in and must be enabled only for a named experiment; the M335 wrapper
restores the process environment after its run. On refactor completion, keep
the game default off, leave only bounded/low-cost runtime telemetry that the
engine needs, and require an explicit opt-in for disk traces.

## Layout recommendation

The current split is serviceable but not a good long-term home for future
subsystem refactors: `tools/` mixes reusable runners, one-off experiments, and
analyzers; `bin/` mixes build products, saves, raw logs, reports, and selected
historical script sources. Keep the present paths stable while moving toward
these boundaries:

```text
tools/research/<subsystem>/run/       repeatable experiment entry points
tools/research/<subsystem>/analyze/   offline analyzers and report builders
tools/research/<subsystem>/capture/   window/frame/evidence helpers
docs/research/<subsystem>/            protocol, inventory, audit, and plan
artifacts/research/<subsystem>/<id>/  manifests, reports, logs, and captures
```

`tools/` remains the shared home for generic build/test utilities; the
subsystem folders are for research tooling. `artifacts/` is local/ignored by
default; raw screenshots, perf traces, and app logs are not source files.
First add a configurable artifact root to the flight harness and write a run
manifest with explicit paths for the report, perf JSONL, INFO logs, captures,
and analysis outputs. Stop relying on "newest file" discovery where the app
report already knows its own path. Then add compatibility shims at existing
`tools/` paths, migrate and verify one script family at a time, and only then
move source scripts. Keep selected baselines in Git only with an explicit
rationale. Reuse this layout for future renderer, physics, worldgen, and
content refactors; do not create a separate artifact convention per subsystem.

## Outstanding follow-up

- Add a run-manifest/artifact-root option and update the M335 wrapper and
  analysis path discovery to use it. This is the next tooling change; do not
  relocate source scripts in the same change.
- Keep the newly versioned script inventory synchronized when a reusable
  runner, analyzer, capture helper, or experiment recipe is added or retired.
- Remove run-specific environment instrumentation after each experiment; keep
  the documented switches and default them off.
- Finish the rendering tasks in
  `ENGINE_REMEDIATION_PLAN_2026-10-03.md`: dependency-driven boundary relight,
  deterministic skylight air/water validation, and short cold/far-route closure.

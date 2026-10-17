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

There are 326 tracked files under `tools/`, 17 under `scripts/`, and 163 under
`docs/streaming/`. The experiment tooling is already mostly in Git, but naming
and placement have grown incrementally. The initial inventory found 7,145
untracked files under `bin/` (mostly images, JSON reports, and saved chunk
data), including 104 Python files, 4 PowerShell files, 34 batch files, and
research notes. The selected research sources and notes listed below are now
archived in Git; remaining generated and legacy files are user data. Do not
clean or bulk-stage them. The `_commit*.bat` files in particular perform Git
writes.
The research archive now includes the 104 Python analyzers/helpers, 4
PowerShell drivers, one C++ reproducer, and 16 research notes, plus the
experiment/score wrappers `_own_af_suite.bat`, `_v4_af_suite.bat`,
`_v5_af_suite.bat`, `_v6_af_suite.bat`, `_v4_score_all.bat`,
`_v5_score_all.bat`, and `tmp_run_audit191112.bat`. Git-writing commit
wrappers remain local and untracked.

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

## Metrics and disk writes

`UFramePerfMonitor` currently computes a large per-frame diagnostics record and
creates `bin/logs/perf_*.jsonl` whenever the application runs, even when the
performance overlay is hidden. This work adds the opt-in environment variable
`CUBATARIUM_PERF_LOG=1`. With it unset or set to `0`, the periodic collector and
its JSONL writes are disabled. The flight experiment harness opts in because
its gate analysis consumes that file. Direct game launches default to no
performance JSONL. Use the same switch to disable the collector in an ad-hoc
run; the harness intentionally turns it back on for analyzed flights.

Engine telemetry used by streaming, scheduling, and rendering decisions remains
in the runtime. The switch only controls the periodic `FramePerfMonitor`
collection, INFO summaries, and its disk session. The memory-budget code
samples process memory at a bounded 30 streaming-frame cadence when no periodic
sample exists. Environment
trace channels such as `CUBA_VISUAL_BLACK_TRACE`,
`CUBA_WORLD_COLUMN_SOURCE_TRACE`, and `CUBATARIUM_RELIGHT_AUDIT` are separate
opt-in diagnostic paths and should be set only for a named experiment. After
the refactor is accepted, keep direct launches at the default (off) and retain
the switch so future investigations can opt in again.

## Layout recommendation

Do not mass-move the current scripts yet: experiment notes contain literal
paths, and wrappers are used by previous run records. Keep existing entry
points working while moving toward these boundaries:

```text
tools/engine-research/flight/       repeatable run entry points
tools/engine-research/analyze/      offline analyzers
tools/engine-research/capture/      image/window evidence helpers
docs/engine-research/               shared protocol and cross-subsystem index
artifacts/engine-research/<run-id>/ reports, perf JSONL, app logs, frames
```

First add a configurable artifact root to the harness and make each run write a
manifest that references logs rather than relying on "newest file" discovery.
Then add compatibility shims at the old `tools/` paths, migrate and verify one
script family at a time, and only then move source scripts. Keep raw screenshots
and logs out of routine source commits; commit selected baselines with an
explicit rationale. Apply this same layout to later subsystem refactors so
engine implementation, reusable tooling, and evidence have clear owners.

## Outstanding follow-up

- Inventory the 104 untracked Python files in `bin/`, classify reusable flight
  analyzers versus one-off scratch scripts, and promote only the reusable
  pieces without destroying the originals.
- Add a run manifest/artifact-root option and update the M335 wrapper and
  analysis path discovery to use it.
- Remove run-specific environment instrumentation after each experiment; keep
  the documented switches and default them off.
- Finish the rendering tasks in
  `ENGINE_REMEDIATION_PLAN_2026-10-03.md`: dependency-driven boundary relight,
  deterministic skylight air/water validation, and short cold/far-route closure.

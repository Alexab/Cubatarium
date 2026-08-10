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

## 2a. Timeline + run_outcome (P0 harness)

После каждой фазы A0–D / E1–E5:

```powershell
python tools/flight_sim_run.py --world World_164 --teleport-cruise --seconds 130 `
  --fly-stop --fly-phase-sec 45 --stop-phase-sec 60 --idle-sec 8 `
  --process-timeout 420 `
  --phase-id <PHASE_ID> --report bin/iter_reports/timeline/<PHASE_ID>.json
```

Verify in report: `run_outcome=success`, `info_tail` present. Commit фазы **только** при `run_outcome=success`.

**V2–V5 architecture freeze (2026-07-28, `arch/streaming-v2-v4`):** E1–E5 code
contracts landed (SeedDecision, ColumnFlowExecutor, visual SLA, seed factory,
Capture progress). Gate DoD F2/C/CB may still be NO-GO — do not treat docs ✅ as
gate GO; see `TECH_DEBT_CHUNK_STREAMING.md` TD-ARCH-011/015 backlog.

**Era13 readiness DoD (2026-07-29):** architecture contract gates (not GPU ladder):

```powershell
python tools/flight_sim_phase_gate.py --phase-id ARCH_D1 --report bin/iter_reports/timeline/<id>.json
python tools/flight_sim_phase_gate.py --phase-id ARCH_D3 --report bin/iter_reports/timeline/<id>.json
```

| Gate | ARCH_D1 | ARCH_D3 |
|------|---------|---------|
| `post_load_ring_idle_max` | =0 | =0 |
| `effective_holes_rate` | ≤0.24 | ≤0.10 |
| `mesh_async_med_when_dirty` | ≥4 (FOV unfinished) | ≥4 |
| stop not_ready / sticky / holes | 0 | 0 |
| `stop_dark_face_near_end` | &lt;200 | &lt;100 |
| `wall_ms_med` | ≤35 | ≤30 |

Do **not** merge to `develop` until ARCH_D3 GO (+ F2/C/CB as before).

## Era14 V4 reject list (2026-08)

Reject PR / do not merge streaming changes that:

- Reintroduce calm-wall Imm as **primary** FirstMesh (`last_frame_ms≤40` gate on rim Imm).
- Gate stale-wave **enqueue** on `last_frame_ms≤50` (cost throttle only).
- Nest `UpdateStreaming` / `TickMeshEmerge` back inside `RunLegacyPhysicsFrame`.
- Add stand vs cruise sticky Imm forks.
- Cap `mesh_schedule` on holes; fog-as-throughput; SoftDefer zoo packages.
- Close a phase without autofly gate report + TD-ARCH update in
  `TECH_DEBT_CHUNK_STREAMING.md`.

Autofly loop (every code phase): build → `flight_sim_run` → analyze →
`flight_sim_phase_gate` → update TD → checkpoint commit. Land scenarios
(`--land-cruise` / `ARCH_D3_LAND`) required for rim/tops phases.

**Era14 residual (2026-08-07):** do not merge to develop until `ARCH_D3_LAND`
GO (holes≤0.10) and ocean `ARCH_D3` wall≤30. Best land near-GO:
`bin/iter_reports/timeline/era14_p2c_land.json` (miss_stuck=4, wall≈54.6,
sticky=0, holes≈0.2). Era14.1: PreferKick tops HP + SoftDefer miss floor +
phase budget 24ms + Android phase parity; `IDLE_CLEAN`/`IDLE_WARM`/`FLY_CLEAN`
GO (`era14_1_idle`/`warm`/`fly`); land best `era14_1_land` miss=6 holes=0.2
wall≈53; ocean `era14_1_ocean` wall≈49. Worker Capture deferred (TD-ARCH-046).

**Era15 architecture-first (2026-08-08):** visual residual SoT landed — MeshResidency
(TD-049 **done**), ColumnPublication + SoftDeferHeld→ColumnFlow (TD-050 **done**),
PreferKick Kicked stall (TD-051 **partial**). Evidence: `era15_p1_fly` FLY_CLEAN GO;
`era15_p4_idle`/`warm` IDLE GO; `era15_p3b_land` sticky=0 miss_end=0 wall≈45 but
holes≈0.24 miss_stuck=8 (TD-043 still open). Ocean ARCH_D3 / TD-048 open.
Worker Capture deferred (TD-ARCH-046). Do not merge to develop until ARCH_D3_LAND
holes≤0.10.

**Era16 VisibleBlack closeout (2026-08-08):** TD-052 **done**. Hide⇒Ticket SoT —
`VisibleBlackNoTicketN=0` hard on IDLE_CLEAN + ARCH_D3_LAND. P3 matrix GO:
`era16_p3_fly` / `idle` / `warm` / `land` (holes=0 sticky=0 no_ticket=0 wall≈66,
ARCH_D3_LAND wall soft≤70 for remesh tax). TD-043/051 closed via Era16 P2.
Ocean `era16_p3_ocean` ARCH_D3 soft NO-GO (wall≈226 holes≈0.82; no_ticket=0) —
TD-048 residual, not Era16 DoD. Reject: false StaleDark tickets; gating only
on `black_sticky` while VisibleBlack orphans remain.

**Era17 heal-until closeout (2026-08-08):** TD-053 **done**. Contains-only ticket +
Progress/Stalled telem; continuous VB heal (void RelightThenMesh); FirstMesh class
when miss cy≤1. P3 matrix: `era17_p3_fly`/`idle`/`warm`/`land` GO; ocean soft
(`era17_p3_ocean` ARCH_D3 NO-GO — TD-048). Manual residual → Era18 TD-054.

**Era18 focus light-debt closeout (2026-08-08):** TD-054 **partial**. Void
RecoverUnlit⇒`NotePendingLightBeforeMesh`; bg_budget/idle_recovery/SoftDeferCapture
floors while `VB>0`; focus FIFO pin; miss unfinished-storm FirstMesh; hitch drain.
P3 matrix autofly GO; ocean soft (`era18_p3_ocean` ARCH_D3 — TD-048). Manual
`191229`: light-debt soft OK but **wall_med≈279 holes≈0.57 miss_stuck≈40s** —
**regressed FPS/miss** (TD-055). Reject: claim CLOSED on autofly while heal-floors
force spend on hitch; knobs-as-DoD; Worker Capture (TD-046).

**Era19 FrameStreamingBudget (2026-08-08):** TD-055 **partial** (autofly GO;
manual `214034` holes/black residual → TD-056). Unified `FrameStreamingBudget`.
Reject: Era18-style `max` floors while VB on hot wall; Worker Capture.

**Era20 Manual Visual SLA (2026-08-08):** TD-056 **partial** (autofly GO;
enter_app≈100; manual `102236` improved FPS/no_ticket — flicker/FOV → TD-057).
Reject: Imm-off while async=0 under miss; heal-floors-on-hitch; Imm primary zoo.

**Era21 Residency FOV (2026-08-09):** TD-057 **partial** (autofly GO;
manual `154049` eye → TD-058). Keep GPU until BindCommitted;
SoftDefer Capture FirstMesh-only under miss; RecoverUnlit Satisfying;
VB mid-floor under miss+hot; unload Dirty>64 gate. Analyze:
`mesh_discarded_late_delta_cruise`, `vb_progress_without_dark_clear_sec`.
Reject: FreeChunk-before-Bind; Relight-ticket-blocks-miss-Capture; Imm primary;
hitch VB Capture storm.

**Era22 SoftDefer Heal SLA (2026-08-09):** TD-058 **partial** (autofly matrix;
manual `172232` eye → TD-059). SoftDefer FirstMesh schedule under
miss/focus; SoftDeferHeld ∈ progress + Contains cy; VB full-focus tickets when
no_ticket; miss age PreferKick; async floor≥12 post-Finalize under miss|UV.
Reject: SoftDeferHeld-without-Contains; VB-collect≪Count; Imm-as-heal;
Era18 hitch Capture storm; SoftDefer knobs-as-fix.

**Era23 Void Relight / rim miss (2026-08-09):** TD-059 **partial** (autofly
GO; manual `193059` blacks closed void/dark=0; SoftDefer empty → TD-060).
Reject: Held-as-void-progress; Relight-steal-FirstMesh; Imm-as-heal; hitch
Capture; SoftDefer knobs.

**Era24 SoftDefer Empty FirstMesh-until-Drawable (2026-08-09):** TD-060
**partial** (autofly FLY/IDLE/WARM GO; LAND soft post_stop miss; mid-corridor
eye OK on `203144`; frontier void → TD-061). Hide⇒Ticket; FirstMesh ownership +
age SLA; Capture SoftDefer stuck cy pin. Reject: SoftDefer-empty-as-HasGreedy-
progress; Imm-as-empty-heal; PreferKick every empty every frame; SoftDefer
knobs-as-DoD.

**Era25 Frontier Column Stage SLA (2026-08-09):** TD-061 **partial** (autofly
FLY/IDLE/WARM GO; LAND soft post_stop miss; wait manual `203144`-class frontier
void≪412 / empty_stuck≤2 / miss_end=0). Disk/gen telem; light ticket + FirstMesh
on near commit; frontier_pressure dual-queue; NearLoad/PrefetchAhead load-ahead.
Reject: Imm FOV; SoftDefer knobs-as-DoD; hitch Capture; Relight-steal-FirstMesh;
stream_loads-as-gen-progress.

**Era26 Ocean Dual-Debt + Load Light Parallel (2026-08-09):** TD-062 **partial**
(autofly FLY/WARM GO; IDLE soft wall/emerge; LAND soft miss; wait manual
`214325`-class ocean void_med≪249 / empty_stuck≤2 / miss_end=0). Lateral Relight
under miss; SoftDefer empty∥void; FillWater lateral Y; coop async RelightColumns.
Reject: Imm; SoftDefer knobs; Relight-steal-FirstMesh Capture; live parallel
RelightColumn; coop frontier re-queue.

**Era27 Anti-Flicker Ownership (2026-08-09):** TD-063 **partial** (autofly
FLY/IDLE/WARM GO; LAND soft miss; wait manual `224912`-class retarget/f≤1.5,
discarded_late cruise≤2, miss_end=0, no remesh-blink). Capture witness pin T=8;
SoftDefer empty age sticky; MarkRelit remesh damp SoftDefer-empty owned; Inflight
supersede hold under miss. **No new drawable cache** — PendingReplace /
keep-GPU is the residency layer. Reject: Imm; SoftDefer knobs-as-DoD;
PreferKick every empty every frame; FreeChunk-before-Bind; second mesh cache;
claim CLOSED on autofly while manual still blinks.

**Era28 Visual Stage Gate (2026-08-10):** TD-064 **partial** (autofly FLY/WARM GO;
IDLE soft dirtyΔ; LAND soft miss; wait manual `012208`-class opaque≪658 / unlit↓ /
empty≤4s / miss_end=0). Near FOV hide-until-lit; SoftDefer Dirty coalesce;
PreferKick after age SLA; RemeshAfterApply-only while Building. Autofly opaque
churn ≤67 (vs manual 631). Reject: Unlit near «ради дыр»; Imm; second cache;
CLOSED without manual eye.

**Era29 Enter Visual Warmup (2026-08-10):** TD-065 **partial** (autofly FLY/IDLE/WARM
GO; LAND soft miss; wait manual `091332`-class ENTER eye). Bar-side streaming always;
underfeet LitDrawable gate (cap 24); spawn Capture pin T=16; near VB honesty;
far Unlit remesh damp; idle drawable RemeshAfterApply. Autofly opaque≤103 (≪880).
Reject: RD+1 bar wait; Unlit near on bar; MarkAllDirty warmup; CLOSED without ENTER eye.

**Backend matrix (R4):** desktop

```powershell
python tools/flight_sim_iterate.py --backend cpu --replay-edge ...
python tools/flight_sim_iterate.py --backend gpu --replay-edge ...
```

Android seed stays CPU until TD-ARCH-013b (`ANDROID_GPU_BACKLOG.md`).

Edge replay (World_164 boundary):

```powershell
python tools/flight_sim_run.py --replay-edge --report bin/iter_reports/edge_replay.json
```

Ocean FillWater cruise (manual `104841` corridor):

```powershell
python tools/flight_sim_run.py --scenario ocean-cruise --phase-id OCEAN_CRUISE `
  --report bin/iter_reports/era30_ocean_cruise.json --process-timeout 480 --warmup-sec 16
python tools/flight_sim_phase_gate.py --phase-id OCEAN_CRUISE `
  --report bin/iter_reports/era30_ocean_cruise.json
```

Rolling summary: `bin/iter_reports/timeline_summary.json` via `flight_sim_iterate.py --timeline-summary`.

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
| -------- | -------- |
| cold_relight_holes_sec | P0 frontier stall |
| wall_ms_no_holes_med | moving FPS without holes |
| dirty_med_no_holes | F2 remesh thrash |
| spike_count / spike_max_wall / spike_max_wall_holes | flight hitch |
| sticky / nr_end / fd_end | stop contract |

## 4b. Auto-iteration loop (optional, recommended)

Для системной отладки регрессий (spike + sticky-dark + light debt) используйте
итерационный раннер:

```powershell
python tools/flight_sim_iterate.py --world World_164 --iterations 3 --build-first `
  --phase-prefix perf_iter `
  --summary bin/flight_sim_iterate_summary.json
```

Что делает раннер:

- запускает воспроизводимые автопролёты по фиксированному сценарию;
- собирает `perf_*.jsonl` и ближайший `Cubatarium.exe*.INFO.*`;
- классифицирует причины (main-thread hitch, light debt plateau, dirty backlog, sticky-dark);
- формирует рекомендации для следующей доработки;
- останавливается раньше, если выполнены критерии по spike/wall/pending/dark.

## 4c. Era31 Ocean Heal Throughput (smoke ≠ DoD)

**Smoke** (`OCEAN_CRUISE`) — autofly `ocean-cruise`; after void-parity harness (sea+10, no HoldSpace, −550/110) smoke sees real debt — aspirational clean thresholds may NO-GO (honest).

**Parity** (`OCEAN_CRUISE_STRESS`) — cold teleport + idle=3 + sprint; must reproduce void≥400 + holes≥40% (DoD for harness).

**DoD** (`OCEAN_MANUAL`) — analyze manual `122032`/`153653`-class log; CLOSED только при GO + eye.

Era31 bisect metrics: `void_peak_period_idx`, `void_drain_rate`, `emerge_spike_frac`, `vb_progress_without_dark_clear_sec`.

```powershell
# Smoke
python tools/flight_sim_run.py --scenario ocean-cruise --phase-id era31_ocean_smoke `
  --report bin/iter_reports/era31_ocean_smoke.json
python tools/flight_sim_phase_gate.py --phase-id OCEAN_CRUISE --report bin/iter_reports/era31_ocean_smoke.json

# Parity matrix
python tools/flight_sim_run.py --scenario ocean-cruise-stress --phase-id era31_ocean_stress `
  --report bin/iter_reports/era31_ocean_stress.json
python tools/flight_sim_phase_gate.py --phase-id OCEAN_CRUISE_STRESS --report bin/iter_reports/era31_ocean_stress.json

# Manual DoD (122032 SoT)
python tools/flight_sim_analyze.py bin/logs/perf_20260810-122032_27372.jsonl `
  --manual-idle --warmup-sec 16 --report bin/iter_reports/manual_122032.json
python tools/flight_sim_phase_gate.py --phase-id OCEAN_MANUAL --report bin/iter_reports/manual_122032.json

# Side-by-side parity
python tools/flight_sim_parity.py `
  --manual-report bin/iter_reports/manual_122032.json `
  --autofly-report bin/iter_reports/era31_ocean_stress.json
```

Matrix scenarios: `ocean-cruise` | `ocean-cruise-enter` | `ocean-cruise-stress` | `ocean-cruise-short`.

See also: [`ERA31_OCEAN_BASELINE.md`](ERA31_OCEAN_BASELINE.md), [`ERA31_AUTOFLY_RESULTS.md`](ERA31_AUTOFLY_RESULTS.md).

## TD-066 (partial) / Era32 LitDrawable

Ocean cruise SLA tracked in `OceanCruisePolicy.h` + Era32 LitDrawable FOV
([`ERA32_LITDRAWABLE_BASELINE.md`](ERA32_LITDRAWABLE_BASELINE.md)). **Status: partial**
until `OCEAN_MANUAL GO` + eye on `165423`-class (land+ocean). Era32 landed:
lit_ring=4, slice draw hide for fully-dark, VB→RelightThenMesh, SoftDefer empty
no FreeChunk resident, RemeshAfterApply VisualStage damp, FirstMesh admit floor.

## 4d. Era32 LitDrawable FOV (autofly every code phase)

```powershell
python tools/flight_sim_run.py --scenario ocean-cruise --phase-id era32_ocean `
  --report bin/iter_reports/era32_ocean.json
python tools/flight_sim_phase_gate.py --phase-id OCEAN_CRUISE --report bin/iter_reports/era32_ocean.json
python tools/flight_sim_run.py --scenario ocean-cruise-stress --phase-id era32_stress `
  --report bin/iter_reports/era32_stress.json
python tools/flight_sim_phase_gate.py --phase-id OCEAN_CRUISE_STRESS --report bin/iter_reports/era32_stress.json
python tools/flight_sim_run.py --land-cruise --phase-id era32_land `
  --report bin/iter_reports/era32_land.json
python tools/flight_sim_phase_gate.py --phase-id FLY_CLEAN --report bin/iter_reports/era32_land.json
python tools/flight_sim_phase_gate.py --phase-id ARCH_D3_LAND --report bin/iter_reports/era32_land.json
```

CLOSED only with `OCEAN_MANUAL` + land eye — not autofly smoke alone.

## 5. Anti-patterns (reject merge)

- dark preview / sync remesh flood при pending
- early `idle_remesh_debt` 12/20
- снятие heavy_dirty caps ради cold_relight
- `CancelAsyncInFlightKeepDirty` на idle remesh

## 6. GPU dual-stack (after G0–GA + P* + D1)

Desktop expect jsonl: `backend_mesher=gpu_greedy`, `backend_store=mdi_vertex_pool`,
`backend_cull=gpu_frustum`, `backend_fluid=gpu_fluid_surface`,
`backend_lighting_mode` in (`full`,`gpu_full`) — never `flat` on GPU stack,
`gpu_draw_cmds` med ≤15, `gpu_cull_indirect` ≈1, `gpu_fluid_scan_on` ≈1,
`gpu_mask_readback` med = 0.

```powershell
python tools/flight_sim_phase_gate.py --phase-id F2 --report bin/phase_D1.json
python tools/flight_sim_phase_gate.py --phase-id D1a --report bin/phase_D1.json
python tools/flight_sim_phase_gate.py --phase-id D1d --report bin/phase_D1.json
python tools/flight_sim_phase_gate.py --phase-id PA --report bin/phase_D1.json
python tools/flight_sim_phase_gate.py --phase-id GA --report bin/phase_D1.json
```

P* + D1 (Desktop): cull→MDI (P2), single pool upload (P3), PreferGpu fluid (P7),
skylight without sky GetBufferSubData (D1.3), opaque greedy without mask readback
(D1.1), force Full lighting (D1.4).

Android/GLES: GPU-by-default when probe+allowlist pass; opt-out via
`render.android_gpu_enabled=false`. See
[`ANDROID_GPU_BACKLOG.md`](ANDROID_GPU_BACKLOG.md). Device smoke:
[`QA_ANDROID_2026.md`](../QA_ANDROID_2026.md). Factory:
`render_backend_factory_test`, `android_gpu_policy_test`.
Phase runner: `tools/android_gpu_phase_run.py` (desktop + android assemble + gates).

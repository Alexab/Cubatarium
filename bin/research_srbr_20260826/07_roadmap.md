# 07 — Implementation roadmap

Дата: 2026-08-26 · после SRBR research  
Depends on: `04_hotspot_matrix.md`, `05_best_practice_scorecard.md`, `06_arch_options.md`

---

## Package P0: Ghost Dirty drain + underfeet miss ownership

- **Depends on:** H1 CONFIRMED, H2 CONFIRMED
- **Files:**
  - `src/Render/Mesh/ChunkMeshCache.cpp` (MarkDirty*/schedule orphan path)
  - `src/World/Streaming/ChunkEmergeCoordinator.cpp`
  - `src/World/Streaming/SoftDeferEmptyPolicy.h` / `MeshWorkAdmission.h` (predicates)
  - Units: `MissFirstMeshClassTest.cpp`
- **Change summary:**
  1. Never re-admit Dirty for coords without `HasChunk`.
  2. Bulk prune ghost Dirty each emerge tick (cap) instead of RemoveAt×54/frame thrash.
  3. Guarantee FirstMesh schedule for sticky miss nh≤1 when HasChunk + !drawable.
- **KEEP / NEVER:** SoftDefer KEEP; P13 remesh protect; miss_undrawn; MaxAge decay; no Capture++
- **Unit tests:** ghost Dirty not requeued; nh≤1 HasChunk schedules under SoftDefer
- **Autofly gates (no-teleport only):**
  - `fz-ne-frontier-stand` SoftDefer KEEP PASS
  - `fz-manual-plateau` no stream/VB regress vs prior plateau band
- **Manual DoD (≥180s land stand keep≥160):**
  - skip_orphan med ≤10
  - miss_frac ≤0.5 (target ≤0.2)
  - unf late ≤40 (falling vs early)
  - VB late ≤70
  - stream_ms med ≤30
  - discard/spike ≤0.05
  - SoftDefer KEEP
- **Guardrails:** sim med <135; keep≥160; SiteB≤5; floor≤8
- **Rollback:** revert package commit; SoftDefer audit must stay green
- **Est. agent effort:** 2–4 days code + 1 manual SoT

---

## Package P1: Ticketed VB / unlit consume on stand

- **Depends on:** H3 CONFIRMED; P0 green or at least skip_orphan↓
- **Files:**
  - `MarkRelitInstall.cpp` / `RelightInstallPlanner.h`
  - `RelightFifoPolicy.h` (consume / force MarkRelit for ticketed)
  - `WorldStreaming.cpp` (stand consume budgets)
- **Change summary:**
  1. When VB_focus high + no_ticket low + unlit_hidden≈PL_focus: prefer lit remesh / MarkRelit for focus FullyDark.
  2. Do not starve remesh floor under ticketed VB stand (align with P13 protect spirit).
  3. Telem: ticketed_vb_age / unlit_focus_age if missing.
- **KEEP / NEVER:** no F5 deadband; no min_cap=3 over fat time_cap; SoftDefer KEEP
- **Autofly:** `fz-manual-plateau` + `fz-manual-parity` (no-teleport)
- **Manual DoD:**
  - VB late ≤50
  - unlit_hidden late ≤25
  - pending_light_focus late ≤25
  - opaque_refs stand ≥550
  - SoftDefer KEEP; discard≤0.05
- **Guardrails:** sim<135; stream≤30; no SoftDefer thrash
- **Rollback:** revert; compare vs P0 SoT
- **Est.:** 3–5 days + manual ≥180s

---

## Package P2: Per-epoch scan cache + cost attribution telem

- **Depends on:** H4/H7; Variant B
- **Files:** `WorldStreaming.cpp`, `ColumnFlowExecutor.cpp`, `FramePerfMonitor.cpp`, optional SoftDefer scan memo
- **Change summary:**
  1. Single VB/Unfinished sample per frame epoch.
  2. Counters: dirty_ghost_n, miss_sticky_age, ticketed_vb_age.
  3. Ban live CountVisibleBlack fallback when sample valid.
- **Autofly:** `fz-manual-long` no-teleport (stream regression)
- **Manual DoD:** stream_ms stand ≤25; no functional regress vs P1
- **Est.:** 2–3 days

---

## Later (not now)

| Item | Why defer |
| --- | --- |
| FIFO trim O(N·drops) rewrite | H8 REJECTED as stand root |
| SoftDefer MaxAge / Capture expand | SoftDefer already KEEP; P14 lesson |
| Unified V4 work graph | Variant C after A+B |
| Teleport `fz-inring-cruise` as DoD | blacklisted |

---

## Execution order

```text
P0 ghost Dirty + miss ownership
  → SoftDefer KEEP + manual stand gates
P1 ticketed VB consume
  → VB/unlit/opaque gates
P2 scan cache + telem
  → stream≤25 + RCA hygiene
```

## Validation commands

```bat
cmd /c "python bin\tmp_fz27_b_test_smoke.py"
cmd /c "python tools\flight_sim_suite.py --only fz-ne-frontier-stand fz-manual-plateau --phase-id SRBR-Px"
cmd /c "python bin\tmp_audit_ne_frontier_stand.py <new> bin\logs\perf_20260826-104722_26064.jsonl"
cmd /c "python bin\tmp_audit_100413_stand.py <new> bin\logs\perf_20260826-100413_20156.jsonl"
cmd /c "python bin\tmp_audit_112418.py"   rem: or successor SoT compare vs 112418
```

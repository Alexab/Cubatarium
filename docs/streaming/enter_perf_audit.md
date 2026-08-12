# Enter-load performance audit (Era44–Era46)

Manual reference runs: World_174.

| Run | Log | Symptom |
|-----|-----|---------|
| Pre-Era44 | `20260812-153650` | gpu_warmup ~120s, mesh abort forced InGame with holes |
| Era44 variant A | `20260812-163342` | coop PrepareView >8 min, plateau fifo≈6–13, gpu≈15–26, mesh_dirty=1 |
| Era45 post-fix | `20260812-175811` / `enter_lit_20260812-175840.jsonl` | coop >26 min; mark_relit_raa_total froze at 33; **gpu≈15–24 plateau** |
| Era46 verify | `20260812-191815` / `enter_lit_20260812-191911.jsonl` | remesh_after_apply_n med≈1 (was ~13); gpu≈8–18 + mesh_dirty=1 still; run cut ~105s wall |

## Era45 post-mortem (175840)

| Metric | t≈34s | t≈120s (coop_abort_drain) | t≈26min |
|--------|-------|---------------------------|---------|
| fifo | 77→11 | 5 | 3–15 |
| gpu_pending | 5→17 | 17 | **15–24 (med≈19)** |
| mesh_dirty | 1 | 1 | **1** |
| remesh_after_apply_n | 5→16 | 13 | **6–20 (med≈13)** |
| mark_relit_raa_total | 0→33 | 33 | **33 frozen** |
| suppress_relight_seam | 0 | 0 | 0 |

**Era45 partial:** MarkRelit RAA storm stopped (`mark_relit_raa_total→33`); B5 suppress=0. **Not systemic:** drain throughput left gpu/dirty plateau (R7). Abort wall only grew `(Ns)` status — gate still held (`load_settled=false`).

## Root causes

| ID | Verdict | Notes |
|----|---------|-------|
| R4 | Confirmed + Era45 partial / Era46 coalesce v2 | Residual `remesh_after_apply_n≈13` via MarkDirty→RAA without MarkRelit |
| R6 | Observed | snapshot debt=0 while fifo>0 (lit vs mesh-ready) |
| R7 | **Confirmed + Era46 fix** | Coop PrepareView lacked `DrainEnterGameMeshWarmup`; emerge iters = budget/4 vs gpu_warmup 6 |

## Era46 fixes (systemic throughput — not stop-condition bypass)

1. Shared enter drain frame: `DrainEnterGameMeshWarmup` + `TickEnterGateMeshDrain(EnterGateMeshDrainIterations)` on **both** coop Load PrepareView and Application gpu_warmup.
2. RAA commit coalesce v2: PreferKick if GPU still pending; MarkDirty only when clear; MarkDirty mid-flight PreferKick when GPU pending.
3. Telemetry: `ring_blocker`, `raa_commit_mark_dirty_n`, `markdirty_to_raa_n`; enter_lit jsonl under `bin/logs/` via exe dir.
4. Coop `RecordFrameSteps` + escalate GPU budget×2 only after abort_drain ≥3 min (gate unchanged).
5. Gate invariant KEEP: `phase=done` only when `IsSpawnMeshRingReady()` + lit debt clear.

## Era46 verify (191911)

| Metric | Era45 (175840 plateau) | Era46 (191911 ~0–105s) |
|--------|------------------------|-------------------------|
| remesh_after_apply_n | med≈13 | **med≈1** (often 0–5) |
| gpu_pending | 15–24 | 8–18 (still flat) |
| mesh_dirty | 1 | 1 (ring_blocker=dirty) |
| mark_relit_raa_total | 33 freeze | still grows (100@105s) |
| raa_commit_md / md_to_raa | n/a | 80 / 226 @105s |
| phase=done | no (26+ min) | not reached in this cut |

**Conclusion:** R4 residual (RAA set size) largely fixed by coalesce v2 + PreferKick. R7 drain parity is wired, but sticky `mesh_dirty=1` + gpu backlog still blocks `IsSpawnMeshRingReady()` — needs follow-up on stuck dirty slice ownership (not stop-condition bypass).

## Verification

```text
cd bin
.\Cubatarium.exe --console --autoload-last-world --visible --timeout-sec 300
```

| Metric | Era45 (175840) | Era46 target | Era46 observed |
|--------|----------------|--------------|----------------|
| coop PrepareView wall | >26 min | ≤3 min | incomplete run ~105s |
| mesh_dirty at settle | 1 | 0 | still 1 mid-run |
| gpu_pending at settle | 15–24 | 0 | ~15 mid-run |
| remesh_after_apply_n | ~13 plateau | 0 | **~1** |

Unit: `miss_first_mesh_class_test` — Era46 PreferKick/escalate/ring_blocker predicates — **OK**.

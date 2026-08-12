# Enter-load performance audit (Era44–Era47)

Manual reference runs: World_174.

| Run | Log | Symptom |
|-----|-----|---------|
| Pre-Era44 | `20260812-153650` | gpu_warmup ~120s, mesh abort forced InGame with holes |
| Era44 variant A | `20260812-163342` | coop PrepareView >8 min, plateau fifo≈6–13, gpu≈15–26, mesh_dirty=1 |
| Era45 post-fix | `20260812-175811` / `enter_lit_20260812-175840.jsonl` | coop >26 min; mark_relit_raa_total froze at 33; **gpu≈15–24 plateau** |
| Era46 verify | `20260812-191815` / `enter_lit_20260812-191911.jsonl` | remesh_after_apply_n med≈1; gpu≈8–18 + mesh_dirty=1; cut ~105s |
| Era47 pre-fix | `enter_lit_20260812-215450.jsonl` | quiesce latch on; sticky Dirty `(2,4,-14)` 135s+; timeout 181s `ingame_frames=0` |
| **Era47 GO** | `autoload ~57s` / `enter_lit_20260812-223146.jsonl` | **`ingame_ok`**, dirty cleared ~2s after quiesce |

## Era47 diagnosis

Not “finish all columns.” After lit SoT (`snapshot_debt=0`) EnterLitGate waited on `IsSpawnMeshRingReady()` while MarkRelit / Dirty↔GPU refeed kept ingress alive. Era47 scopes producers off under lit-quiesce and forces enter GPU admission.

Sticky World_174 remnant: single Dirty at cy=4 with `gpu/async=0` blocked the ring until early lit-quiesce prune (`!HasChunk` / SoftDefer park / drawable+soft_empty drop) + Application/`TickEnterWarmupDrainFrame` parity.

## Era47 fixes

| Phase | Change |
|-------|--------|
| P0 | enter_lit: `gpu_kick/finish`, `mark_relit_prefer_kick`, `dirty_schedule_skip_inflight`, `pending_gpu_global`, `enter_lit_quiesce`, `dirty_n`, stuck chunk/drawable |
| P1 | `ShouldSuppressMarkRelitRemeshOnEnterLitQuiesce` + latch on debt=0; Classify PreferKick/Skip only |
| P2 | `enter_lit_gate` → never Normal admission; ban Normal drain_cap=4 / schedule=1 under `EnterGpuQuiesceDrain` |
| P3 | RAA PreferKick-only under quiesce; no MarkDirty refeed; SoftDeferHeld no re-Dirty; early Dirty prune |
| P4 | Coop + Application both call `TickEnterWarmupDrainFrame` |

KEEP: no force `phase=done` / InGame on abort; gate still `IsSpawnMeshRingReady` + lit debt clear.

## Era47 verify (223146 + autoload_report)

| Metric | Era46 (191911) | Era47 target | Era47 observed |
|--------|----------------|--------------|----------------|
| after debt=0: mark_relit Schedule | grows | ~0 | mr_raa stays 0 |
| `gpu_pending_near` after lit-quiesce | plateau 8–18 | →0 | 0 by ~2s |
| `mesh_dirty` | sticky 1 | 0 | 0 by ~2s |
| `phase=done` / InGame | no | ≤3 min | **`ingame_ok` ~57s** |

```text
cd bin
.\Cubatarium.exe --console --autoload-last-world --visible --timeout-sec 300
```

Unit: `miss_first_mesh_class_test` — Era47 predicates — **OK**.

## Epoch note

Full `MeshPublishGate` deferred. Reopen if cruise/ocean repeats Dirty↔GPU refeed class outside enter.

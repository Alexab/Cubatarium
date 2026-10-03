# Bisect result B1 + next B2

## B1 result — FAIL / worse (manual `202741`)

Build: tip + **pre-A2 column frustum**. Logs:
- `bin/logs/perf_20260914-202741_25256.jsonl`
- `bin/logs/enter_lit_20260914-202840.jsonl`

| Mid-corridor | 154921 | tip `200927` (A2 ON) | **B1 `202741` (A2 OFF)** |
|---|---:|---:|---:|
| unlit med | — | **8** | **41** |
| unlit ≥40 frac | — | 5% | **51%** |
| dark_face_stale med | — | 45 | **113** (max **3514**) |
| mesh_apply_stale_visual | — | 3 | **14** |
| enter gate continuous | — | ~71 ms | **~8 s** |
| VB med | 77 | 71 | 69 |

Operator: fully black chunks + chunk-sized holes — matches telemetry (mass unlit + void/stale spikes).

**Verdict:** A2 row-frustum is **not** the 200927 texture-swap culprit. Reverting it re-exposes audit N02 false-negative cull. **A2 restored** (row planes in `Frustum.h`) + Release exe rebuilt ~20:33.

## B2 — ready for eye (A1 atomic publish off)

Keep A2. Working tree: pre-`58f65ffe` `GreedyGpuPublication.cpp` (+ test). Checklist: `bisect_b2_no_a1_manual.md`. Exe touched ~20:34.

# Phase Execution Log

Автоматический учёт прогонов фаз streaming (autofly `--teleport-cruise`).

| Phase | Branch | Commit | Report | sticky | nr_end | fd_end | cold | spike_max_h | F2 | Notes |
|-------|--------|--------|--------|--------|--------|--------|------|-------------|-----|-------|
| final_combined | phase-4-unified | ba98cfb9 | bin/phase_final_combined.json | 0 | 25 | 358 | 12 | — | FAIL | best pre-roadmap autofly |
| manual_134418 | snapshot | 9392ce5b | bin/phase_manual_134418.json | 0 | 25 | 347 | **14** | **3309** | FAIL | current UX snapshot |
| iter1_golden | iter1-harness | 9392ce5b | bin/phase_iter1_golden.json | 8 | 90 | 651 | 14 | 6637 | FAIL | flaky vs final_combined |
| iter1_replay | iter1-harness | 9392ce5b | bin/phase_iter1_replay.json | 0 | 80 | 595 | 8 | 6263 | FAIL | --replay-manual parity |
| iter23_combined | iter2-iter3 | 57221ea2 | bin/phase_iter23_combined.json | 5 | 39 | 432 | 12 | **1017** | FAIL | spike↓; sticky regress |
| **iter23_r2** | iter2-iter3 | TBD | bin/phase_iter23_r2.json | **0** | 51 | 454 | **6** | **788** | FAIL | **cold↓ spike↓ sticky=0**; nr/fd open |

Checklist: `PREMERGE_CHECKLIST.md`. Ownership: `ARCHITECTURE_OPTIONS.md` (Ownership Map).

## Current Snapshot — manual `perf_20260722-134418`

**Лог:** `bin/logs/perf_20260722-134418_25496.jsonl`  
**Маршрут:** (−472,48) → (−483,48).

### Плюсы (baseline UX)

- sticky ≈ 0, pending → 0, missing = 0 на stop
- SoftDefer работает; stop nr_end = 25, fd_end = 347

### Минусы (открытый долг)

| Хвост | Было (134418) | После iter23_r2 (autofly) |
|-------|---------------|---------------------------|
| A cold_relight | 14s | **6s** (цель ≤3) |
| B fd_end / moving FPS | fd 347 / dirty~520 | fd 454 (ещё открыто) |
| C spike_max holes | 3309ms | **788ms** |

### Что сделано в roadmap

1. **Iter1:** analyzer FPS/spike metrics, PREMERGE_CHECKLIST, snapshot docs, gate cold≤3 (уже в perf).
2. **Iter2:** `FocusIngressPolicy` + unit test; dedicated relight floor; spike guard (no non-underfeet sync fill when async&lt;4); promote dedupe.
3. **Iter3:** moving no-hole schedule clamp; landing remesh boost; `DrainFocusVisualWork`; ownership map.

F2 gate ещё FAIL (`nr_end`, `fd_end`, `cold_relight`). Рекомендация: merge `streaming/iter2-iter3-runtime` → `perf` для spike/cold wins; дальше точечно F2 dirty без ослабления spike guard.

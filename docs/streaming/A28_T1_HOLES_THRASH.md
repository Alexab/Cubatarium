# A28 T1 — Holes + thrash

Date: 2026-09-22  
Latest AF: `bin/_a28_t1_warm_af6.log`

## Result

| Gate | Status |
|---|---|
| `dirty_dropped/period` (delta median) | **PASS** — 471 ≤800 |
| `near_focus_holes` periods>0 | **FAIL** — 8 |
| A24 safety overall | **FAIL** (holes only) |

## Fixes landed

- Honest A24 metric: period-delta median (was cumulative sum/n ~30k false FAIL).
- Softened A27 unified-pool admit clamp; CapDirtyAdmit thrash wins over FD raise.
- MarkRelit hinterland admit-deny ∉ DirtyDropped.
- Bounded DropRemesh / MaybeDrop / DropFarFM per call; skip FM drop under holes.
- Heap fix: BindCommittedSlot free after map erase (no iterator UAF).

## Non-goals kept

No force_stale / RemoveChunk pending FD / shell-light mirror / heal expand.

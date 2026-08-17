# Cruise column generation diagnosis (`195810`)

Inland corridor −485/50→55 after Era28 publication rollback
(`5edc709c`: RelightThenMesh owns miss+pending, no Unlit-near).

This is **measurement**, not a bypass list. Do not “fix” holes with Unlit
near, hide exceptions, Imm, or `*FloorMs`.

Compare logs:

- `bin/logs/perf_20260817-163559_2464.jsonl` (pre hole-fill)
- `bin/logs/perf_20260817-183918_13392.jsonl` (Unlit-near + prune keep_h=2)
- `bin/logs/perf_20260817-195810_30216.jsonl` (this SoT)

## SLA vs `streaming_cruise_sot.md`

| Metric | Target | 163559 | 183918 | **195810** |
|--------|--------|--------|--------|------------|
| wall med / p90 | ≤130 / 220 | 208 / 407 | 215 / 294 | **204 / 279** |
| miss frame% | ≤47 | 87.5 | 97.3 | **88.2** |
| visible_black med | ≤25 | 58 | 32 | **40** |
| opaque_draw med | not collapse | 882 | 472 | **543** |
| late cz≥55 opaque | ≥200 | 101 | 105 | **108** |
| schedule_ok med | ≥8 | 5 | 8 | **8** |
| dirty_fm med | — | 192 | 39 | **204** |
| pending_light med | — | 30 | 2 | **6** |
| skip med | — | 69 | 5 | **48** |

Publication SoT is honest again (skip↑ vs 183918). Throughput did not
clear the witness column. Late opaque still collapsed.

Corridor `195810`: focus (−485,50)→(−485,55), 153 cruise frames, miss 135.

## Two-phase bottleneck

### A. Relight does not finish the witness (primary)

On `195810`, **120 / 135 miss frames still have `pending_light_n>0`**.

DesiredStage correctly chooses `RelightThenMesh`; SoftDefer correctly
skips FirstMesh (`skip` med 48). The hole is therefore **waiting on
light**, not on a missing ticket.

Sticky witness at start is **(−487, 48)** (nh=2, cy=0) for 24 frames,
then hops (`softdefer_witness_retarget` 0→192). Miss horiz: nh=2×58,
nh=3×40, nh=1×16, nh=4×16, nh=5×5. Almost all cy=0 (111/135).

FIFO never empties:

- `relight_fifo_n` med **58**, min **42**, max **78** (whole cruise)
- `relight_completed_n` == 0 on **117 / 153** frames (med 0, p90 1, max 4)
- `relight_fifo_dropped` latches **0 → 24** and stays
- `relight_drain_ms` med **30** on miss (capture runs; Completed ring
  stays empty — partial Y-band / requeue, not “drain idle”)

`PromoteRelight` on the hole column is armed; the FIFO still does not
finalize that witness. Light debt on nh=1–3 is the rate limiter.

### B. After pending dips, FirstMesh cannot catch the rim

Per-cz (195810):

| cz | miss | pl med | fm med | skip med | opaque med | packed med | pool MB |
|----|------|--------|--------|----------|------------|------------|---------|
| 50 | 59/61 | 23 | 228 | 22 | 739 | 150 | 12.5 |
| 51 | 10/10 | 2 | 291 | 23 | 537 | 77 | 10.3 |
| 52 | 15/15 | 1 | 250 | 67 | 634 | 88 | 12.1 |
| 53 | 11/11 | **0** | 218 | **92** | 707 | 103 | 13.7 |
| 54 | **0/7** | 1 | 220 | 98 | 746 | 130 | 14.4 |
| 55 | 40/49 | 7 | 115 | 58 | **108** | **29** | **1.95** |

At cz=53 light debt is gone (`pl med=0`) but miss is 100% at **nh=4–5**,
`dirty_fm` ~220, `schedule_ok` **8**. Eight FirstMesh slots cannot drain
a 200-deep FM queue in one focus step. Hinterland prune keep=ring is
why `dirty_fm` is high again (correct overflow vs 183918 keep_h=2).

45 miss frames with `pending_light_n≤2` still have fm med **252**.

## Opaque / underfeet (late cz≥55)

- Start: `opaque_draw` 923, packed 258, pool **13.8 MB**
- Stop: opaque **112**, packed **31**, pool **~2.0 MB**
- `underfeet_reason=7` (`NotReadyState`): **all 7** cz=54 frames (miss
  already 0, opaque still 746) **and all 49** cz=55 frames. GpuInFlight
  (5) only **2** whole cruise (183918 had 87× reason 5)

cz=54 is the tell: focus miss cleared, underfeet already NotReady, then
cz=55 residency collapses. 163559 late packed also ~34, but **pool stayed
~13.7 MB**; 195810 actually frees the pool (reconcile `ae0c75b0` without
a replacement bind).

NotLoaded(6) is gone; NotReadyState means emerge/visual ready, not
“chunk missing”. `heal_deferred_for_miss=1` on 151/153 frames.

## Witness / SoftDefer telem

- `softdefer_witness_retarget` 0 → 192 (witness hops while miss sticky)
- `softdefer_capture_floor_hits` 0 → 182
- `softdefer_empty_placeholder_n` med **0** (183918 was 3 — hole-fill
  empty storm is gone)
- `pending_gpu_queued_n` med **25** (end 8); `mesh_dirty_gpu_n` med **0**
- `gpu_kick_n` / `gpu_finish_n` med **1**

GPU mesh apply is not the late underfeet story; Relight + FM queue are.

## Root-cause statement

1. **Relight FIFO throughput** (completed=0, fifo floor ~40, drops) so
   PendingLight never leaves the miss column → FirstMesh must wait
   (Era28, by design).
2. **FirstMesh admit 8 vs FM Dirty ~200** after light dips → rim nh 3–5
   stays missing for the rest of the +Z step.
3. **GPU packed residency** shrinks an order of magnitude over Δcz=5 →
   late opaque ~100 (draw SoT), independent of Unlit-near.

Not a publication-fork bug. Not “SoftDefer forgot to mesh.” Light-before-
visible-mesh is working; **column RelightThenMesh execution and GPU
residency** are the industrial gaps.

## Next (architecture only)

Do **not**: Unlit near, hole_fill_preview, prune keep_h=2, Imm, hitch floors.

Do measure / change Relight FIFO complete path (why `relight_completed_n=0`
with fifo≥42), witness pin so PromoteRelight actually finalizes nh≤2,
and GpuPacked keep-until-bind on cruise (existing residency SoT,
`ae0c75b0`). Re-run inland −485/50 with the same audit keys.

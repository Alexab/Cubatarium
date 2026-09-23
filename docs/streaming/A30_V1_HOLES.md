# A30 V1 — near_focus_holes → 0

Date: 2026-09-22  
AF logs: `bin/_a30_v1_warm_af4.log` … `af6.log`

## Result

| Gate | Best this cycle | Notes |
|---|---|---|
| `dirty_dropped/period` delta median | **PASS** ≤800 | af6=432; thrash stays CLOSED |
| `near_focus_holes` periods>0 | **OPEN** (A29 best **2**) | af4=7, af5=13, af6=6 — V1 probes **regressed** |
| A24 safety overall | **FAIL** (holes) | no heal-loop |

## Attempts (reverted)

1. SoftDefer Owned-without-stuck / r≤1 owned scan — hang/regress.
2. PreferKick+PromoteRelight on nh≤1 + age 20–30 — holes 2→6–13.
3. Snapshot floor after phase clamp + late re-assert + CaptureRefresh×8 — holes→6–13.

Kept from plan gap only as **documentation**: sticky nh≤1 + SoftDefer clear + schedule_ok often 0; pending_light_focus~20–35; skip_snapshot spikes without SoftDefer skip.

## Non-goals kept

No heal-loop / force_stale / RemoveChunk pending FD / shell-light mirror / Ring ON.

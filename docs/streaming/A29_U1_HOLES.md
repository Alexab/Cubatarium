# A29 U1 — near_focus_holes → 0

Date: 2026-09-22  
Latest AF: `bin/_a29_u1_warm_af7.log` (also af2–af6)

## Result

| Gate | Best | af7 |
|---|---|---|
| `dirty_dropped/period` delta median | **PASS** ≤800 | 480 |
| `near_focus_holes` periods>0 | **2** (af3–af5) | **4** FAIL |
| A24 safety overall | **FAIL** (holes) | FAIL |

## Root causes fixed (partial)

1. **FM reserve collapse** — queue-sized reserve no longer shrinks effective FM cap to ~4.
2. **EmptyBacklog drip** — large clnm/dirty_fm bursts FM 12–16 instead of clamp-to-4.
3. **AbortDrip crush** — restore burst after abort reinforce.
4. **Remesh under miss** — remesh_cap=0 / no remesh snapshot reserve when focus missing.
5. **SoftDefer owned storm** — higher escape budget, earlier invalidate, PreferKick×8, pin nearest miss.

## Non-goals kept

No heal-loop / force_stale / RemoveChunk pending FD / shell-light mirror / Ring ON.

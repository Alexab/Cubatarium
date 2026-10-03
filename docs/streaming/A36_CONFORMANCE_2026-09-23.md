# A36 conformance — A31 Gate matrix after S0–S7 workstream

Date: 2026-09-23  
Parents: [A34_CONFORMANCE](A34_CONFORMANCE_2026-09-23.md), [A31_REAUDIT](A31_REAUDIT_2026-09-23.md)

## Gate matrix

| Gate | Status | Notes |
|---|---|---|
| 1 / P0 | PARTIAL | clean-tree bind + A36_S0 baseline; far UNTESTED until clean visible far AF |
| 2–3 / P1 | PARTIAL | NoteCullDecision + ClassifyChunkDefect + A36_S1 samples; freeze pixel UNTESTED |
| 4 / P3 | PARTIAL | production `SeamCoverageFullySatisfied`; unit negatives; AF seam OPEN |
| 5 / P4 | PARTIAL | Y-slice ContentRev; defer last-good without re-scan; no sync fallthrough; worker join |
| 6 / P2.2 | PARTIAL | pre-pub before mutate (CPU/GPU); A34 dark carve-out; A35 stamps; fault unit |
| 7 / P2.1 | PARTIAL | StopConverged + face monotonicity tests; AF stop often false |
| 8 / P6 | OPEN | holes/eye/post_stop not closed; merge_green=false |
| 9 / P7 | OFF | Ring stays OFF |

## A30 V3 retirement

Shared validate path retained; dual-system retirement still PARTIAL until Gate 8. See [A35_R3](A35_R3_ARTIFACT_RETIREMENT_2026-09-23.md).

## Stop-lines

Ring OFF; no SoftDefer/PreferKick heal; dirty AF rejected unless `CUBA_ALLOW_DIRTY_AF=1`.

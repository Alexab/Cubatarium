# F5 after RelightReplaceOwner (`c3010053`)

| Run | Perf | early_vb_med | early_vb_max | vb_med | focus | pass |
|---|---|---|---|---|---|---|
| cold | `095434` | **109** | 117 | 25 | (7,3)→(−3,3) | false |
| warm | `095703` | **92** | 117 | 14 | (7,3)→(−8,3) | false |

Vs E0d proxy baseline early VB ~115: no catastrophic regress; product early-west FAIL class unchanged. **Not** G1 product CLOSED.

Reports: `f5_cold.json`, `f5_warm.json`, `f5_summary.json`.

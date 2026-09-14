# Autofly vs manual west corridor (E0b)

Plan: `g1_a10_continuation` E0b. Tip at study: `d80ec866` / docs tip after E0.

## Inputs

| Role | Log |
|---|---|
| North autofly P5 | `bin/logs/perf_20260913-230002_40748.jsonl` |
| West manual FAIL | `bin/logs/perf_20260914-080455_21652.jsonl` |
| Enter (manual) | `bin/logs/enter_lit_20260914-080523.jsonl` |
| Product SoT | 141350 / 174657-class / product_anchor 192015 (west) |
| Driver default | `tools/flight_sim_run.py --replay-manual` → **yaw 90** |

## Diff matrix

| Field | Autofly 230002 | Manual 080455 | Rank |
|---|---|---|---|
| focus path | `(7,3)→(7,37)` (+Z north) | `(7,3)→(−3,3)` (−X west) | **#1 root** |
| periods | 162 | 56 | #3 duration |
| VB med | 23 | **75** | consequence |
| focus_missing_mesh med | 0 | **1** | consequence |
| miss_stuck med | 0 | **263** | consequence |
| fog_pull_in_rd med | **1** (5→1→0) | **4** (stays ~4) | **#2** stress |
| movement_speed med | ~6.0 | ~5.1 | weak |
| schedule_ok / ok_remesh med | 1 / 1 | 3 / 2 | progress live both |
| wall_ms med | ~27 | ~90 | secondary |
| teleport | no | no | not a diff |
| hold-space / pitch | forced 0 + hold-space | manual camera | #4 |
| HUD / window | hidden flight-sim | visible + HUD | #5 secondary |

## Verdict

**Root cause #1 (historical P5): yaw/corridor mismatch.** North `--replay-manual` yaw 90 under-stresses streaming. West manual keeps higher RD pressure and shows VB/missing FAIL class matching 174657 / 141350 product gates.

**Root cause #1b (proxy_v2 west, 2026-09-14):** even with yaw **180**, `product-174657` inherited `replay_manual` **`hold_space=True`**. Autofly eye climbs **Y ~76→300** while manual **122212/100645** stays **~50→58**; `fog_rd` collapses (~2 vs 4), `focus_missing`/`miss_stuck` stay **0**. Ocean-cruise already documented HoldSpace blindness — west product proxy needed the same fix.

**Adequacy fix:** scenario `product-174657` = resume World_164, no-teleport, **yaw 180**, west focus, **`hold_space=False`**, `min-alt-above-sea=0`, eye Y pinned ~56, altitude gate in `proxy_adequacy`. Legacy `--replay-manual` yaw 90 remains north smoke only (may still hold-space).

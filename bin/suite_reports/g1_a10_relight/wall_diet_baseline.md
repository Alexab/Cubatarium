# Wall-diet track baseline (A0 / D0)

| Field | Value |
|---|---|
| Audit HEAD | `420335e9` |
| Visual SoT | manual `154921` / enter `154948` |
| Corridor | `(7,3)→(−3,3)` west |
| Operator | PASS (no edits); product G1 vs 141350 **OPEN** |
| Wall med (period) | ~71 ms |
| `prep_schedule_policy_ms` med (audit filter) | ~21.7 ms |
| Audit filter | `2 < movement_speed < 20`, `-3 <= focus_cx <= 6`, `player_y >= 54` |
| World | `World_164` |
| Autofly gate | `--scenario product-174657` (no-teleport, yaw 180) |
| GPU class | AMD Radeon Graphics iGPU (154921) |

## Track targets (after A5; comparative)

- `prep_schedule_policy_ms` med ≤ 10
- `mesh_emerge_ms` ≤ 16
- `wall_ms` med ≤ 50 (same audit filter)

## Dual-lane regress class (autofly green)

- StaleVL ≤ 84.5, VB ≤ 84.5
- unlit max ≤ 15 cold / ≤ 19 warm
- dirty_remesh ≤ 60
- Better than P3 cold `133817` (StaleVL 94.5)
- Adequacy proxy PASS; not G1 CLOSED

Exe SHA256 from audit (disk identity, not auto-linked to 154921 run):
`7fae041bbef8934b809bc2f7a307f80270b91f28c9f5c438ad78a82c80776a7d`

# A21 replay pose / world-copy fixtures (P0.6)

Do **not** modify the operator working world. Use independent copies for A/B.

## Worlds

| Label | Source | Copy instruction |
|---|---|---|
| HEAD working | `World_164` | leave untouched |
| HEAD fixture | copy of World_164 → `World_164_a21_head` | copy before HEAD flights |
| Anchor 27beca1c | same seed/save snapshot | `World_164_a21_27beca1c` |
| Anchor dd7871ab | same seed/save snapshot | `World_164_a21_dd7871ab` |

Cold and warm are separate scenarios (`--warmup-sec 0` vs `20`), not different worlds.

## Product-174657 west cruise poses (canonical)

Pin locus ≈ `(120, 56, 56)` world units; yaw **180** (west); `hold_space=false`;
`cruise-eye-y=56`; `min-alt-above-sea=0`.

| Phase | Duration (s) | Intent |
|---|---:|---|
| idle | 15 | settle |
| fly west | 55 | cruise coverage |
| stop | 20 | convergence |
| dive (dive scenario) | 12 @ −30° pitch | underwater |

## Edit / load events (for later visual gates)

Recorded as abstract sequence (implement as flight flags / debug cmds when available):

1. `load_world` World_164 fixture cold
2. `idle` 15s
3. `fly_west` 55s
4. `yaw_180_turn` at remaining budget ≈ 0 (P1 cull gate)
5. `stop` 20s
6. Optional: `edit opaque↔transparent`, `water↔lava`, `unload_reload_neighbor`

## Route hash

`tools/a21_run_manifest.py` hashes `scenario:world:cold_warm` into `route_hash`.
Historical logs without `run_manifest` are **not** equivalent to new runs.

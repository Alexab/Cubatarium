# A41 — Visual Obligation SoT (audit since dcc02e27)

Date: 2026-09-24  
**Детальный план (код):** [.cursor/plans/a41_visual_sot_root_cause.plan.md](../../.cursor/plans/a41_visual_sot_root_cause.plan.md)  
Audit range: `dcc02e27` … `4d1155df` (+ A40 WIP)

## One-line verdict

Чёрные чанки не закрываются системно: **шесть независимых hide/ready слоёв** + D1 `open_sky` equal-rev FD без Dirty-owner. A40 рвёт PL↔FD deadlock, но рисует open_sky wrong bake чёрным без remesh.

## Code facts (HEAD+A40 WT)

- Ready writers: Emerge LitDrawable **и** `ClearPending…→RenderReady` (`World.cpp` ~4414).
- `still_stale` = только rev ahead; FD census ≠ stale (`MeshLightStalePolicy.h`).
- MarkRelit: `!open_sky`→LegalDark; `open_sky`→clear PL, **no Dirty** (D1).
- SoftDefer Hold без ticket; RelightOnly ad-hoc Enqueue без MarkDirty.

## Next

P0 ADR → P1 enum → P2 sole Ready → P3 LightRepair Dirty once → P4 SoftDefer⇒ticket → P5 demand → P6 Gate8 scale=1.

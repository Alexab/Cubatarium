# Item visual QA (tools_visual)

Date: 2026-11-13. Actual triangle/UV rasterization; native FP/TP acceptance is separate.

- Passed: **True**
- Failures: 0
- Warnings: 0
- Items audited: 70

## Failures

- none

## Warnings

- none

## Family sheets

- `pickaxe`: [docs/research/item-previews-v2/family_pickaxe_sheet.png](family_pickaxe_sheet.png)
- `axe`: [docs/research/item-previews-v2/family_axe_sheet.png](family_axe_sheet.png)
- `shovel`: [docs/research/item-previews-v2/family_shovel_sheet.png](family_shovel_sheet.png)
- `hoe`: [docs/research/item-previews-v2/family_hoe_sheet.png](family_hoe_sheet.png)
- `sword`: [docs/research/item-previews-v2/family_sword_sheet.png](family_sword_sheet.png)

## Acceptance notes

- Silhouette column must identify the family without color.
- Gray column must keep wood/stone/metal tiers separable without hue.
- Every published item must ship `textures/diffuse.png` (no factor-only).

Per-item sheets: `docs/research/item-previews-v2/<id>_contact.png`.

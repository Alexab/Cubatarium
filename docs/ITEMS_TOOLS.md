# Items & Tools

Data-driven tools for player / bot / mobs (shared `UCreature` inventory + attributes).

## Content

- Definitions: [`content/items/*.json`](../content/items/)
- Catalog groups: `itemTypes` in [`content/types.json`](../content/types.json) (`mining`, `cutting`, `digging`, `combat`, `utility`, `armor`, `misc`)
- Palette tab: **Tools** (5th tab). Key **B** opens **Blocks**, not Tools — use inventory key / last tab or switch to Tools manually.

### Inventory, equipment and controls

`E` opens the common player backpack, `C` equipment, `B` the creative catalog,
`K` the station-aware recipe book. The player window also includes Status and
Appearance. Inventory has 40 real slots in four rows; the number of visible
HUD panels does not change capacity. The selected slot is the active hand.
Four armor positions and the offhand own moved instances, not copies.

Creative and Survival keep separate loadouts. Creative catalog creation uses
an authorized operation; it does not create Survival property. Survival displays
owned stacks and instances with their actual wear and contents.

- Left click: take/place/merge/swap. Right: half-stack/place one.
- Shift-click: quick transfer/equip. Double click: collect matching stacks.
- Ctrl-click: pin. Hover plus `1–0`: exchange with the active row.
- Search highlights in place. Explicit Sort preserves pinned/equipped positions.
- Cancel/close preserves the carried stack. Recovery allows withdrawal only.
- Armor drag onto slots or the character is retained. Cosmetic outfit is independent.

`V` grabs/places eligible physical cargo, hold/release `R` winds up/throws,
Escape cancels windup. One unit is detached from a stack. Creature identity,
health, effects and possessions survive carry/flight/save. Mass and dimensions
limit one/two-handed carry and speed; an occupied shield does not block.
These actions and the recipe book key are configurable.

Full mechanics and acceptance: [completion report](research/items-completion-2026-11-13/README.md).

### Item JSON (summary)

| Field | Meaning |
|-------|---------|
| `id`, `displayName`, `types[]` | Catalog |
| `stack_max` | Tools = 1 |
| `wear_end` | `destroy` (default) / `broken` / `indestructible` |
| `repair.materials`, `repair.amount` | Repair restore fraction |
| `tool.groupcaps` | Dig capabilities (cracky/choppy/…) — domain A dig |
| `tool.damage` / `damage.melee` | Combat damage groups (e.g. `fleshy`) — domain A hit; scalar `melee` maps to fleshy |
| `tool.full_punch_interval` | Melee cooldown |
| `tool.punch_attack_uses` | Wear denominator for melee hits |
| `visual.*` | FP/TP wield scale, offset, euler, swing/use preset ids |
| `ranged.*` | Charged physical bow: enabled, ammo_id, damage profile |
| `physical.*` | Conditional mass, collision dimensions, contact hardness |
| `visual.presentations.*` | Independent icon/FP/TP/ground scale |
| `block.*` | Shield: enabled, damage_mul, angle, passive mul |

Hand is a built-in hidden def (`hand`) with weak `oddly_breakable_by_hand` / `crumbly` / weak `choppy` caps for the wood bootstrap.

### Critical vs hideable

**Critical** (must stay in catalog even without textures; must not set `hidden`):

- Built-in `hand`
- Tools: `wood/stone/copper/iron/gold_{pickaxe,axe,shovel,hoe,sword}`
- `wood_bow`, `arrow`, `wood_shield`, `iron_shield`, `wood_torch`
- Armor (4 slots): `leather/copper/iron/gold_{head,chest,legs,feet}`
- Food / thirst: `apple`, `bread`, `raw_meat`, `leather`, `waterskin`

Duplicate imported roles were removed in tools_visual. The original `wood_spear`,
two shields and utilities remain; the catalog now has 70 physical item definitions.

Audit: `python tools/audit_item_visuals.py` → `bin/iter_reports/item_visual_audit.json`.
Validate: `python tools/validate_item_defs.py`.

See [INTERACTION_ARCHITECTURE.md](INTERACTION_ARCHITECTURE.md) (decisions A + C) and
[SURVIVAL_INTEGRATION.md](SURVIVAL_INTEGRATION.md) (`WorldDifficulty`).

## Dig contract

`ResolveDigParams(tool, block, attrs, mode)` in `src/Items/ToolCapabilities.*`.
Hit math: `ResolveHitParams` (same module). Dig channel runs through Influence bus (`Channel::Dig`, shipped).

Blocks should declare:

```json
"dig": { "level": 1, "groups": { "cracky": 2 } }
```

If `DigGroups` is empty, tools **infer** groups from `Types` / name / hardness (fallback until packs filled).

Hardness gates unbreakable (`hardness <= 0` in Survival). On groupcap match, duration uses
`times[rating]` (hardness does not further scale). Else `BlockDigRules` baseline `hardness * 1.5`.

## Wear gate

`IsToolWearEnabled(mode, difficulty)` / ModePolicy:

- **Creative** → no wear
- **Survival + Peaceful** (easiest) → no wear
- else → wear on dig complete **and** melee hit; `wear_end` decides destroy vs broken
## Commands

- `/give <item_id>` — assigns tool to active hotbar slot when id is an item
- `/repair` — repairs active hotbar item using its first repair material

## Visuals

- Inventory / hotbar icons: `UItemIconCache` via `UItemPreviewRenderer` (glTF → parts[] → FallbackParts)
- Hotbar: wear bar + broken dim
- FP viewmodel: clear-Z mesh arms (`TryDrawSkinnedArms`) + held Item/Block (`ShowFpWield`, Perspective only). Held items prefer glTF (`TryDrawGltfHeld`), else parts. Scale = AABB fit × `visual.wield_scale` (category defaults via `ItemVisualDefaults` / `tools/apply_item_visual_defaults.py`). Target tool length ≫ FP block cube (~0.22).
- Worn armor: `WornEquipmentDrawer` on `bone_skeleton`. TP tool/offhand on `rightItem`/`leftItem` (hidden for possessed player when FP viewmodel is on). Character sheet paper-doll draws the same wield attach.
- Swing/use presets: [`content/item_visual_presets.json`](../content/item_visual_presets.json)
- Models: [`ITEM_ASSETS.md`](ITEM_ASSETS.md)

## Shield / bow controls

- **Bow** (`wood_bow`): hold RMB to draw, release to launch a physical arrow. Ammo and bow wear commit together; failed launch spends nothing. Swept collision stops arrows at walls/creatures.
- **Shield** (offhand): passive `armor.armor_groups` + `block.passive_damage_mul`; hold **RMB** to block (`block.damage_mul`, front arc); blocked hits wear offhand via `block.block_uses` (else `punch_attack_uses`). FP plays `raise_shield`. Carrying cargo or drawing a bow suppresses shield use.

## Influence handshake

`UItemToolInfluenceProvider` implements `IUToolInfluenceProvider` so held tools feed melee/ranged
(and Dig channel uses the same item `groupcaps` via `ResolveDigParams`). One Influence bus;
two group matchers (A).

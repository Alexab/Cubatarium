# Items & Tools

Data-driven tools for player / bot / mobs (shared `UCreature` inventory + attributes).

## Content

- Definitions: [`content/items/*.json`](../content/items/)
- Catalog groups: `itemTypes` in [`content/types.json`](../content/types.json) (`mining`, `cutting`, `digging`, `combat`, `utility`, `armor`, `misc`)
- Palette tab: **Tools** (5th tab). Key **B** opens **Blocks**, not Tools — use inventory key / last tab or switch to Tools manually.

### How to equip (Creative)

There is **no separate “equip to hand” action**. The **active hotbar slot** *is* the right hand (FP viewmodel / dig / place). Character sheet **Main** mirrors that slot.

1. Open palette → **Tools** (leave the bottom hotbar visible — drag target).
2. Pick a sub-tab (Mining / Utility / Armor / …).
3. **Click** an item to assign it to the **currently selected hotbar slot**, or **drag** onto a bottom hotbar slot (or onto sheet **Main**).
4. Hotbar keys `1–9` / `0` (or wheel in Perspective) select the active tool/block.
5. **Armor**: open character sheet (**C**), drag armor from Tools → Armor onto paper-doll slots (Head/Chest/…).
6. **Offhand**: on the character sheet, drag an Item or Block onto **Offhand** — left FP hand.

Or: `/give <item_id>` into the active hotbar slot.

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

Hand is a built-in hidden def (`hand`) with weak `oddly_breakable_by_hand` / `crumbly` caps.

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

- Inventory / hotbar icons: `UItemIconCache` via `UItemPreviewRenderer` (parts[] / optional glTF)
- Hotbar: wear bar + broken dim
- FP viewmodel: clear-Z dual box arms + held Item/Block (`ShowFpWield`, Perspective only; isometric hides hands). Offhand on sheet. Skinned = TD-ITEM-004.
- Models: [`ITEM_ASSETS.md`](ITEM_ASSETS.md)

## Influence handshake

`UItemToolInfluenceProvider` implements `IUToolInfluenceProvider` so held tools feed melee
(and Dig channel uses the same item `groupcaps` via `ResolveDigParams`). One Influence bus;
two group matchers (A).

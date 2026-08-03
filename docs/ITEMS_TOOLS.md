# Items & Tools

Data-driven tools for player / bot / mobs (shared `UCreature` inventory + attributes).

## Content

- Definitions: [`content/items/*.json`](../content/items/)
- Catalog groups: `itemTypes` in [`content/types.json`](../content/types.json)
- Palette tab: **Tools**

### Item JSON (summary)

| Field | Meaning |
|-------|---------|
| `id`, `displayName`, `types[]` | Catalog |
| `stack_max` | Tools = 1 |
| `wear_end` | `destroy` (default) / `broken` / `indestructible` |
| `repair.materials`, `repair.amount` | Repair restore fraction |
| `tool.groupcaps` | Luanti-style dig capabilities |
| `tool.damage.melee` | Influence / combat damage |

Hand is a built-in hidden def (`hand`) with weak `oddly_breakable_by_hand` / `crumbly` caps.

## Dig contract (block agent)

`ResolveDigParams(tool, block, attrs, mode)` in `src/Items/ToolCapabilities.*`.

Blocks may declare:

```json
"dig": { "level": 1, "groups": { "cracky": 2 } }
```

If `DigGroups` is empty, tools **infer** groups from `Types` / name / hardness (temporary; prefer explicit dig groups).

Hardness still gates unbreakable (`hardness <= 0` in Survival). Duration uses groupcaps when matched, else `BlockDigRules` baseline.

## Wear gate

`IsToolWearEnabled(mode, difficulty)`:

- **Creative** → no wear
- **Survival + Peaceful** (easiest) → no wear
- else → wear applies; `wear_end` decides destroy vs broken

## Commands

- `/give <item_id>` — assigns tool to active hotbar slot when id is an item
- `/repair` — repairs active hotbar item using its first repair material

## Visuals

- Inventory: generated icon via `UItemIconCache` (CPU silhouette)
- Hotbar: wear bar + broken dim
- FP wield: `UItemWieldRenderer` API (see tech debt for full mesh path)

## Influence handshake

`UItemToolInfluenceProvider` implements `IUToolInfluenceProvider` so held tools feed melee capability (parallel influence system).

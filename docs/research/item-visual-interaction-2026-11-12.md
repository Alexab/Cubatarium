# Cubatarium Items: Familiar Actions, Distinct Visuals, One Pipeline

## Executive Summary

- **Share Within Families**: Build one recognizable shape language for each core tool family, then use material variants and selective geometry changes. Hytale's guidance prioritizes silhouette over extra geometry. [9]
- **Separate Five Views**: Use one canonical asset with inventory, first-person, third-person, dropped, and display presets. Minecraft explicitly supports context-specific transforms. [6]
- **Modernize The Baseline**: Minecraft now includes seven material tiers; copper tools and tiered spears belong in the familiarity reference, though neither requires immediate feature parity. [4][5][14]
- **Compose Behavior**: Follow Bedrock components and Vintage Story's JSON properties: separate item identity, visuals, statistics, and reusable actions. [1][8]
- **Protect Readability**: Hytale uses higher texture density for attachments seen close to the camera; reserve detail for heads, edges, grips, and functional parts. [9]
- **Avoid Color-Only Tiers**: Add shape, markings, names, and UI badges. Xbox accessibility guidance explicitly rejects color as the sole information channel. [11]

## 1. Share Geometry Until The Material Changes The Shape

**Hytale case study.** Hytale builds models from cubes and quads and advises increasing geometry only when it improves the silhouette. Its attachment assets use **64 pixels per unit**, versus **32** for props and blocks, because hands and equipment receive close first-person scrutiny. These are Hytale conventions, not mandatory Cubatarium specifications. [9]

The useful lesson is to spend geometry on recognition and texture detail on material identity. For Cubatarium, retain the functional landmarks: transverse pick head, broad axe blade, shovel scoop, perpendicular hoe blade, and sword blade/guard.

| Tier group | Recommended geometry policy | Distinguishing treatment |
|---|---|---|
| Wood / primitive stone | Separate primitive variants | Lashed heads, chipped stone, carved wooden edges |
| Copper / iron / gold analogs | Share forged-family mesh where plausible | Palette, roughness, edge treatment, stamped markings |
| Gem / late-game analogs | Selective head or guard replacement | Faceting, reinforcement, distinctive end-cap |

Do not create seven unrelated silhouettes. Conversely, recoloring a polished metal axe brown will not convincingly produce a wooden tool. Keep grip position, striking edge, and overall handling envelope stable across progression.

For visual and legal separation, create original geometry, textures, names, ornaments, and icon compositions; avoid tracing Minecraft sprites or reproducing their exact pixel contours. Maintain asset-pack license records and derivative-work permissions. **There is no percentage-change rule that guarantees legal clearance.**

**Starting budgets, proposed for Cubatarium rather than measured from these games:** ordinary rigid tools **150-600 triangles**; bows, shields, and articulated utilities **400-1,500**. Use **32x32 or 64x64** textures for simple tools, up to **128x128** for larger equipment. Keep a consistent texel density; profile before increasing these limits.

**Decision:** use shared handles, interchangeable heads, material variants, and a small number of silhouette upgrades.

## 2. One Asset, Five Explicit Rendering Contracts

**Minecraft case study.** Java separates item appearance definitions from raw model geometry. Since **1.21.4**, client item definitions live under `assets/<namespace>/items/`, referencing models rather than relying on hardcoded item-model paths. [7] Its rendering contexts include first/third-person hands, GUI, ground, and fixed display. [6]

Bedrock reaches a similar outcome through attachables bound to equipment slots, with separate first- and third-person animations. [2] Cubatarium should adopt this separation without implementing either file format wholesale.

| Representation | Transform and scale contract | Animation/state needs |
|---|---|---|
| Inventory icon | Orthographic three-quarter camera; fit visual bounds within a padded square | Usually static; explicit filled/loaded variants |
| First-person | Camera-relative grip offset, independent presentation FOV, per-family rotation/scale | Equip, idle, mine, attack, use, charge, release, block |
| Third-person | Hand-bone socket plus authored grip transform; world-size mesh | Locomotion-compatible upper-body actions; offhand poses |
| Dropped | Ground pivot, collision proxy, world scale | Optional bob/spin, or resting physics |
| Frame/display | Fixed mounting pivot and depth offset; authored display scale | Static by default; optional inspection rotation |

Normalize source units once: for example, meters with grip origin at the handle's grasp point. Store ground and mounting pivots separately. Start dropped and third-person assets at **1.0 world scale**; override only for deliberate readability.

Do not independently resize every icon, held item, and drop until they merely "look right." Save context presets and review them together. Mirrored left-hand presets need explicit checks for asymmetric blades, writing, and bow mechanisms.

**Decision:** share geometry by default; create a separate first-person mesh only when camera proximity or articulation genuinely demands it.

## 3. Make Interaction A Composition Of Actions

Minecraft's tier system separates mining level, durability, efficiency, and damage bonus; the correct tool category and sufficient mining level are distinct concerns. [4] Cubatarium should preserve that distinction rather than giving every higher-tier item one universal multiplier.

| Data component | Implementable fields and behavior |
|---|---|
| Equipment | Main/offhand eligibility, slot, grip socket, support-hand socket, hand-occupancy policy |
| Mining | Block-tag rules, speed multiplier, harvest level, effectiveness feedback |
| Durability | Maximum, mining/hit/use costs, repair ingredient, break event |
| Combat | Damage/type, reach, cooldown, hit window, knockback, animation profile |
| Primary action | Mine, melee, cast, or other explicit action |
| Secondary action | Ordered contextual rules: target predicate, action, cost, resulting state |
| Presentation | Animation profile, sound set, contact socket, particle profile |

Bedrock separately exposes use animation, swing duration, durability, and equipment-related components. That separation is important: **animation duration is not automatically attack cooldown or damage timing.** [1]

Recommended familiar secondary actions:

| Family | Secondary/use behavior |
|---|---|
| Placeable item | Validate target, place, decrement stack |
| Hoe / shovel / axe | Till soil / create path / strip eligible wood |
| Flint-and-steel / shears | Ignite eligible target / shear eligible entity or foliage |
| Bucket | Collect or pour; replace contents state/item |
| Bow / crossbow | Charge-release / load-then-fire |
| Fishing rod | Cast, maintain line state, reel |
| Shield / food | Hold block / consume after use duration |

Resolve interactable blocks before item fallback, with a modifier to bypass interaction. Define successful-use costs explicitly; failed targeting should not accidentally consume durability or inventory.

For particles, distinguish **contact placement** from **debris appearance**: attach the emitter timing/origin to the action, but derive block chips from the struck block's material. Tool-specific sparks or trails are supplementary effects, not replacements for recognizable block feedback.

Use one readable, Java-like attack-recovery model rather than targeting full cross-edition combat parity. Preserve charge feedback and input buffering; defer elaborate combos and enchantment exceptions.

For two-handed items, define occupancy as gameplay data, not an inference from mesh size. A support-hand IK target controls the pose; separately decide whether charging suppresses an offhand shield or item.

**Decision:** keep mechanics authoritative and presentation replaceable; never let a pretty swing determine whether a block breaks.

## 4. Collapse Packs Into Jobs, Not Mesh Counts

Use the following as a **Minecraft-familiar scope map**, not a commitment to implement every system immediately:

| Scope | Baseline families | Cubatarium recommendation |
|---|---|---|
| Survival core | Pickaxe, axe, shovel, hoe, sword | One progression per family |
| Combat extensions | Bow, arrows, shield, crossbow; later spear, trident, mace | Add only with distinct combat behavior |
| World utilities | Bucket, flint-and-steel, shears, fishing rod | Model important states and moving parts |
| Consumables / placement | Food, drinks, seeds, torches, blocks | Shared consume/place profiles |
| Navigation / transport | Compass, clock, map, lead, boat, minecart, spyglass | Separate backlog tied to actual systems |

The modern reference includes copper equipment: Mojang describes copper tools as faster and longer-lasting than stone, while dealing the same damage. [5] Spears also have materially different timing and separate jab/charge attacks, so they are not merely long swords. [14]

Do not interpret mixed-pack names as separate gameplay classes. "Lumber axe," "iron axe," and "survival axe" become one family unless reach, target effectiveness, grip, or actions genuinely differ. Retain compatible alternatives as cosmetic skins; discard inconsistent leftovers.

**Core Keeper case study.** Its tools cover mining, digging, gardening, and fishing, with repair/reinforcement and an Upgrade Station that strengthens tools and weapons. [16][13] This supports a useful Cubatarium choice: progression can improve an existing item identity instead of requiring a new mesh at every step.

For a **Valheim-style alternative**, deliberately distinguish a woodcutting axe from a two-handed battleaxe only if the latter changes handling and combat. Treat that as an optional class-design direction, not permission to preserve every duplicate axe in the pack.

**Decision:** deduplicate by `(job, action set, handling)`, then assign material progression and skins.

## 5. JSON Definitions, Baked Icons, Reusable C++ Systems

**Vintage Story case study.** Its item JSON exposes separate GUI, first-person, third-person, and ground transforms. Shapes can reuse texture bindings across variants; bows can select alternative shapes for charge states. [8]

It also reserves custom classes for functionality beyond JSON. [8] Follow that boundary: data configures behavior, while reusable C++ components implement capabilities.

A proposed Cubatarium definition:

```json
{
  "id": "cubatarium:iron_pickaxe",
  "extends": "family:pickaxe",
  "material": "tier:iron",
  "visual": {
    "mesh": "tools/pick_forged",
    "materialSet": "iron",
    "contexts": "poses/pickaxe",
    "icon": "auto"
  },
  "mining": {
    "rules": [{"tag": "mineable/pickaxe", "speed": 6.0}],
    "harvestLevel": 2
  },
  "durability": {"max": 300},
  "combat": {"damage": 4.0, "cooldownSeconds": 0.8},
  "actions": {"primary": "mine_or_melee"}
}
```

**Numbers above are illustrative balance values, not Minecraft statistics.** Resolve inheritance at load time and validate references, tags, sockets, and action compatibility.

Keep immutable definitions separate from stack state: quantity, damage, contents, charge, and skin. Let the server validate targets/costs while clients predict animation and reconcile results.

For rendering, begin with a padded atlas per compatible material/shader group; use nearest filtering where appropriate and mip-safe padding. Prefer texture arrays when supported and suitable; reserve unique textures for exceptional large or translucent assets. An atlas alone does not batch incompatible shaders or draw states.

Bake icons offline from the actual mesh and material, using fixed camera, lighting, padding, and alpha. Cache by asset/material revision; allow authored overrides when a complex shape reads poorly. Godot documents the underlying viewport-to-texture mechanism. [12]

Use family animation curves for wind-up, contact, and recovery. Compose:

`handBone * gripTransform * actionTransform * modelTransform`

Modern Java selectors and CIT illustrate state-based visual replacement. CIT can replace textures/models and bow-pulling states; it is a cosmetic selection layer, not an item-behavior system. [7][10]

**Decision:** avoid switches on item names; register reusable action handlers and declarative visual selectors.

## 6. Synthesis: Familiarity Survives Through Consistent Handling

Minecraft emphasizes context-specific presentation; Hytale emphasizes silhouette and close-up texture readability; Vintage Story demonstrates configurable shapes and transforms; Core Keeper shows progression through tool improvement. Their useful commonality is **stable identity with replaceable presentation**, not identical rendering technology. [6][9][8][13]

Across mechanism, scope, and trade-off, choose shared geometry for maintainability, unique geometry for meaningful shape changes, and viewmodel specialization only for camera problems. A richer combat-class direction adds animation and balancing costs; cosmetic variants should not silently add mechanics.

| Failure | Preventive acceptance test |
|---|---|
| First-person wall/camera clipping | Test walls, corners, FOV extremes, equip and charged poses |
| Generic-stick appearance | Identify each family from a monochrome hotbar silhouette |
| Scale drift | Review all five contexts in one contact sheet |
| Colorblind tier ambiguity | Test grayscale plus material badges and geometry cues |
| Over-designed tools | Verify head orientation and action remain immediately recognizable |

Epic's first-person rendering supports separate FOV and camera-directed scaling to reduce intersections. [15] For Cubatarium, pair a dedicated viewmodel pass with wall-distance lowering; otherwise tools can appear to float through walls. Keep world-space hit tests authoritative.

**Recommended default pipeline:**
1. Audit licenses and collapse pack duplicates into gameplay families.
2. Author grip-centered meshes with shared forged-tier geometry and selective primitive/endgame variants.
3. Export mesh, sockets, materials, and five context presets.
4. Define family behavior, tier statistics, and per-stack state separately.
5. Bake GUI icons and validate tags, references, and required animations.
6. Attach third-person items to hand bones; drive first-person poses with shared curves and explicit action events.
7. Gate release on silhouette, grayscale, clipping, scale, and interaction tests.

## References

1. *Item Components Documentation - Item Components | Microsoft Learn*. https://learn.microsoft.com/en-us/minecraft/creator/reference/content/itemreference/examples/itemcomponentlist?view=minecraft-bedrock-stable
2. *Using Attachables | Microsoft Learn*. https://learn.microsoft.com/en-us/minecraft/creator/documents/attachables?view=minecraft-bedrock-stable
3. *Items model definition – Minecraft Wiki*. https://minecraft.wiki/w/Items_model_definition
4. *Tiers – Minecraft Wiki*. https://minecraft.wiki/w/Tiers
5. *Minecraft Java Edition 1.21.9 | Minecraft*. https://www.minecraft.net/en-us/article/minecraft-java-edition-1-21-9
6. *Item Models | Fabric Documentation*. https://docs.fabricmc.net/develop/items/item-models
7. *Minecraft Java Edition 1.21.4 | Minecraft*. https://www.minecraft.net/en-us/article/minecraft-java-edition-1-21-4
8. *Modding:Item Json Properties - Vintage Story Wiki*. https://wiki.vintagestory.at/Modding:Item_Json_Properties/en
9. *An Introduction to Making Models for Hytale | Hytale*. https://hytale.com/news/2025/12/an-introduction-to-making-models-for-hytale
10. *Custom Item Textures*. https://optifine.readthedocs.io/cit.html
11. *Xbox Accessibility Guideline 103 - Microsoft Game Dev | Microsoft Learn*. https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/103
12. *Using a Viewport as a texture*. https://docs.godotengine.org/en/4.0/tutorials/shaders/using_viewport_as_texture.html
13. *Upgrade Station - Core Keeper Wiki*. https://corekeeper.atma.gg/en/Upgrade_Station
14. *Minecraft 1.21.130 Bedrock Changelog | Minecraft*. https://www.minecraft.net/en-us/article/minecraft-1-21-130-bedrock-changelog
15. *First Person Rendering | Unreal Engine 5.6 Documentation | Epic Developer Community*. https://dev.epicgames.com/documentation/en-us/unreal-engine/first-person-rendering?application_version=5.6
16. *Tools - Core Keeper Wiki*. https://corekeeper.atma.gg/en/Tools
17. *Minecraft Usage Guidelines for Fans and Creators | Minecraft | Minecraft*. https://www.minecraft.net/en-us/usage-guidelines

# Armor construction and anatomical sections

Current catalogue status: all 70 v3 resources are published. Construction notes
and earlier acceptance lists below retain their checkpoint context. The current
[publication and acceptance record](../../docs/research/item-visual-redesign-2026-11-14/production-publication/README.md)
is authoritative; final integrated visual/performance acceptance is still open.


All sixteen armor IDs now have candidate sources: fourteen new recipes plus
the retained iron helmet and leather cuirass pilots. Seeding with
`python tools/itemgen.py seed-armor` creates missing sources only. Builds never
publish or rewrite gameplay definitions, author recipes or source overlays.

| Set | Construction | Sources |
|---|---|---|
| Leather | Sewn crown/ear panels, straps and open buckles, patched sleeves and laced boots. Base leather avoids stitches repeated on every facet; stitching belongs to actual straps/joins. | [head](leather_head/design.yaml), [chest](leather_chest/design.yaml), [legs](leather_legs/design.yaml), [feet](leather_feet/design.yaml) |
| Copper | Segmented crown and brow rivets, overlapping torso/leg lamellae, toe plate and fasteners. | [head](copper_head/design.yaml), [chest](copper_chest/design.yaml), [legs](copper_legs/design.yaml), [feet](copper_feet/design.yaml) |
| Iron | Crown ridge/cheek guards, faceted breastplates, shaped thigh/shin plates and knee guards, reinforced boot toe/ankle band. | [head](iron_head/design.yaml), [chest](iron_chest/design.yaml), [legs](iron_legs/design.yaml), [feet](iron_feet/design.yaml) |
| Gold | Thin crown band and inset ornament, chest chevron, slim leg plates with fine bands, small boot clasp/trim. Geometry changes as well as color; no new armor balance is introduced. | [head](gold_head/design.yaml), [chest](gold_chest/design.yaml), [legs](gold_legs/design.yaml), [feet](gold_feet/design.yaml) |

The rectangular `tube` primitive is open at both ends, with inner/outer walls
and closed rims. It makes genuine sleeves and cuffs instead of filled boxes.
Every armor atlas is 512 px. New recipes explicitly use 230 texels/metre,
except the many small gold-leg surfaces, which use 180 after a measured packing
overflow. Existing pilot density choices remain independent author settings.

`wear_sections` declares node, slot, target bones, anatomical reference bounds,
region (`slot`, `full`, `waist`) and local `euler_deg`. Bounds receive the same
physical fit/centering as geometry. Only the selected node subtree is submitted
for an anatomical section; held, dropped and catalog views still use the whole
item resource. Leggings have a waist, two thighs and two shins; boots have one
section per foot. The legacy worn path stays available for resources without
these annotations. Existing wear pose transforms remain applied.

The section transform rotates about the target reference center. A 180-degree
Y correction aligns the authored +Z front with the human rig's front without
swapping the left/right attachments or moving the sole plane. Native review
caught rear-facing copper/iron plates and reversed boot toes before this was
corrected. Source and runtime validation reject missing/ambiguous nodes, empty
reference volumes and duplicate bone assignments. Subtree selection also limits
the culling bounds; a complete trouser model is no longer copied onto every
thigh and shin.

Native QA records the exact section roots and draw counts for both body forms,
idle and walk snapshots, plus world minimum Y. Boot soles must stay above the
documented -0.025 m support tolerance in those snapshots. These technical gates
do not prove full animation-cycle/world acceptance or artistic quality. Final
skin clearance, all motion transitions, small icons, complete sets, world light
and performance still need the final acceptance pass.

All coordinate drawings are repository-authored functional armor constructions;
no external mesh, UV or image files are sampled. This source history is recorded
separately from final artistic-independence and licensing review.

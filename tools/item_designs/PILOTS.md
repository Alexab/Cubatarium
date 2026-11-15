# Pilot design cards

These twelve designs are candidates, not the finished production catalog.
Generate the comparison with `python tools/preview_item_designs.py`.
The source path samples no imported meshes, UVs or pixels: construction uses
authored coordinates and surface painting. References are functional archetypes,
including lashed stone heads, forged tools, vessels, sewn leather and baked food.

| ID | Construction / surface | Remaining visual acceptance |
|---|---|---|
| stone_pickaxe | Asymmetric chipped bridge, adze and point; lashed wood, mineral grain | Contacts and small silhouette |
| iron_pickaxe | Forged bridge, tapered point, chisel and socket wedge; brown shaft, cold steel | Contacts and distinction from stone |
| copper_axe | Broad convex cheek, cutting edge, wedged socket and rivets; hammered copper | Edge readability and hand placement |
| gold_sword | Asymmetric point, divided guard, fuller and six-sided pommel; gold and wrapped grip | Small-icon ornament clarity |
| iron_head | Open underside, brow band, ridge and cheek plates; forged steel | Both body forms, motion and skin clearance |
| leather_chest | Separate panels, straps, belt and open buckle; stitched leather and brass | Both body forms and motion |
| wood_bow | Curved YZ members, wrapped grip, articulated fiber string and a byte-preserving component of the authored arrow | Nock/string alignment verified; FP/TP hand pose and two-hand contacts still require correction |
| bucket | Open shell, rim, raised handle and cross-grip; metal and wood | Corrected FP grip, water and TP attachment |
| shears | Crossing blades, two hollow loops, tangs and hinge; contrasting steel | Articulated opening and finger contacts |
| apple | Ring-built faceted body, narrowed ends, stem and leaf; red skin fields | Leaf scale and eating pose |
| bread | Round faceted loaf and domed crown; warm crust fields | Score marks are too hidden and need surface work |
| raw_iron | Mineral lobes and exposed inclusions; dark matrix, oxide and pale metal | World scale and other ore consistency |

Seeding never overwrites an existing author design or surface layer. Canonical
production resources are unchanged until separate publication and acceptance.

## Contact and framing contract

Right/left grip anchors are expressed in construction coordinates and undergo
the same uniform fitting as the mesh. In the new FP path they supersede legacy
centre-plus-offset grips. FP Euler, scale and hand-root offset are independent
of physical bounds; root offsets move the arm and item together.

`icon_euler_deg` changes only icon/preview orientation. The bow uses it to show
the curved plane without changing its physical orientation. TP attachments use the same surface asset and have their own `tp_scale`.
Successful-use, bow-draw and water-fill snapshots drive named nodes in FP, TP,
world and inspection renders. Cancellation and empty/full state icon invalidation
are covered by the native candidate probes. Two-hand contacts and artistic
acceptance still require work.

`visual_bindings` supports normalized input ranges, linear/pulse transform
curves and visibility thresholds. Length-valued bindings receive the same
uniform fit as construction geometry. Span bindings stretch a root-child member
between two named anchors; they keep bow strings connected to moving limbs and
the draw point. Span outputs cannot be dependencies of other spans. Auxiliary
geometry marked `fit_reference: false` does not change the physical fit.

Published legacy fit axes do not apply to v3 FP assets: FP fitting uses the rest
silhouette and authored FP scale. This avoids enlarging a YZ bow by its shallow
Z thickness. Native FP probes record projected bounds and reject substantial
clipping, in addition to verifying that geometry was submitted.

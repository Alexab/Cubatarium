# Pilot design cards

Current catalogue status: all 70 v3 resources are published. Construction notes
and earlier acceptance lists below retain their checkpoint context. The current
[publication and acceptance record](../../docs/research/item-visual-redesign-2026-11-14/production-publication/README.md)
is authoritative; final integrated visual/performance acceptance is still open.


These twelve designs are candidates, not the finished production catalog.
Generate the comparison with `python tools/preview_item_designs.py`.
The source path samples no imported meshes, UVs or pixels: construction uses
authored coordinates and surface painting. References are functional archetypes,
including lashed stone heads, forged tools, vessels, sewn leather and baked food.

| ID | Construction / surface | Remaining visual acceptance |
|---|---|---|
| stone_pickaxe | Bevelled chipped bridge, adze and point; crossed fiber lashings and directed wood grain | Native fit and both-hand views checked; final small-icon/world review remains |
| iron_pickaxe | Forged bridge, bevelled point/chisel, visible socket pins and wedge; directional wood grain and steel working marks | Final world lighting and small-icon review |
| copper_axe | Faceted convex cheek and cutting edge, wedged socket and rivets; hammered copper | Final world lighting and small-icon review |
| gold_sword | Bevelled asymmetric blade, divided guard, narrow warm-metal fuller on both sides and six-sided pommel | Final small-icon ornament review |
| iron_head | Shallow ridged crown, raised face opening, brow band and cheek plates; forged steel | Eye clearance now checked on both body forms; full motion/world review remains |
| leather_chest | Separate panels, straps, belt and open buckle; stitched leather and brass | Both body forms and motion |
| wood_bow | Curved YZ members, wrapped grip, separated braced string and a byte-preserving component of the authored arrow | Rest string clearance and nock/FP/TP contacts checked; real-input transitions/world review remain |
| bucket | Open shell, rim, raised handle and cross-grip; metal and wood | Corrected FP grip, water and TP attachment |
| shears | Crossing bevelled blades, two hollow loops, tangs and visible front/back hexagonal hinge pins | Native opening/cancellation checked; final finger-contact/world review remains |
| apple | Authored unequal radial facets, short stem, veined leaf and clustered peel details | Final world/eating review |
| bread | Oval faceted loaf with exposed crown scores and a darker crust rim | Score visibility fixed in native FP; final world/eating review remains |
| raw_iron | Unequal matrix facets, tilted oxide lobe and embedded angular inclusions; dark rock and rough mineral surfaces | Native fit checked; other-ore consistency and final world review remain |

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

## Construction refinements

`revolve` accepts explicit per-side `radial_scales` for authored asymmetric
organic forms. The scale count must match `sides`; nonpositive/nonfinite or
non-convex outlines fail. Defaults preserve the former uniform geometry.

`ridged_shell` replaces the outer base cap with two roof planes and two gables,
preserving a closed wall surface and the inner floor. Inverted, this provides
the iron helmet crown. `shell` and `ridged_shell` also accept `front_rim_drop`:
the lowered front edge raises the face opening after inversion. Wall thickness
and inner floor remain intact; an opening that reaches the floor is rejected.
Both metal-helmet source templates and the actual authored head recipes carry
the eye-opening correction. The head-face UV direction is explicit, allowing
the 512 atlas and existing density to fit without enlarging texture budgets.

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

# Equipment, food and material wave

Current catalogue status: all 70 v3 resources are published. Construction notes
and earlier acceptance lists below retain their checkpoint context. The current
[publication and acceptance record](../../docs/research/item-visual-redesign-2026-11-14/production-publication/README.md)
is authoritative; final integrated visual/performance acceptance is still open.


This wave adds 23 authored candidates. Use `python tools/itemgen.py seed-supplies`
to create only missing source recipes. The constructor reads no external mesh,
UV or pixel data; after seeding, each design and its overlays remain authoritative.
Builds do not publish or change gameplay definitions. These are candidates,
with final artistic, interaction and performance acceptance still open.

| Item | Construction / surface |
|---|---|
| [wood_spear](wood_spear/design.yaml) | Faceted wooden shaft, leaf-shaped bevelled point, socket, bindings and separate butt cap. |
| [arrow](arrow/design.yaml) | Thin shaft, bevelled point, three feather vanes, binding and split nock. +Y is the flight axis used by existing ballistics. |
| [wood_shield](wood_shield/design.yaml) | Cut-corner wooden board panel, painted spine and dark rim; the reverse is wood too, with a hand loop, mounts and arm strap. Plank seams belong to the surface, not dozens of floating strips. |
| [iron_shield](iron_shield/design.yaml) | Different clipped outline, faceted plate, boss and rivets; separate reverse grip and strap. |
| [wood_torch](wood_torch/design.yaml) | Tapered staff, resin bundle, fiber bands and angular flame/core. Emissive factors keep only the flame visible in darkness. |
| [flint_and_steel](flint_and_steel/design.yaml) | Hollow forged striker and a separate knapped flint; the flint strokes about its pivot during a successful-use pulse. It no longer shares a torch silhouette. |
| [waterskin](waterskin/design.yaml) | Flattened sewn pouch, curved side welts, neck, stopper and tether. Fill 0..4 changes pouch volume, including its seams; it no longer shares bucket geometry. |
| [copper_ingot](copper_ingot/design.yaml), [iron_ingot](iron_ingot/design.yaml), [gold_ingot](gold_ingot/design.yaml) | Intentionally shared cast-bar blank, tapered mold form, individual metal surfaces and marks. This intentional geometry reuse is excluded from uniqueness claims. |
| [raw_copper](raw_copper/design.yaml), [raw_gold](raw_gold/design.yaml) | Different mineral/lobe arrangements, exposed metal and oxide/quartz surfaces. The existing raw-iron pilot remains independent. |
| [coal](coal/design.yaml) | Angular mineral mass and cleaved lobe, subdued reflective facets. |
| [charcoal](charcoal/design.yaml) | Charred branch with an exposed end and longitudinal cracking; distinct from mineral coal. |
| [cord](cord/design.yaml) | Three faceted hollow coils, loose end and tie, with directional fiber texture. |
| [stick](stick/design.yaml) | Tapered branch, exposed cut and small angled knot; wood grain follows the shaft. |
| [leather](leather/design.yaml) | Irregular cut hide and folded corner, with a different edge surface. |
| [wool](wool/design.yaml) | Faceted fleece bundle, side lock and binding; clustered curl marks. |
| [plant_fiber](plant_fiber/design.yaml) | Five stripped tapered fibers and a separate tie. |
| [seeds](seeds/design.yaml) | Scattered dark kernels, flattened individually. |
| [grain](grain/design.yaml) | Upright amber kernel cluster with creases, distinct from the scattered seeds. |
| [raw_meat](raw_meat/design.yaml) | Asymmetric fresh cut, marrow and fat ribbon; broad flesh variation and drawn marbling. |
| [cooked_meat](cooked_meat/design.yaml) | Fuller roasted cut, rendered fat and seared surface fields; a different construction from raw steak. |

Positive part `scale` permits flattening organic solids before rotation. It
preserves winding, normals and the explicit pivot. Negative/mirrored or
non-finite scales are rejected. Physical fitting still uses the unchanged item
profile. All seven equipment assets use 256-pixel atlases; the small material
atlases are 128, and the two handheld meat items 256. Explicit densities are
reduced where packing overflow was measured, not silently rescaled by the packer.

The item-only renderer supports linear standard glTF `emissiveFactor` values
within 0..1, in addition to albedo and metallic/roughness factors. Emissive maps,
normal maps and other unsupported material maps remain rejected. This is visual
emission only, not an added world light or gameplay mechanic. The factor and
default-texture semantics were checked against the
[glTF material specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#material-emissivefactor).

The flint/striker pulse and empty/half/full/cancel pouch states use the same
runtime bindings in FP, TP, ground rendering and inspection. Native inventory
QA now checks both bucket and waterskin state variants in the backpack and
footer, including returning to the same empty icon. FP review corrected the
striker's clipped stroke, pouch occlusion by the arm, repeated stitches across
every pouch facet and the wooden shield's metal reverse.

Open review includes all small-icon silhouettes, full world lighting and hands
in motion. The compact flint still fits the pre-existing tall/narrow collision
profile without changing it; physical rebalance is outside this visual wave.
The bow's temporary nocked-arrow mesh still needs to share the final arrow
resource, and full two-hand contacts remain unfinished.

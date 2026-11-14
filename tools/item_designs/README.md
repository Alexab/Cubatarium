# Authored item designs (v3)

This directory is **input**, not generated output. Build one design with:

```
python tools/itemgen.py build-design --id bucket
```

Output is `assetgen_output/items/redesign/<id>`. The command does not publish,
retarget gameplay definitions or rewrite the current runtime asset manifest.
Legacy recipe build/publish behavior is retained during migration.

`<id>/design.yaml` declares version 3, existing item ID, atlas_size (128/256/512),
texels_per_metre, triangle_budget, named materials and parts. Every part has a
unique name, material and shape. Supported shapes: convex CCW XY `extrude`
(contour/depth), `bevel_box` (size/bevel) and open `shell` (size/thickness).
Additional shapes: `revolve` (Y/radius profile, 3..16 sides) and `ring`
(outer_radius/inner_radius/depth, real hollow loops). Optional offset,
XYZ rotation in degrees, pivot and positive taper are applied
before uniform fitting into the unchanged gameplay physical envelope.

An optional positive XYZ `scale` flattens or elongates a part around its pivot
before rotation. This does not alter the gameplay physical profile.

`blade` uses a convex contour, web `depth`, thinner `edge_depth`,
`bevel_fraction` and optional `edge_material` to construct sloping cutting
bevels on both faces. A material `uv_direction` projects its preferred U axis
onto each surface without mirroring or stretching; `grain_axis: u` aligns wood
grain with that axis. `marks` selects restrained construction details on broad
working faces. See [the workshop tool cards](WORKSHOP_TOOLS.md) for all 25 tools.

Materials have an RGB color, pattern (plain/wood/leather/forged/hammered/stone/
food/fiber/water), optional seam, metallic and roughness. Textures contain surface-local
variation, not imported pixels. This is a base layer for authored work, not a
substitute for artistic review of each completed model.

Read the generated `uv_layout.json` for stable surface names and pixel rectangles.
Artist overlays live in `<id>/surfaces/<surface-name>.png`, RGBA, exactly the
island's width and height. Transparent pixels retain the generated base; opaque
pixels replace it. Regeneration reads but never modifies those source images.
Changing geometry or density can change a required overlay size, in which case
the build fails with the surface name instead of silently resampling the image.

`design_build.json` records source hashes and triangle count. glTF normals are
flat per face; material properties use standard glTF 2.0 fields. Presentation
metadata supplies right_grip, left_grip, fp_euler_deg, fp_scale, fp_hand_offset
and icon_euler_deg. Contact anchors follow the physical fitting transformation;
camera/hand presentation values are independent of it.
Named-node visual bindings project bow draw, successful-use pulses and container
filling without becoming an inventory store. See the [pilot cards](PILOTS.md)
for the binding/attachment contract and candidate limitations, and the
[Item Lab guide](ITEM_LAB.md) for read-only inspection and pose controls.
See [equipment and supply cards](SUPPLIES.md) for the new weapon, utility,
container, food and material constructions and the supported emission factor.
Manual glTF import, publication and the remaining Item Lab presentations are
still under development.

Generate comparative sheets with `python tools/preview_item_designs.py`. Use
`--read-staged` to inspect existing output without rebuilding; it refuses a
staging directory whose recorded author-source hashes have changed. This is
offline geometry inspection, separate from native acceptance.

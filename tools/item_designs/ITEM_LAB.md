# Native Item Lab

Open an existing QA world in the Release application and press **Ctrl+F8**.
F8 keeps its existing weather action. Esc, Ctrl+F8 or Close exits the studio.

The left preview uses the item definition's published resource under the runtime
asset root. The right preview uses
`assetgen_output/items/redesign/<id>/model.gltf` beside that root. Build a candidate
with `python tools/itemgen.py build-design --id <id>`; building does not publish it.
Without staging the right preview uses current production and says so explicitly.
Neither view equips an item or edits player property.

Search filters all catalog item IDs. Drag either preview to rotate both. The
mode button cycles lit, silhouette, gray material, albedo, UV checker, normals and
wireframe. Studio/Dim/Bright adjusts ambient lighting. Rest/Half/Peak drives the authored
visual channels: bow draw, a successful-use pulse and container filling. These
are test snapshots and do not perform the corresponding gameplay action.
Reload clears preview,
surface and texture caches and notifies game visual caches. It does not reload
item definitions or change gameplay balance. Export writes 512-pixel PNGs and a
JSON source-path/camera/pose report to `bin/item_lab_exports`.

Old resources without normals receive computed normals only in this inspection
path. A declared v3 candidate must pass the same normal validation as production.
The studio renders actual triangles, not bounding boxes.

The general acceptance runner stages authored designs and invokes Item Lab
through the application's input router. Its report is
`bin/items_acceptance/native.json.item-lab.json`, with layout captures beside it.
It checks all published items, seven candidate modes, six resolution/scale
combinations, pointer switching, closing and unchanged player inventory.

The studio now includes Orbit, FP, TP, Ground, Armor and native 32/48/64 icon
scenes, both body forms/hands, camera presets and static/animated poses.
See [the operator guide](../../docs/item-lab.md) for controls and export sizes.
Contacts/physical/UV overlays and detailed resource costs remain in development.

## Packaged old/current comparison

Before publication, the reference is the current published model and the candidate
comes from staging. After publication the reference uses `_itemgen_reference`;
the candidate still prefers staging, or uses current production if staging is
absent. Missing resources show a diagnostic, not a cube. Both viewports retain
shared camera, state, light, reload and inspection controls. Player property is
not an authoring or preview store.

Export schema 3 includes actual `published`, `reference` and `candidate` paths,
`archived_reference` and `staged_candidate` flags. Images are named
`<id>-<mode>-reference.png` and `<id>-<mode>-candidate.png`. These names replace
the ambiguous old `-published.png` reference label. Native QA exercises packaged
comparison without a staging folder at both resolutions and all three UI scales.
Non-orbit export names additionally include scene and actor index; the JSON
records scene, actor, camera, motion and animation time.

# Native Item Lab

Open an existing QA world in the Release application and press **Ctrl+F8**.
F8 keeps its existing weather action. Esc, Ctrl+F8 or Close exits the studio.

The left preview uses the item definition's published resource under the runtime
asset root. The right preview uses
`assetgen_output/items/redesign/<id>/model.gltf` beside that root. Build a candidate
with `python tools/itemgen.py build-design --id <id>`; building does not publish it.
A missing candidate is reported explicitly. Neither view equips an item or edits
player property.

Search filters all catalog item IDs. Drag either preview to rotate both. The
mode button cycles lit, silhouette, gray material, albedo, UV checker, normals and
wireframe. Studio/Dim/Bright adjusts ambient lighting. Reload clears preview,
surface and texture caches and notifies game visual caches. It does not reload
item definitions or change gameplay balance. Export writes 512-pixel PNGs and a
JSON source-path/camera report to `bin/item_lab_exports`.

Old resources without normals receive computed normals only in this inspection
path. A declared v3 candidate must pass the same normal validation as production.
The studio renders actual triangles, not bounding boxes.

The general acceptance runner stages authored designs and invokes Item Lab
through the application's input router. Its report is
`bin/items_acceptance/native.json.item-lab.json`, with layout captures beside it.
It checks all published items, seven candidate modes, six resolution/scale
combinations, pointer switching, closing and unchanged player inventory.

This initial studio is an orbit comparison screen. Dedicated FP/TP/ground and
both-body armor scenes, gameplay-state controls, contacts/bounds overlays and
32/48/64-pixel icon inspection are still required by the full redesign plan.

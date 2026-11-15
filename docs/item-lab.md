# Item Lab

Open the developer Item Lab with **Ctrl+F8**; close it with Ctrl+F8 or Escape.
It uses synthetic instances and never transfers or saves player property.

Search selects an item without changing its definition. The left panel resolves
an archived pre-publication reference when available, otherwise the published
model. The right panel resolves a staged redesign, otherwise the published model.
Captions identify the actual source. A missing or unsupported source reports an
error instead of rendering a substitute cube.

The same sidebar controls apply to both panels:

- **3D** cycles Orbit, FP, TP, Ground, Armor and Icons. Armor requires an armor
  item. FP invokes the gameplay viewmodel; TP and Armor invoke the creature
  preview/equipment drawers; Ground invokes the loose-item drawer.
- **Male/R** cycles body form and active hand. Armor uses the selected body;
  its anatomical sections follow the preview skeleton.
- **Cam** selects orbit/front/back/side/top views. Drag a panel to orbit.
  FP and Icons have fixed cameras.
- **Rest/Half/Peak** sets synthetic item state, including container contents,
  bow charge and utility phase where supported by the definition.
  In FP, eat/drink definitions also use their authored consumption arm gesture;
  gameplay supplies the actual held duration and clears it on cancellation.
- **Still/Walk pose/Play** sets static or animated preview motion. This is a
  presentation inspection, not a simulation of combat or carrying mechanics.
- The material mode cycles lit, silhouette, gray, albedo, UV checker, normals
  and wireframe. Lighting cycles Studio, Dim and Bright intensity presets.
- **Reload** invalidates the inspection and item caches. **Export** writes
  paired PNGs and a JSON report under `bin/item_lab_exports`.

Both sources use the current item rendering path; the left view is not an
emulation of the historical renderer. The synchronous, item-specific source
override is scoped and restores its predecessor even when rendering fails.
Definitions, inventory, world files and creature vertex formats stay unchanged.

Exports use schema 3 and include source paths, scene, actor, camera, motion,
animation time and the selected state preset. Panels export at 512x512.
FP renders at 1280x720 and is letterboxed in that square. Icons are rendered
at real 32/48/64-pixel target sizes and placed at native size in the exported
mosaic; fitting the mosaic into a GUI panel can scale the on-screen display.

Contact-point/physical-bounds overlays, detailed texture-memory costs, full
carry poses and final artistic/performance acceptance remain in development.

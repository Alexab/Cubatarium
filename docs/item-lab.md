# Item Lab

Open the developer Item Lab with **Ctrl+F8**; close it with Ctrl+F8 or Escape.
It uses synthetic instances and never transfers or saves player property.

Search selects an item without changing its definition. The left panel resolves
an archived pre-publication reference when available, otherwise the published
model. The right panel resolves a staged redesign, otherwise the published model.
Captions identify the actual source. A missing or unsupported source reports an
error instead of rendering a substitute cube.

The same sidebar controls apply to both panels:

- **3D** cycles Orbit, FP, TP, Ground, Armor, Icons, Contacts and UV. Armor requires an armor
  item. FP invokes the gameplay viewmodel; TP and Armor invoke the creature
  preview/equipment drawers; Ground invokes the loose-item drawer.
- **Contacts** shows the real model and its authored right grip (red cross),
  left grip (cyan diagonal cross), animated support grip (gold) and definition
  physical size (purple box). Coincident right/left anchors remain visible.
  Framing includes both the current model state and all diagnostic lines.
  The box is centred at the asset origin in physical metres; it is a definition
  reference, not a simulation of a moving world collider or effective cargo mass.
- **UV** displays every referenced albedo separately with real indexed triangle
  edges, including dormant model parts. Image aspect ratios are preserved;
  texture order matches the export's sorted image paths. glTF UV origin is the
  image's top-left. Camera/actor/pose/material controls are disabled for this
  static atlas view and retain their values for the other scenes.
- **Male/R** cycles body form and active hand. Armor uses the selected body;
  its anatomical sections follow the preview skeleton.
- **Cam** selects orbit/front/back/side/top views. Drag a panel to orbit.
  FP and Icons have fixed cameras.
- **Rest/Half/Peak** sets synthetic item state, including container contents,
  bow charge and utility phase where supported by the definition.
  In FP and TP, eat/drink definitions also use their consumption arm gesture;
  gameplay supplies the actual held duration and clears it on cancellation.
  Fill presets set both water identity and quantity; they do not consume water.
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

The two-line resource footer shows total triangles/materials and buffer/texture
costs rounded up to KiB (`K`). Mesh counts the actual position/UV/normal and
32-bit index payload uploaded by one item renderer. Tex counts unique referenced
RGBA8 images and every mip level. These are payload/texel estimates, not driver
VRAM, whole-process memory, or the cost of duplicate renderer instances. Costs
are cached per inspected source and refreshed by Reload. Hidden parts still
cost memory and are included.

Exports use schema 4 and include source paths, scene, actor, camera, motion,
animation time and the selected state preset. Panels export at 512x512.
Each available panel also exports exact byte counts, image sizes/mip levels and
encoded file sizes, physical profile, grip coordinates, current visual inputs,
all evaluated node matrices (column-major), and diagnostic line counts.
FP renders at 1280x720 and is letterboxed in that square. Icons are rendered
at real 32/48/64-pixel target sizes and placed at native size in the exported
mosaic; fitting the mosaic into a GUI panel can scale the on-screen display.

Full carry poses, independent state controls and final artistic/performance
acceptance remain in development. Run the focused Release/native checks with
`python tools/run_item_lab_qa.py`; `--skip-build` reuses the Release binaries but
still rebuilds authored candidates and checks the entire catalog. It uses
`tools/flight_sim_run.py` on an existing dedicated QA world and never overlays
or publishes production item resources.

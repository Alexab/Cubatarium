# glTF side-UV correction and re-review

Root cause: generated mesh face 2 is -X and face 3 is +X, but `_box_uvs` assigned the east and west atlas islands in opposite order. `surface_point` correctly painted each island in anatomical space, so asymmetric marks along Z (eyes, gills, fur details) appeared at the opposite end of a side face.

The exporter now maps -X to west and +X to east. The new non-square-box regression undoes the half-texel inset and verifies every exported vertex/UV on all six faces against the anatomical painting coordinates. It catches both swapped islands and reversed surface direction.

`side_uv_correction.json` proves all 13 previously published glTF species retain bit-identical positions, indices, weights, joint palettes, inverse bind matrices, node hierarchy, named animation tracks, PNG payloads and runtime definitions. Only UV coordinates change semantically. Puffin's newly declared walk clip also changes serialization order; its tracks are compared by clip name/target, not byte offsets.

Final evidence was recaptured through Release, with a close follow camera per species. Aquatic stingray, hermitcrab and penguin use real water; other rows use normal placement. This camera capability also supports the tiny seahorse and is confined to explicit QA environment flags.

All 13 complete native sheets, actual 64px icons and selected world views were directly reviewed and republished through review.json. For hermitcrab the close follow camera was inside shoreline terrain; that obstructed evidence was discarded for acceptance. runtime_water_hermitcrab/frame_002.png is the replacement open-water view. world_final.png uses that replacement. Badger retains an inherited Run contact minimum of -0.016819 blocks; this is an explicit R6 follow-up, not an effect of the UV change. Performance, full routes, equipment/FP/save-load/GLES and all-61 reproducibility remain R6.

After publication, an ordinary Release run without CUBA_CREATURE_QA_MODELS reproduced all 435/435 native preview frames of all fifteen glTF species pixel-identically. Pinned runtime hashes and parity results are in creature-gltf-side-uv-v2/context_production_release.json and production_preview_parity.json. This does not establish full R6 routes or performance acceptance.

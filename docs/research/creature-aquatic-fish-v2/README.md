# Shark / seahorse anatomy review

Reviewer: Codex, direct inspection; not user sign-off. Original b35f00a75 assets are research references only. Production mesh, UV, pixels and animation are own recipe output.

- `{id}.png`: baseline / old generic production / authored candidate in equal scale.
- `native_approved_{id}.png`: complete actual Release pose/motion sheet, 4 sides + 8 locomotion/run/action phases.
- `native_candidate/{id}_12.png`: actual 64px UI icon.
- `runtime_final_{id}/frame_002.png`: selected final close-world real-water view.
- `context_final_{id}.json`: exact reviewed payload/definition/executable hashes. Earlier captures precede UV correction and are historical only.
- `{id}_die_asset.png`: six actual sampled skinning poses, not a runtime Death state.
- `metrics.json`: rest extents, effective clip periods, 65-phase death contact (penetration below 0.0005 blocks).
- `rig_capability_audit.json`: actual weighted nodes, resolved through each mesh instance skin.
- `review.json`: final exact bundle and image hash decision.

Shark has 20 weighted joints, separate jaw/teeth, pectoral and pelvic fins, two dorsal fins, laterally beating tail chain and unequal vertical tail lobes. All five original clips retained. Body dimensions differ by about +3.1/-1.9/+0.8 percent. Own blue-gray countershading and anatomical gills.

Seahorse has 17 weighted joints, curved upright head/neck axis, long tube snout, crown, seven tail segments forming an actual open curl, two dorsal segments and pectoral fins. Own ochre ring pigmentation. Original idle/fly/die retained. Dimensions differ by +1.5/0/-0.1 percent. Tests verify a real opening in the curled-tail side silhouette and fin-driven upright motion.

The seahorse eye inspection exposed a generic side-UV island swap. It was corrected before acceptance. All 13 prior glTF models were separately re-reviewed and republished; see ../creature-gltf-side-uv-v2/README.md and side_uv_correction.json. The new regression matches all six face UVs to actual anatomical surface coordinates.

25 fidelity / 9 quality tests and 8 selected Release C++ tests pass. No performance PASS: gates were disabled. Continuous navigation, equipment/FP/save-load, GLES and complete all-61 reproducibility remain R6.

After publication, an ordinary Release run without CUBA_CREATURE_QA_MODELS reproduced all 435/435 native preview frames of all fifteen glTF species pixel-identically. Pinned runtime hashes and parity results are in creature-gltf-side-uv-v2/context_production_release.json and production_preview_parity.json. This does not establish full R6 routes or performance acceptance.

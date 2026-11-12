# Puffin / wasp fidelity review

Reviewer: Codex, direct inspection; not user sign-off. Baseline b35f00a75 is research-only.
Canonical authoring: tools/creature_recipes/{puffin,wasp}.yaml and surface_materials.py.

- `{id}.png`: baseline / previously published / candidate, equal scale per species.
- `native_approved_{id}.png`: full actual Release preview poses and 8 phases each of locomotion/run/action. The filename does not itself approve anything.
- `native_candidate/{id}_12.png`: actual 64px icon.
- `runtime_close_{id}/frame_002.png`: directly inspected world close view. Puffin is grounded here; wasp wings work in world. Continuous navigation/flight routes remain R6.
- `context_close_{id}.json`: pinned actual payload/definition/executable/shader hashes; matches final staging before publication.
- `{id}_die_asset.png`: six true sampled glTF skinning poses, not runtime Death.
- `metrics.json`: rest extents, timing and 65-phase actual skinning contact.
- `rig_capability_audit.json`: actual weighted nodes resolved through each mesh skin, excluding unweighted helpers.
- `review.json`: exact staged bundle and image hash gate, including explicit limitations.

Puffin: 18 weighted joints, three wing segments per side, original idle/fly/die plus independent grounded walk. Own pale mask, slate feathers and orange/gold/gray wedge beak. Dimensions about +2% over original. No baseline mesh/pixels copied.

Wasp: 29 weighted joints, six two-joint legs, four wings, segmented antennae, narrow waist and independent abdomen/tip sting motion. All original clips retained. Native inspection found ladder-like legs; front/rear leg pairs were spread and lower legs bent before final capture. Original wasp has 15 palette nodes but only one weighted node; these are not 15 independent deforming body chains.

All 22 fidelity, 9 quality and 8 selected Release C++ tests pass. Existing new-clip rejection test now uses an unpublished sentinel, so publishing owl/glide cannot silently change that test's precondition.

Performance gates disabled in research flights; no performance PASS. Full R6 equipment/FP/save-load/navigation/GLES/reproducibility remains pending. Death images do not establish a runtime Death state.

After publication, Release was run with the ordinary production root and no CUBA_CREATURE_QA_MODELS. All 58 native preview frames matched reviewed QA pixels (`production_preview_parity.json`); final world frame_002 was directly inspected. This is a two-species check, not full R6 acceptance.

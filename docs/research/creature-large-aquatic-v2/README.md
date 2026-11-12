# Large aquatic fidelity review

Reviewer: Codex, direct inspection; not user sign-off. Baseline b35f00a75 is research-only; own recipes, geometry, UV, texture pixels and animation are the production sources.

Final evidence:

- `{id}.png`: equal-scale baseline / prior generic production / authored candidate.
- `native_approved_{id}.png`: complete actual Release preview sheets, four sides and eight phases each of locomotion/run/action.
- `native_candidate/{id}_12.png`: actual 64px UI icon.
- `runtime_final_{id}/frame_002.png`: selected unobstructed final real-water world view, after the spawn fixes.
- `context_final_{id}.json` and `relocation_final_{id}.txt`: exact payload/executable hashes and actual spawned instance.
- `metrics.json`: rest size, effective clip periods and payload hashes.
- `rig_capability_audit.json`: each original has 8 palette nodes but only 1 actually weighted node; own rigs have 14/15 weighted joints.
- `review.json`: explicit exact-bundle and image hash acceptance.

Whale preserves idle/walk/run. Broad restored trunk, large head, long two-part pectoral flippers, two-part peduncle and horizontal flukes with actual vertical beat. Own navy/pale materials and throat pleats. Final X/Y/Z size change: +15.1/-9.5/0 percent; length remains 11 blocks. Extra width was explicitly reviewed to expose the long flippers outside the restored broad torso. Collider bounds, habitat and speed remain unchanged. First candidate was too narrow; buried/dark flippers were moved out and lightened before final capture.

Sea serpent preserves idle/walk/run/punch. Tall curved neck/body, small long head, articulated jaw and elongated swept horns. The head was lowered and horns lengthened after the first comparison. Own teal scale material, pale ventral plates and gold eyes. Final dimensions +3.0/-1.1/-0.3 percent. Curved side_profile tubes support optional positive bottom/top side_width. Full exported skinning tests confirm coincident joint-ring vertices stay coincident under combined pitch/yaw.

Neither original had a die clip; no generic death was fabricated as a capability.

QA findings:

1. A successful native preview did not imply actual world spawn. The initial whale world was empty: its rejected image/context are retained under runtime_whale_spawn_failed and *_whale_spawn_failed.*.
2. Water placement now checks the body's full yaw envelope and real adjusted spawn Y. The game's spawn routine overwrites the requested Y with its chosen fluid-column depth; validating the requested Y was insufficient.
3. Spawn is delayed until frame 30 so terrain/collision data are ready, before relocation and native export. Relocation validates free space without treating the existing creature as its own obstacle.
4. Tall aquatic necks may extend into open air under the existing 75-percent immersion rule; the volume must contain no solid terrain. Block-centre coordinates are respected.
5. The underwater camera uses a lower vertical offset and searches for an unobstructed direction. Final whale and dragon world images show actual whole models.
6. Generic runner requires a fresh current-run relocation dump and presence of every selected QA species, even if all preview PNGs exist. --production probes ordinary bin/models without a QA override and applies the same gates.

27 fidelity, 9 quality and 9 selected Release C++ tests pass (including aquatic spawn). App built using the required Release command only. No performance PASS: research metrics gates disabled. Full movement routes, compatibility/equipment/FP/save-load/GLES and complete all-61 regeneration/provenance remain R6.

Primary anatomy reference, accessed 2026-11-11: [NOAA PMEL humpback biology](https://www.pmel.noaa.gov/acoustics/whales/whale-biology-hump.html), describing broad flat head, long pectoral fins and contrasting fluke patterns. The creature is an own stylized whale rather than a copy of that reference image.

Final ordinary-catalog verification: --production was run separately for whale and water_dragon using the current Release executable, full half-diagonal yaw clearance and no CUBA_CREATURE_QA_MODELS. Both current-run relocation dumps contain the expected QA instance. All 58/58 preview frames are pixel-identical to the accepted candidates; production world frame_002 images were directly inspected. context_production_*.json, relocation_production_*.txt, report_production_*.json and production_preview_parity.json retain evidence. This is a partial R6 production check, not full routes/performance acceptance.

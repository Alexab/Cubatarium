# Manatee / seal fidelity review

Reviewer: Codex, direct inspection; not user sign-off. Baseline b35f00a75 is research-only. Own canonical recipe meshes, skin weights, atlases and animation; baseline geometry/pixels are not production inputs.

Final evidence:

- `{id}.png`: equal-scale baseline / prior generic production / authored candidate.
- `native_baseline_sheet_{id}.png`: actual original Release preview, to distinguish reference defects from offline diagnostic artifacts.
- `native_approved_{id}.png`: actual final Release pose, locomotion, run and action sheets. Native files come from final_water_manatee and final_land_seal contexts.
- `native_swim_seal.png`: separate eight true Swim phases.
- `native_candidate/{id}_12.png`: actual 64px UI icon.
- `runtime_final_water_manatee/frame_002.png`: selected actual manatee water image.
- `runtime_final_land_seal/frame_002.png`: selected seal land image.
- `runtime_water_grid_seal/frame_002.png`: selected seal water image. The close-water image was foreground-occluded and is not accepted evidence.
- `{id}_die_asset.png`: six true sampled glTF skinning poses, not runtime Death.
- `metrics.json`: actual exported 65-phase contacts, dimensions and runtime clip periods.
- `rig_capability_audit.json`: six original weighted joints each vs fifteen/sixteen own weighted joints, excluding unweighted helpers.
- `review.json`: exact staged bundle and image hash acceptance decision.

Manatee dimensions differ from baseline by -1.4/+5.4/+0.5 percent. It has a continuous bulky torso/neck, two small front flippers, prehensile upper lips and a broad, unforked horizontally flattened paddle. Swim is now mapped to its real fly clip instead of original idle. The first candidate had excessively wide front flippers; those were corrected before final capture.

Seal dimensions differ by +4.1/-4.7/+1.4 percent. Rest is grounded. Own gray spotted coat, short front flippers and paired rear flippers. Land uses body undulation with front-flipper support; water uses lateral rear-body/hind-flipper propulsion. Original clips are preserved. The first candidate was too flat and slightly floated in Rest; body height and origin were corrected before final capture.

Skinning tests check head/neck boundary coincidence during motion, loop continuity, vertical manatee vs lateral seal propulsion and actual contact over 65 phases. Seal Walk min/max-min Y: -0.000020/+0.000031 blocks. Both death clips penetrate less than 0.0005 blocks. Source baseline manatee separation is visible in actual current Release preview too; the own model does not reproduce that defect.

Primary anatomy/motion references, accessed 2026-11-11:

- [Florida FWC: manatee facts](https://myfwc.com/education/wildlife/manatee/facts-and-information/): fore flippers steer, round flattened paddle tail propels, prehensile lips manipulate food.
- [NOAA: seal anatomy](https://oceantoday.noaa.gov/sealanatomy/): harbor seals propel themselves with side-to-side hind-flipper movements and have restricted rear-flipper rotation on land.

26 fidelity, 9 quality and 8 selected Release C++ tests pass. Research flight performance gates were disabled: no performance PASS. Full routes and land/water transitions, equipment/FP/save-load/GLES and all-61 reproducibility remain R6.

Earlier water_manatee, land_seal and other pre-final captures are historical intermediates. baseline_native_review is an intermediate offline diagnostic with a baseline candidate column, not acceptance evidence.

After publication, ordinary Release was run without CUBA_CREATURE_QA_MODELS. All 58/58 actual native frames matched the accepted candidate pixels. context_production_release.json and production_preview_parity.json pin the exact normal runtime payload/executable and comparison. This is a two-species production parity check, not completion of R6.

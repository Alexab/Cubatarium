# Bow hand contact checkpoint

The authored bow now names its animated `pull` node as its support-hand contact.
Independent FP/TP aiming frames replace the generic tool orientation while
aiming. The main inventory slot remains the right hand, and the left hand draws
the string. Hand occupancy hides the equipped offhand visual while the bow is
drawn, without changing either inventory entry.

The versioned human rig solves both arms using the actual mesh-derived palms
and segment lengths. The desired bow position follows the rendered face through
crouch and head pitch; projection into both arms' reachable volumes preserves
arrow direction and string separation. The ordinary locomotion/creature vertex
format is unchanged. Bow release retracts the pose, while full UI/world resets
clear it. Actor/world identity is part of the FP transient pose key.

FP uses the same drawable palm bounds instead of approximate sockets. Its
support arm retains the authored proportions and UVs; a rigid rotation and
translation place its palm at the string. Rebinding the skin shader before the
left arm fixes the previously missing arm after a typed right-hand item draw.
The bow arm turns away from the string's return path around its palm. This is
a viewmodel pose, not a complete first-person shoulder/elbow IK system.

The bow's binding is narrower, preventing the width constraint from shrinking
the entire bow. Its relaxed height now uses the existing physical profile;
draw travel is 0.30 author units, with the existing limb flex/string spans.
Physics, economy and inventory definitions are unchanged.

## Evidence

- Release human motion tests cover both forms, head pitch -80/0/80 degrees,
  standing/crouch, three draw charges, exact string contact, arrow direction,
  zero-time retention, carry priority and release at 30/60/144 Hz.
- The focused Release native run passes 324 checks for bow/shield. The new
  checks inspect actual submitted item transforms against rendered palm bounds
  for both forms, still/walking, three camera angles, rest/partial/full/cancel.
  It exposed and corrected a preview-only missing carrier scale: local and
  world-space bow attachment matrices now agree. The equipped shield appears
  at rest/cancel and disappears only while drawing.
- FP native records check main palm contact, support contact, live skin shader
  programs and framing on the actual loaded mesh. Four representative native
  images and the focused raw reports are retained here, with hashes in
  `summary.json`. All eight runtime substitutions were restored after the run.
- CPU/resource contract tests reject missing/wrong-type support nodes and
  malformed aiming vectors. Carrier scale covariance has a CPU regression too.
- The subsequent all-70 staged Release regression passes 25 CTests, gameplay
  59, ownership 57, ownership UI 18, player window 7, icons 209, Item Lab 291
  and presentations 3571. All 212 runtime substitutions were restored.
  `full-result.json.gz` retains the raw combined report and
  `full-design-probe.json` records its exact executable hash and coverage.
  The earlier focused probe uses a distinct executable hash and narrower scope;
  both are explicitly recorded in `summary.json`.
- Eight new native Item Lab checks exercise rest/half/peak/rest FP snapshots
  on both forms. Reference and candidate snapshot renders reset their transient
  pose state independently, without requiring gameplay time to advance.
- `conservation.json` compares raw Git object content against `b9e27ed59`:
  all 70 gameplay definitions, 70 recipes and 775 legacy resource/manifest files
  are unchanged. This checkpoint has not promoted staged models to production.

The regression does not establish all-item art acceptance or final performance.
The overall goal remains active, with no production v3 promotion yet. Actual
input-driven bow/transition footage, complete authored carry contacts, Item Lab
overlays/costs and final all-item world/art/performance acceptance remain open.

```text
cmake --build build/desktop-msvc --config Release --target Cubatarium --parallel 8
python tools/run_item_design_probes.py --skip-build --presentation-only --id wood_bow --id wood_shield
```

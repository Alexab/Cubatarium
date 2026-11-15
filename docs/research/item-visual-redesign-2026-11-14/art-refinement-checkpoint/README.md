# Authored pilot refinement and headwear eye clearance

Eight candidates were refined after inspecting their real native FP, loose and
worn views. This is an artistic correction pass over staged resources, not a
claim that all 70 candidates have passed artistic acceptance.

- Iron pickaxe: bevelled working profiles, visible front/back socket pins and
  directed shaft grain. The original fit and attachment profile remain intact.
- Copper axe: bevelled cheek and cutting surface, rivet surface treatment and
  directed wooden grain, rather than a uniformly extruded plate.
- Gold sword: bevelled blade and a narrow warm-metal recessed line. The previous
  broad dark strip read as a hole through the blade in FP.
- Bread: an oval loaf with three exposed crown cuts. Earlier score details were
  hidden inside the loaf and did not communicate bread in the actual view.
- Apple: an irregular convex cross-section, shorter stalk, smaller veined leaf
  and localized skin variation/pore clusters instead of a rotationally uniform
  fruit and generic surface pattern.
- Iron helmet: a shallow ridged roof beneath its existing crest. Copper, iron
  and gold helmets now slope their frontal opening clear of the actual eyes;
  side/back coverage and the original wear cages remain unchanged.

New opt-in generator features are convex per-side radial scales, a closed
ridged hollow shell, a sloping open rim and fruit/leaf surface patterns. Defaults
retain the previous geometry. Metal head UV direction is explicitly X, allowing
the sloping rim to pack at the original 512 atlas size and texel density rather
than increasing texture memory. Author sources are not overwritten by a build.

## Evidence

`handheld-comparison.png` and `armor-comparison.png` juxtapose unmodified native
renders. `before-after.zip` retains 462 raw assets, author recipes and frames;
`archive-files.json` signs the archive members. Before frames are historical
captures from the executable identified in `summary.json`, not a same-executable
benchmark. After frames are from the final full staged regression. The earlier
focused run has its own distinct executable hash in `focused-summary.json`.

The old headwear defect was reproduced on all three metal helmets and both
human bodies: both eye-center rays hit armor. That baseline is retained in
`before-eye-obstruction.json`. The final regression uses a stricter 12-ray area
sample across both iris/white columns and three heights of the actual atlas eye
rectangles. All four headwear materials on both bodies pass; frontal worn PNGs
are also retained. This validates sampled eye clearance and visible results,
not all possible head/animation contacts.

The exact Release desktop command succeeded. The final all-70 staged run passes
25 CTests and native gameplay 59, ownership 57, ownership UI 18, player window 7,
icons 209, Item Lab 1023 and presentations 3587 checks. The real-flight harness
uses `tools/flight_sim_run.py`, `bin/Cubatarium.exe` and `CarryAimQA`. All 212
temporarily substituted runtime files were restored. Full and focused raw
reports are retained compressed, with actual executable/source hashes.

The first short focused route completed all presentation checks but produced
only 18 frames, below the unchanged 30-frame harness minimum. Presentation/Lab
routes now run for 12 seconds instead of three. Subsequent runs pass the original
frame gate; it was not weakened. All 13 mesh tests, surface/UV, design, armor,
workshop and other profile suites pass in the full run. Style, include boundaries,
changed-file clang-format and whitespace checks pass.

Production promotion remains **0/70**. Remaining work includes the other pilot
and family art reviews, small-icon/silhouette/world-light comparisons across all
70, independent Lab state controls, authored carry contacts and real-input
transition footage, final publication and repeated performance/property gates.
Black-on-dark loose silhouette captures are not accepted as silhouette evidence.
No gameplay definition, economy, identity or physical profile was redesigned here.

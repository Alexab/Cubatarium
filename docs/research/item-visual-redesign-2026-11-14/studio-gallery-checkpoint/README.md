# Complete native studio comparison gallery

All 70 candidates now have real native comparison sheets against their actual
reference resources: five lit cameras (orbit/front/back/side/top), silhouette,
gray, albedo and native-size 32/48/64 icons. Both sources use the current renderer;
the reference is not an emulation of an old shader. The gallery contains **1260
raw PNGs**, **70 item sheets** and **16 catalog overview sheets**. Captures are
scoped inspection instances and do not change player property or production.

Run the opt-in gallery through the documented Release/flight path:

```text
cmake --build build/desktop-msvc --config Release --target Cubatarium --parallel 8
python tools/run_item_lab_qa.py --skip-build --art-gallery
python tools/build_item_art_sheets.py
```

The runner validates fresh all-item coverage, both real sources and all nine
views. The sheet builder validates each reported 512 target and preserves the
occupied icon strips without resizing. Large comparison views are downsampled
for arrangement; unmodified native originals remain in the archive. Neither
capture success nor triangle counts automatically approve artistic quality.

## Findings and actual corrections

The 16 overview pages were inspected across all 70 items. Detailed sheet review
now covers all 12 pilots plus raw gold and cooked meat, with observations signed
against sheet hashes in `studio-review.json`. The other 56 detailed reviews are
explicitly pending. The comparison confirms the
replacement of unrelated-family placeholders and clear construction differences
from the old box/texture-tile catalog. Two concrete defects required correction:

- The resting bow string intersected broad wooden limbs and vanished in the
  silhouette. Its limbs are now narrow, segmented bent staves, joined at matching
  sections without coplanar overlapping front surfaces. The rest brace separates
  the string from the wood. String rest geometry follows the revised anchors;
  the embedded authored arrow follows the same nock. The original limb pivots,
  grips, uniform fit and fully drawn nock position are preserved. Count changes
  from 276 to 292 triangles, below the 1800 budget.
- The cooked cut's fat was buried under its surface and read as a tiny dot.
  A broader top cut, exposed fat strip and three localized sear bands now convey
  cooked food. Count changes from 80 to 116, below the 600 budget. Gameplay ID,
  nutrition, recipe and physical definition are unchanged.

`wood_bow.png` and `cooked_meat.png` are current full comparison sheets. The
correction archive retains their prior authored sources, staged assets and
native images, plus current sources/assets; `correction-files.json` signs them.
These are comparisons from the same Release executable. The initial broad-leaf
bow is historical evidence, not the accepted final candidate.

## Verification and limits

The exact Release build passes. Final `--art-gallery` passes **3560 checks**,
including the existing Item Lab UI/inputs/diagnostics/property checks and the
all-70 captures. The source conservation gate passes with frozen inputs. The
focused bow/cooked-food probe passes **596 checks**, covering both human forms,
FP/TP, nock/string contacts, shield occupancy, use/cancellation and loose items;
all 14 runtime substitutions are restored. Eight design tests, five probe
tests, 25 profile CTests and style/include/clang-format/whitespace audits pass.

One initial focused run lacked the two typed shield candidates needed by its
bow occupancy fixture. Focused bow selection now includes those dependencies;
the original occupancy checks remain intact. A separate intermediate gallery
was invalidated because its design-card documentation was edited during capture.
That summary is retained; only the later source-conserving run is accepted.

`native-report.json.gz`, `native-summary.json` and `flight.json` retain the final
capture results. Route performance gates were disabled: this is not a benchmark.
`native-comparisons.zip` stores all 1260 unmodified PNGs;
`comparison-sheets.zip` stores all 86 arranged sheets. `sheets-manifest.json`
signs both sets. Tested executable/source and evidence hashes are in summary.

Detailed remaining-item review, world day/night/interior and carry/contact
acceptance, final publication and final performance/property gates still remain.
This checkpoint does not claim all 70 have completed every artistic check.
Production promotion is **0/70** and the full goal remains active.

After implementation commit `91932727e`, fresh disposable checkouts in both
autocrlf modes preserve 981 signed files and regenerate/publish all 70 assets
byte-identically. Reference archives, working production and author sources
remain unchanged. `checkout-summary.json` and `checkout-output.zip` retain these
results; publication occurs only inside the disposable clones.

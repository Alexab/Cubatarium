# Independent Item Lab states and animations

Item Lab now separates synthetic bow draw, use phase, container contents and
ammunition. Rest/Half/Peak remain quick presets; an individual edit selects
Custom. A separate state-animation selector drives one channel while leaving
the other values intact. Character Still/Walk pose/Walk play has its own clock
and does not animate item state. A manual edit stops animation of that same
channel; other edits leave it running. Stopping restores the manual value.

The state row is shared by reference/candidate and uses the real candidate
capacity for whole-unit fill selection: bucket 0/1..1/1, waterskin 0/4..4/4.
Each source maps the normalized value to its own supported capacity. Unsupported
controls are disabled, an item without capacity shows No fill, and the animation
button skips unsupported channels. Values survive item/scene/reload switches;
UV disables controls. A retained animation has no visible effect on a newly
selected item which does not support its channel. This is inspection state only;
the studio still does not consume/save player property.

Schema 5 exports the manual values, state-animation channel/time, character
clock and preset/Custom label as well as the existing evaluated per-panel
inputs, node matrices, sources, contacts and costs. The guide is updated in
`docs/item-lab.md`.

## Verified scope

- The exact required Release desktop build succeeds; tested executable and
  worktree source hashes are in `summary.json`, with verbatim build output.
- `python tools/run_item_lab_qa.py --skip-build` regenerates all 70 candidates
  and passes **1040 native checks** through `tools/flight_sim_run.py` and
  `bin/Cubatarium.exe` on `CarryAimQA`. The process exits 0 with 563 in-game
  frames. Route performance metrics are disabled; this is not a benchmark.
- Real routed mouse input checks fill, unsupported-control rejection, independent
  use, Walk play isolation, Custom export and preservation through Reload.
- Native state checks cover independent container use/fill animation, manual
  cancellation, full bow with/without ammunition, actual arrow pixel differences,
  UV round trip and preset reset. The fixture uses a four-unit waterskin for
  intermediate fill states, rather than inventing fractional gameplay units.
- Layout/text-fit checks and retained UI frames cover 1280x720/1920x1080 and
  UI 100/150/200%. The state-animation button has its own top-right position;
  the four state controls retain readable widths at the smallest required size.
- Existing all-70 contact/UV/resource comparisons, real FP/TP/armor views,
  OpenGL mip allocation checks and property conservation remain green.
- All 25 profile CTests pass. Style, include-boundary, clang-format and
  whitespace checks pass.

Initial runs exposed quarter-fill rounding on the one-unit bucket and a narrow
label at 200%. `first-failure.json` retains the first failed checks. The interface
now selects actual units and the final run passes the unchanged layout gate.
No acceptance check was weakened to hide these issues.

`native-report.json.gz` retains the full native report, `custom-export.json` the
real schema 5 export and `native-selected-frames.zip` 14 selected unmodified
PNGs: 12 diagnostic UI sizes/scales and the bow ammunition pair. `frames.json`
signs those images. All 297 diagnostic frames were generated during the run;
this archive intentionally retains the 14 relevant to the new controls.

This finishes the previously missing independent studio controls. It does not
finish all-item artistic/world/carry-pose review, final publication or final
performance/property acceptance. Production promotion remains 0/70; the overall
goal remains active.

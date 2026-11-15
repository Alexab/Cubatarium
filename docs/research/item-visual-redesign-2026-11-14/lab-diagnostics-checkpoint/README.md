# Item Lab contacts, UV and resource diagnostics

Item Lab now adds two real diagnostic scenes beside Orbit/FP/TP/Ground/Armor/Icons:

- **Contacts** draws the actual item triangles, definition physical-size box,
  right/left grip markers and the animated support contact. The cyan left-hand
  marker is diagonal so coincident right/left anchors remain distinguishable.
  Framing includes the current visible geometry and all diagnostic vertices;
  it does not clip the physical box to the model's smaller dimensions.
- **UV** shows each referenced albedo with real indexed triangle edges, including
  dormant parts and embedded components. Images retain their aspect ratio and
  the glTF top-left UV origin. Each atlas uses only its own material primitives.
  Camera/actor/state/material controls are disabled for this static view and
  retain their values when returning to other scenes.

The footer reports total geometry/material counts and rounded-up KiB costs for
one renderer's vertex/index payload and unique referenced RGBA8 mip texels.
These are explicit payload estimates, not driver VRAM or whole-process memory.
Reference and candidate each have independent metadata; paths are retained in
the native report. Reload invalidates the metadata cache along with resources.

Schema 4 exports source paths, exact costs, image dimensions/mips/encoded bytes,
physical definition profile, grip coordinates, current state inputs and evaluated
column-major node matrices. Model state is projected from the same inputs used
by rendering. This also exposed a real studio defect: fill presets changed units
but left `HasWater=false`. They now set both identity and quantity, and filled
bucket pixels differ from the empty state without changing player property.

## Verified scope

- Exact Release desktop command succeeded; output and executable hash are retained.
- All 25 profile CTests pass, including new CPU checks for actual buffer layout,
  shared-image deduplication, rectangular odd-size mip chains, malformed image
  dimensions, real indexed UV edges and physical/contact vertices.
- `python tools/run_item_lab_qa.py --skip-build` rebuilds all authored candidates,
  invokes the required flight runner on `CarryAimQA`, and passes **1023 native
  checks**. All 70 IDs have contact/UV comparisons against the real reference.
  The script checks fresh report coverage and unchanged definitions, recipes,
  author sources and production resources; it does not overlay runtime models.
- The expected texture costs agree with actual allocated OpenGL sRGB8-alpha8 mip
  levels in all 140 scene comparisons. This proves the texel/payload scope stated
  above, not driver allocation overhead or deduplication between renderer instances.
- Both new scenes pass native layout/text-fit checks at 1280x720/1920x1080 and
  UI 100/150/200%. The checks measure label lines in addition to widget bounds.
- Existing studio checks still pass: seven material modes, real FP/TP/armor/ground
  and thumbnail views, bow contact transitions, reload, export, input and unchanged
  player inventory. This focused run does not claim a new gameplay/throw regression.
- Style, include-boundary and changed-file clang-format checks pass.

`native-report.json.gz` retains the full report. `native-comparisons.zip` contains
295 unmodified native PNGs: 140 reference images, 140 candidate images, 12 UI
captures and three bucket fill states. `frames.json` signs every raw image;
`summary.json` records tested source hashes, executable hash and evidence hashes.
Representative images are also directly accessible in this directory.

No production v3 model has been promoted. Final all-item artistic/world-lighting
review, independent studio state controls, authored carry poses, real-input
transition footage and final performance/property acceptance remain in the
original goal. This checkpoint supplies inspection tools and evidence for that
work; it does not finish the whole redesign.

```text
cmake --build build/desktop-msvc --config Release --target Cubatarium --parallel 8
python tools/run_item_lab_qa.py --skip-build
```

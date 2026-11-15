# Consolidated acceptance audit

Status: **COMPLETE — all implementation and acceptance gates pass**.
All 70 active authored v3 items are published. The final complete performance
series passes all seven gates with unchanged thresholds. Native presentation
verification passes 3753 checks. Current-source regression outputs are retained
under `final-source/`; historical checkpoint counts remain in their own records.

## Requirement coverage

| Requirement | Authoritative evidence and scope |
|---|---|
| Audit all 70 active items, old paths and duplicated silhouettes | [Baseline](../baseline.json) records all 70; [origin review](../production-publication/origin-review.json) reconciles each source, construction, before/after geometry and texture data. |
| Own faceted shapes and painted surfaces across tools, weapons, armor, utilities, food and materials | [All-item studio review](../studio-review-completion/README.md): 70 individually reviewed items, 93 sheets, 1260 native views and native 32/48/64 icons. Per-family construction cards and recorded observations support the art review; triangle counts alone do not. |
| Twelve pilots followed by all 70, with no unrelated-family stand-ins | Publication covers 70/70. The history preserves pilot and family waves; the final origin review covers their published replacements and intentional same-blank material families. |
| Generator geometry, pivots, hollow forms, UV direction/density/gutters, layers and per-item overrides | Current extended acceptance rebuilds and validates all 70 designs, with mesh, UV, component, design, family, raster and source-preservation tests. See [final source verification](final-source/verification.json) and its runner log. |
| Preserve authored geometry/paint and restricted manual glTF | [Manual-source record](../manual-checkpoint/summary.json), strict loader tests and current manual/component/publication tests cover transforms, UVs, normals, multiple materials, named nodes, invalid input and unchanged author files. |
| Separate staging/validation/publication, dependency hashes, rollback and historical import protection | [Committed publication](../production-publication/publication.json) has no pending journal and 70 verified reference archives. Current tests cover stale inputs and failure/process death on both sides of commit. |
| Deterministic clean checkouts and author/grip preservation | [Published-revision proof](../production-publication/release/checkout-proof.json): both autocrlf modes, 1864 signed files, all 70, byte-identical rebuild/publication, unchanged production and author trees. |
| Item-only normals/materials/node transforms, linear filtering/mips, independent presentation sizes | Current native presentation report includes material/color-space/state checks, 57 exact-pixel renderer-equivalence checks and 24 exact GUI RGBA comparisons for the static and legacy-shader paths. The item path retains the creature vertex ABI and creature lighting path. See [supported rendering subset](../../../ITEM_ASSETS.md). |
| Icon, FP, TP, loose item, held cargo and worn armor representations | Current 3753 presentation checks plus [actual all-70 world TP draw audit](../world-tp-resource-check/resource-audit.json). The independent gate requires both wield and drop submissions; its retained negative probe rejects drop-only false positives. |
| Both hands, both bodies, moving armor and use/carry transitions | Native presentation/body/headwear probes; [food/drink/bow real input](../use-input-checkpoint/README.md); [creature/block cargo real input](../creature-cargo-checkpoint/README.md). Identity, property and AI-return checks accompany captures. |
| Bow, shears, fill surfaces and cancellation from gameplay state | Current state/presentation checks, input-driven use and [64 utility captures / 304 checks](../utility-checkpoint/README.md). Temporary QA combustibility is explicitly declared; default pack ignition is not silently enabled. |
| Item Lab selection, old/new panes, cameras, seven modes, native small icons, FP/TP/drop/armor, body/hands, state/light controls | Current Item Lab report passes 1041 checks, with scene/input/reload/export tests and test-only instances. [Diagnostics evidence](../lab-diagnostics-checkpoint/README.md) covers overlays, resource costs and persistent independent controls. |
| Reload, cache invalidation and progressive icons without reopening | Content hashes cover same-size dependency edits; current 209 icon checks and real catalog completion pass. Ten GPU cache tests cover byte-exact deferred PNG persistence, read/draw/pack-state restoration and cancellation. |
| Provenance, licensing and independence separate from hash checks | [Publication origin review](../production-publication/origin-review.json) covers 70 reviewed items and zero imported mesh/UV/pixel inputs. Authored sources have their own attribution; historical Kenney/KayKit/Quaternius references remain separate. Direct Luanti copying was not established. This is not an exhaustive third-party catalog or legal certification. |
| Preserve item IDs, recipes, stats, ownership, saves and throw behavior | Publication compares all nonvisual definition fields with baseline. Current native gameplay 59, ownership 57, ownership UI 18, player-window 7, icons 209; 26 Release CTests also cover equipment, physical interactions, snapshots and resource economy. |
| 1280x720 and 1920x1080, UI 100/150/200%, day/night/interior | [Six actual-window Lab runs](../display-matrix/accepted-lab/) and [24 world/display/light runs](../display-matrix/accepted-world/) retain raw reports, requests, original images and hashes. Later FP fitting reuses the same cached default bounds; current native all-item probes check the resulting presentations. The GUI optimization additionally has independent exact-pixel coverage at both resolutions and all three scales; these offscreen tests are not mislabelled as another real-window matrix. |
| Cold catalog, texture memory, FP, armor and 512 resting/moving bodies; p95 <= reference +10% | [Final complete series](../performance-final/final-no-capture/summary.json): 30 runs, three alternating 30-second pairs per scene; all seven gates pass. FP +1.11%, armor +3.17%, warm catalog -1.64%, cold loading +8.39%, resting bodies -38.25%, whole moving scene -47.79%, moving-only -45.51%. Source/config/world/runtime restoration checks pass. [Method and retained failures](../performance-final/README.md) state screenshot policy and memory scope. |
| Release-only builds, flight runner and silent tests | The prescribed Release desktop build and 26 CTests pass. Eight native reports pass through `flight_sim_run.py` on a dedicated QA world; [final source record](final-source/verification.json). Audio is disabled during test processes and ordinary settings restored afterward. |
| CI exit codes/fresh evidence, style/include audit and final report | Windows workflow checks each step, stale/missing reports and actual world draw provenance; optional native matrix runners are wired. Local final audits pass. Remote CI execution is not claimed. The final source record signs evidence and records the precise tested executable/source relationships. |

## Corrected defects found during acceptance

- TP active items were hidden when the FP preference was enabled in a TP camera.
  Hiding now also requires actual first-person perspective; native draw evidence
  checks real submitted assets, not only the inventory ID.
- Explicit `physics.flammable` was ignored by both material parser branches.
  The parser honors it, with a red-before/green-after regression. Utility success
  captures use an explicit temporary QA combustible material and restore the
  original catalog.
- Shared world uniforms, static armor selections/bind transforms and local
  material reuse removed unnecessary draw setup without reducing geometry.
- Cold item lookups performed disk work synchronously; icon stores waited for
  GPU readback. Work is queued and budgeted, readbacks polled without waits,
  manifest publication deferred, and duplicate glTF parsing removed.
- FP fitting recalculated the default silhouette each frame. It now uses the
  exact bounds cached at load, independently of animated draw state.

- Packed GPU geometry preserves all item vertices and draw ordering, while reducing
  geometry GL objects from 954 to 210 per renderer cache containing all 70 items.
- Static GUI textured quads and streamed color buffers remove repeated in-flight
  buffer overwrites. Legacy/GLES shaders retain their pixel-position path; exact
  RGBA comparisons cover both paths at six resolution/scale combinations.
- Final diff review restored accidental encoding changes in comments and three
  diagnostic messages. An exact inverse byte check relates these two changed
  source files to the performance snapshot; no rendering/gameplay logic changed.

## Evidence interpretation

Production, studio, input and display records were captured at their stated
checkpoints, not all on one executable. The authored assets are the published
unchanged generation. Subsequent renderer optimizations have exact-pixel
comparisons plus current all-item native regression; the final performance series
has its own signed source snapshot and executable. `final-source/verification.json`
records the post-performance encoding-only edits and the final native rebuild.
Historical 0/70 and pending statements remain checkpoint history and are
superseded here. Remote CI, whole-driver VRAM, exhaustive legal clearance and
unmeasured hardware are not claimed by these local acceptance results.

## Separate backlog

These do not waive any required acceptance gate above:

- Choose default-pack wood/plant combustibility and fire balance separately.
- Broader hardware, driver and long inhabited-world performance testing; current
  acceptance is scoped to this AMD system and isolated repeatable scenes.
- Additional species-specific carry IK and broad world soaks from the earlier
  interaction backlog; existing native carry/identity/AI checks remain required.
- Full PBR/normal maps, cloth/ragdoll and an in-game model/texture editor are not
  part of this visual plan. Item Lab remains an inspection and export tool.

[Implementation history](../IMPLEMENTATION_HISTORY.md) retains abandoned probes
and earlier partial scopes without presenting them as final acceptance.

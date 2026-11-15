# Actual world item lighting checkpoint

All 70 staged items now have actual world captures in four contexts at
1280x720 / UI 100%: open daylight, open night, a closed unlit room and a closed
room with a real torch. Every frame uses the ordinary active inventory item,
FP renderer and a separately tracked finite physical drop. No preview renderer
substitutes for the world. The real light field is sampled at the player's eye:
open scenes have packed light 15, the closed unlit room 0, and the torch room
176 (sky 0 / block 11). Night uses day factor 0; daylight uses 1.

Twelve pilot items also pass the lit-room capture at 1920x1080 / UI 200%.
These are 292 accepted native frames, not a completed resolution/scale matrix.
The fixtures restore the QA world, configuration and all substituted runtime
files. Author files are conserved. Newer runs also check the fixture's own
source bytes. Production promotion remains 0/70.

## Visual observations

All 70 daylight FP crops were inspected, followed by all 70 night/torch-room
pairs. There are no new static FP silhouette or fallback-cube blockers in these
views. Working heads, handles, guards, shears halves, vessel handles and the
refined ore/food shapes remain recognizable. The light field darkens items with
the world; the torch retains working faces and the emitted torch flame remains
visible. Dark coal/charcoal lose fine surface detail in low light as expected.
The inherited creature-arm lighting remains brighter than item lighting; this
checkpoint does not change the creature renderer or certify its lighting style.
The unlit room was captured for every ID but only the iron-pickaxe frame received
separate full-frame inspection; do not equate its capture coverage with detailed
all-item art review in that context.

`review-pages.zip` retains daylight native crops and reduced night/torch crop
pairs. They are review aids, without exposure or color changes. Original full
frames remain in `native-world-frames.zip`; `frame-hashes.json` signs all 292.
The 1920/UI200 full-frame sample also retains the existing long development
version badge/HUD overlap; this is not a complete HUD layout acceptance.

## Fixture corrections and reproducibility

The first night run failed its overly broad one-body assertion when natural NPC
spawning introduced unrelated loot. The corrected fixture removes unrelated
creatures and verifies the specific spawned finite body by ID and stack.
The first torch-room run passed capture coverage but was rejected on visual
inspection: direct bulk mutation left part of the displayed wall stale. The
fixture now places its torch through `UWorld::AddObject`, the ordinary gameplay
placement path, including light and mesh invalidation. The repeated room is
closed in the native images. Rejected reports and representative defect PNGs
are preserved under `rejected-fixtures/` in `native-output.zip`; they are excluded
from the 292 accepted frames. This establishes a fixture correction, not a
claim that all unrelated world-streaming defects have been resolved.

`summary.json` records each run's exact executable, revision and source hashes.
The daylight/night/unlit runs use the earlier committed fixture; the corrected
torch and high-resolution runs use its successor. `tested-sources.zip` retains
exact bytes for every signed source in every accepted run. Release build,
style/include/format output, flight reports, requests and native metadata are
retained separately in `native-output.zip`. All runs invoke the Release binary
through `tools/flight_sim_run.py` on the restored `ItemVisualPerfQA` world.
The Windows workflow now runs all four world-light contexts in its optional
native item job, checks every exit code and uploads the results. Remote CI
execution is not claimed here.

## Remaining acceptance

This is static FP/ground world evidence. Actual key/mouse-driven use, carry,
one/two-hand contact, cancellation, throw/release/pickup and associated motion
remain for the next implementation pass. Final provenance reconciliation,
working-catalog publication, all final display combinations, property regression,
cold catalog/texture memory and paired frame-time/moving-body measurements also
remain. Earlier studio and synthetic attachment tests do not replace those gates.

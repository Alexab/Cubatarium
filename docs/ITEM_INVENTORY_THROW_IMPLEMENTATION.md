# Items, inventory and throwing implementation

Branch: `codex/items-inventory-completion`, base `develop` `83f250433`.
Worktree: `E:/Work/Home/Cubatarium-items-inventory`.
Original worktree and its three unrelated edits are preserved.

## Accepted scope

40 physical slots (four rows of ten), independent durable instances, separate creative loadout,
transactional inventory/equipment/crafting/repair, common player window, status effects,
preserve search/categories/3D previews/appearance editor. Complete existing 56 roles plus
material-processing/farming/food chains. Original generated art, real triangle/texture QA.
Throw one item at a time; grab any size/mass-eligible creature including hostile creatures;
predictable ballistic motion; one/two-handed carry; resistance; impact damage to both bodies.
Conduct separate throwing research/prototypes before production ballistic code.
Default V grab/place, hold/release R throw, Escape cancels wind-up.
No ragdoll requirement, no new physics middleware requirement, no destruction/explosion extension.

## Ordered delivery and evidence

- [x] Verify upstream develop and create isolated worktree.
- [x] Baseline native capture / audit regression fixtures.
- [x] Physical slot ledger, operations, migration, creative isolation, tests.
- [x] Throw research: primary sources, prototypes, measured tuning, native comparison.
- [x] Common player window and gestures, equipment/status/appearance.
- [x] Generator and genuine geometry/texture QA, provenance manifest.
- [x] Complete survival chain and original item sets.
- [x] Item/arrow ballistics then creature carry/throw, animation and bilateral impacts.
- [x] Release native QA, performance/regression matrix, CI and final report.

## Verification discipline

Only Release builds. Desktop command:
`cmake --build build/desktop-msvc --config Release --target Cubatarium --parallel 8`.
Use `tools/flight_sim_run.py` and `bin/Cubatarium.exe`, isolated QA worlds.
Do not report completion based solely on asset validators or offline rendering.
Research and implementation checkboxes remain open until evidence exists.


## Final evidence (2026-11-13)

[Implementation, measured balance, native reports, Release logs and backlog](research/items-completion-2026-11-13/README.md).
70 assets reproduce exactly; 20 Release tests, 47 native checks, 15 empty-inventory
survival stages, 24 UI layouts and active-row hand drag pass. Style/include gates now pass; eight auditor regressions and two publication
regressions pass. Remaining configured legacy dependencies and measurement limits
are recorded in the [final audit](research/items-completion-2026-11-13/final-audit/README.md).

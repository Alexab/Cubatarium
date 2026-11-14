# Native visual verification

Release executable; CarryAimQA, 1280x720. Actual native renders.

- catalog-blocks / catalog-items: progressive catalog and original models.
- backpack / equipment / status / appearance: common player navigation.
- carry-item-one-hand / carry-block-two-hands: FP cargo and hand occupancy.
- carry-creature-third-person: two-handed creature carry.
- bow-first-person: existing bow presentation preserved.

Captured through tools/flight_sim_run.py with
CUBA_OWNERSHIP_CATALOG_VISUAL_QA=1 or CUBA_ITEMS_CARRY_VISUAL_QA=1.
Overlay FPS is diagnostic context, not a controlled performance benchmark.
Flight records are in ../evidence.

Detail captures: carry-small-creature-one-hand and carry-creature-first-person
show the one/two-handed FP creature poses before release (capture interval 1s).

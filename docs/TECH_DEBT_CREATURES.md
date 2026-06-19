# Tech debt: Creatures (visual + catalog)

> Review at end of phases 0, 1, 2, 3, 4, 5. Close items when implemented or explicitly wont-fix.

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-CRE-001 | 0 | Full glTF backend (cgltf, skinned shader, clip playback) | Primary path is rigid_voxels | 5 |
| TD-CRE-003 | 0 | `visual.rig` parsed but does not select pose presenter | `locomotion_archetype` is sufficient | backlog |
| TD-CRE-006 | 1 | `AerialPosePresenter` — simplified wing flap | Enough for chicken MVP; not physics-based | backlog |
| TD-CRE-007 | 1 | `AquaticPosePresenter` / `SerpentinePosePresenter` | No mobs in phase-2 ship set | backlog |
| TD-CRE-008 | 2 | `FleeActivityAgent`, `MeleeAttackActivityAgent` | Visual scope, not AI | backlog |
| TD-CRE-009 | 2 | Spider / 8 legs rigid approximation | High complexity | backlog |
| TD-CRE-010 | 3 | FP viewmodel arms (`fp_parts[]`) | Not a blocker | backlog |

## Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| TD-CRE-012 | 0 | `CreatureVisualFactory` uses `std::cerr` once-per-species for glTF stub |
| TD-CRE-002 | 1 | Controlled head follows camera pitch via biped presenter |
| TD-CRE-005 | 2 | Ship-set creature JSON uses explicit pivot/limb fields |
| TD-CRE-004 | 3 | Icons for all ship-set species (`icon.png` or `parts_preview` FBO) |
| TD-CRE-011 | 4 | Creature resource packs merge via `ApplyCreaturePackOverlays` |

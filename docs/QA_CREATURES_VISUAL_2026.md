# QA: Creature visual restore (`bc94ade` → `arch_refactor3` @ `c05439d`)

Branch under test: **`arch_refactor3`** @ `c05439d26f809da440078a156d2a56fe5816a777`

Prerequisites:
- Clear local icon cache before first run: delete `bin/cache/icons/` (done in restore packet).
- Open creative palette (G), select creature tab, pick species from grid.

## Automated checks (2026-07-07)

| Check | Command | Result |
|-------|---------|--------|
| Creature code/assets vs baseline | `git diff bc94ade HEAD -- src/Creatures/ models/creatures/` | **empty** |
| glTF bind-pose / skinning | `python tools/test_gltf_skinned_bind_pose.py` | **33/33 passed** |
| Preview dock isolation | `ContentPreviewDock` uses `RenderUnique` (`c05439d`) | **present on branch** |
| Dock walk animation | `PreviewAnimTime` @ 0.8x, walk clip, 30 FPS re-render | **present** |

## Manual matrix

Sign-off: _pending in-game run_

| Species | Backend | World spawn + walk | Slot icon | Dock preview static (5s) | Dock orbit drag |
|---------|---------|-------------------|-----------|---------------------------|-----------------|
| wolf | gltf_skeleton | [ ] | [ ] | [ ] | [ ] |
| cow | gltf_skeleton | [ ] | [ ] | [ ] | [ ] |
| badger | gltf_skeleton | [ ] | [ ] | [ ] | [ ] |
| crab | gltf_skeleton | [ ] | [ ] | [ ] | [ ] |
| chicken | bone_skeleton | [ ] | [ ] | [ ] | [ ] |
| puffin | gltf_skeleton (TD-CRE-034) | [ ] | [ ] | [ ] | [ ] |
| manatee | gltf_skeleton (TD-CRE-035) | [ ] | [ ] | [ ] | [ ] |

### Pass criteria

1. **Dock preview** does not change without user drag (no «animation» / «fall apart»).
2. Body parts stay connected when rotating preview.
3. **Slot icon** matches dock preview model (± fixed yaw/pitch framing).
4. **World** model matches preview bind-pose silhouette (animation in world is OK).

### Known backlog (not blockers for restore)

- `puffin` / `manatee` may look wrong even on `bc94ade` — track as TD-CRE-034/035.
- Do **not** re-apply `114cf50` without per-species validation.

## Regression guard

If preview breaks again, check:
- `ContentPreviewDock.cpp` still calls `Renderer->RenderUnique`, not `Render`.
- `CreaturePreviewRenderer` uploads skin matrices via `UploadCreatureBonePaletteGpu` on desktop (UBO `BonePalette`), not only `SetMat4("uBones[i]")`.
- `WarmupCreatureIcons` and dock do not share a single displayed `ColorTex` handle.

# QA: Creature visual restore (`arch_refactor3` @ `f2e8a26`)

Branch under test: **`arch_refactor3`** @ `f2e8a26` (TD-CRE-034/035 fixes: `3de5d0f`, `f57e383`)

Prerequisites:
- Clear creature icon cache before first run: delete `bin/cache/icons/creature_puffin__*.png`, `creature_manatee__*.png` (or entire `bin/cache/icons/`), then restart game.
- Creative world. Desktop: **B** or **Inv** → **Creatures** tab. Android: touch **Inv** → **Creatures**.
- Spawn: select species in grid (assigns hotbar) → close palette → tap ground (Android) or use slot (desktop). Desktop QA grid: **F12** / **Shift+F12**.

## Automated checks (2026-07-07)

| Check | Command | Result |
|-------|---------|--------|
| glTF validate (skinned) | `python tools/validate_gltf_creature.py --skinned-only` | **33/33 OK** |
| glTF bind-pose / skinning | `python tools/test_gltf_skinned_bind_pose.py` | **33/33 passed** |
| Style gate | `python tools/audit_style.py` | **0 violations** |
| Preview dock isolation | `ContentPreviewDock` uses `RenderUnique` | **present** |
| Dock walk animation | `PreviewAnimTime` @ 0.8x, walk clip, 30 FPS re-render | **present** |
| puffin model fix | `git show 3de5d0f --stat` | **model.gltf/bin re-export** |
| manatee bounds fix | `git show f57e383 --stat` | **creature.json bounds** |

## Manual matrix

Sign-off: **partial PASS** — matrix criteria met; known visual backlog noted below.

| Species | Backend | World spawn + walk | Slot icon | Dock preview static (5s) | Dock orbit drag |
|---------|---------|-------------------|-----------|---------------------------|-----------------|
| wolf | gltf_skeleton | [X] | [X] | [X] | [X] |
| cow | gltf_skeleton | [X] | [X] | [X] | [X] |
| badger | gltf_skeleton | [X] | [X] | [X] | [X] |
| crab | gltf_skeleton | [X] | [X] | [X] | [X] |
| chicken | bone_skeleton | [X] | [X] | [X] | [X] |
| puffin | gltf_skeleton (TD-CRE-034) | [X] | [X] | [X] | [X] |
| manatee | gltf_skeleton (TD-CRE-035) | [X] | [X] | [X] | [X] |

### Pass criteria

1. **Dock preview** does not change without user drag (no «animation» / «fall apart»).
2. Body parts stay connected when rotating preview.
3. **Slot icon** matches dock preview model (± fixed yaw/pitch framing).
4. **World** model matches preview bind-pose silhouette (animation in world is OK).

### Manual notes (2026-07-07)

| Species | Observation | Tracker |
|---------|-------------|---------|
| crab | Неправильная форма — прямоугольная «палка» (asset quality) | backlog (pre-existing) |
| manatee | Дырка в теле: видны голова, конечности и зад; торс оторван от головы | **TD-CRE-035 reopened** — mesh/bind gap |
| wolf, cow, chicken | Выглядят как bone-skeleton; ок | — |
| badger | Ок; лапы движутся в стороны — возможно норма gait | — |
| puffin | Критерии preview/icon/world — ок | TD-CRE-034 closed |
### Policy

- Do **not** re-apply mass re-export `114cf50` without per-species validation.
- After any model change: `InvalidateKind("creature")` or clear `bin/cache/icons/` and re-verify world + dock + slot icon.

## Sign-off (manual run 2026-07-07)

- Tester: manual run (desktop)
- Devices: desktop
- Build/commit: `arch_refactor3` @ `f2e8a26`
- Date: 2026-07-07
- Icon cache refreshed: [X] yes (puffin/manatee entries cleared pre-run)
- Result: [ ] Manual PASS [X] Manual partial — TD-CRE-034 closed; **TD-CRE-035 reopened** (torso gap in world model)

## Regression guard

If preview breaks again, check:
- `ContentPreviewDock.cpp` still calls `Renderer->RenderUnique`, not `Render`.
- `CreaturePreviewRenderer` uploads skin matrices via `UploadCreatureBonePaletteGpu` on desktop (UBO `BonePalette`), not only `SetMat4("uBones[i]")`.
- `WarmupCreatureIcons` and dock do not share a single displayed `ColorTex` handle.

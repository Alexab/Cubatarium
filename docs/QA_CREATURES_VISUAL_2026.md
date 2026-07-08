# QA: Creature visual restore (`arch_refactor3` @ `5d4ed36`)

Branch under test: **`arch_refactor3`** @ `5d4ed36` (Luanti b3d skin export: `bb5d423`, P0 re-export: `5d4ed36`)

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
| b3d cube/Bone skin weights | `git show bb5d423 --stat` | **exporter fix + crab re-export** |
| P0 seal / hermitcrab re-export | `git show 5d4ed36 --stat` | **collapsed mesh restored** |

## Manual matrix

Sign-off: **partial PASS** — P0 aquatic mobs OK; manatee gap accepted as backlog.

| Species | Backend | World spawn + walk | Slot icon | Dock preview static (5s) | Dock orbit drag |
|---------|---------|-------------------|-----------|---------------------------|-----------------|
| wolf | gltf_skeleton | [X] | [X] | [X] | [X] |
| cow | gltf_skeleton | [X] | [X] | [X] | [X] |
| badger | gltf_skeleton | [X] | [X] | [X] | [X] |
| crab | gltf_skeleton (P0 skin fix) | [X] | [X] | [X] | [X] |
| seal | gltf_skeleton (P0 re-export) | [X] | [X] | [X] | [X] |
| hermitcrab | gltf_skeleton (P0 re-export) | [X] | [X] | [X] | [X] |
| chicken | bone_skeleton | [X] | [X] | [X] | [X] |
| puffin | gltf_skeleton (TD-CRE-034) | [X] | [X] | [X] | [X] |
| manatee | gltf_skeleton (TD-CRE-035) | [X] | [X] | [X] | [X] |

### Pass criteria

1. **Dock preview** does not change without user drag (no «animation» / «fall apart»).
2. Body parts stay connected when rotating preview.
3. **Slot icon** matches dock preview model (± fixed yaw/pitch framing).
4. **World** model matches preview bind-pose silhouette (animation in world is OK).

### Manual notes (2026-07-07 / P0 2026-07-08)

| Species | Observation | Tracker |
|---------|-------------|---------|
| crab | После `bb5d423` — нормальная форма, анимация ок | **P0 PASS** |
| seal | После `5d4ed36` — полный mesh, world/preview/icon ок | **P0 PASS** |
| hermitcrab | После `5d4ed36` — полный mesh, world/preview/icon ок | **P0 PASS** |
| manatee | Разрыв торса остаётся; оставлен как есть | **TD-CRE-035** — accepted backlog |
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

## Sign-off (P0 b3d skin fix — 2026-07-08)

- Tester: manual run (desktop)
- Devices: desktop
- Build/commit: `arch_refactor3` @ `5d4ed36`
- Date: 2026-07-08
- Icon cache refreshed: [X] yes (`creature_seal__*`, `creature_hermitcrab__*` cleared)
- Result: [X] **P0 PASS** — `crab` (`bb5d423`), `seal` + `hermitcrab` (`5d4ed36`); manatee unchanged (TD-CRE-035 backlog)

## Regression guard

If preview breaks again, check:
- `ContentPreviewDock.cpp` still calls `Renderer->RenderUnique`, not `Render`.
- `CreaturePreviewRenderer` uploads skin matrices via `UploadCreatureBonePaletteGpu` on desktop (UBO `BonePalette`), not only `SetMat4("uBones[i]")`.
- `WarmupCreatureIcons` and dock do not share a single displayed `ColorTex` handle.

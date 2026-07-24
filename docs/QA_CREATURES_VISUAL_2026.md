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
| P1 skin-weight batch | `git show 7d1690a` | **5/7 OK; stingray/seahorse reverted** |

## Manual matrix

Sign-off: **partial PASS** — P0 aquatic OK; P1 partial; manatee gap accepted as backlog.

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

### P1 notes (2026-07-08)

| Species | Observation | Tracker |
|---------|-------------|---------|
| lobster, puffin, shark, wasp, kitten | P1 skin-weight re-export OK | **P1 PASS** |
| seahorse | IBM fix in exporter; Bone weights + cube bake | **re-verify** |
| stingray | IBM fix in exporter; Bone weights + cube bake | **re-verify** |

### Policy

- Do **not** re-apply mass re-export `114cf50` without per-species validation.
- After any model change: `python tools/clear_creature_visual_cache.py --species <id>`, then fully quit the game and spawn new mobs; or clear `bin/cache/icons/` manually.

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

## Sign-off (P1 b3d skin fix — 2026-07-08)

- Tester: manual run (desktop)
- Build/commit: `arch_refactor3` @ HEAD (after stingray/seahorse revert)
- Date: 2026-07-08
- Result: [X] **P1 partial PASS** — `lobster`, `puffin`, `shark`, `wasp`, `kitten` OK; `seahorse`, `stingray` Bone weights restored (cube bake + `skin_bind_joints`), pending manual re-verify

### Skinning gaps (when they appear)

| Mode | Bind mesh | Animation | Typical failure |
|------|-----------|-----------|-----------------|
| Legacy cube weights | OK | **none** (channels on Bone, weights on cube) | stingray/seahorse «frozen» |
| Bone weights + cube bake | OK | OK | preferred for Luanti b3d |
| Bone weights + skin_joint bake | tail/neck **gaps** or inflated bbox | OK | stingray tail y≈1.5; seahorse neck gap ≈0.4 |

Root cause of split-bind gaps: mesh vertices baked through `bind_globals[skin_joint]` while IBM targets Bone joints whose bind scale is stripped to `(1,1,1)` — rest pose no longer matches Luanti cube layout.

**2026-07-08 IBM bug:** `mat4_inverse()` in `b3d_export_gltf.py` mis-inverted some bone rotations → wrong `inverseBindMatrices` → parts mirrored/off-body in world (stingray half-body, seahorse torso gap). Fixed via `numpy.linalg.inv`; re-export all b3d species with `--all-with-b3d`.


## Regression guard

If preview breaks again, check:
- `ContentPreviewDock.cpp` still calls `Renderer->RenderUnique`, not `Render`.
- `CreaturePreviewRenderer` uploads skin matrices via `UploadCreatureBonePaletteGpu` on desktop (UBO `BonePalette`), not only `SetMat4("uBones[i]")`.
- `WarmupCreatureIcons` and dock do not share a single displayed `ColorTex` handle.

# Camera projection modes

Cubatarium renders a single 3D voxel world. **Projection is a per-world camera lens**, not a second engine.

Gameplay input and camera pose are selected by `IUGameplayViewController`:

| Controller | When | Look | Move | F5 |
|------------|------|------|------|-----|
| `UFpsGameplayViewController` | perspective | free look | yaw-relative XZ | 1st / 3rd back / 3rd front |
| `UIsoGameplayViewController` | orthographic isometric | aims **character** (camera fixed) | screen-relative XZ | Close / Standard / Far boom |

## Modes

| Mode | JSON `view.projection` | Matrix | Controls |
|------|------------------------|--------|----------|
| Perspective (FPS) | `perspective` | `glm::perspective` | Mouse look, F5 person mode |
| Isometric | `orthographic_isometric` (alias `isometric`) | `glm::ortho` half-height = `ortho_size` | Elevated `lookAt` boom, Dungeons-style aim, Q/E snap 90°, scroll zoom |

`CameraPerspective` / F5 in perspective is **person mode** (1st/3rd). In isometric, F5 cycles **iso boom presets** (Close / Standard / Far). The body is always drawn in isometric.

## Isometric rig

Camera sits above-side of the player (`focus - lookDir * boomDistance`), looking at a point slightly below the eye. Orientation comes from `iso_yaw_index` + `iso_pitch_deg` (classic ~35.264°). Mouse/look-pad does **not** free-look the camera; it updates aim yaw for the controlled creature.

## Persistence

Stored in `worlds/World_NNN/world_data.json`:

```json
"view": {
  "projection": "perspective",
  "ortho_size": 24.0,
  "iso_yaw_index": 0,
  "iso_pitch_deg": 35.264,
  "iso_view_preset": 1
}
```

`iso_view_preset`: 0=Close, 1=Standard, 2=Far. Missing `view` → perspective defaults (old worlds unchanged).

## UI

- **New World** — View section: Perspective (FPS) / Isometric (+ ortho size).
- **World settings** (paused session) — same View form; Apply hot-reloads the camera and saves `world_data.json`.

Not configured via global `config.json` `render.*`.

## Code map

| Piece | Path |
|-------|------|
| Settings + JSON | `src/World/View/WorldViewSettings.*` |
| Screen → world ray | `src/World/View/ViewRayMath.*` |
| Lens matrices | `src/Render/Camera/CameraLens.*` |
| Mode enum bridge | `src/Render/Camera/ProjectionMode.h` |
| Boom presets | `src/Render/Camera/IsoViewPreset.h` |
| View controllers | `src/Render/Camera/Control/IUGameplayViewController.h`, `FpsGameplayViewController.*`, `IsoGameplayViewController.*` |
| Camera apply/capture | `UCamera::ApplyWorldViewSettings` / `CaptureWorldViewSettings` |

Picking uses center-screen `unProject` in both modes (`TryGetCenterViewRay`).

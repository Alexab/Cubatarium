# Camera projection modes

Cubatarium renders a single 3D voxel world. **Projection is a per-world camera lens**, not a second engine.

## Modes

| Mode | JSON `view.projection` | Matrix | Controls |
|------|------------------------|--------|----------|
| Perspective (FPS) | `perspective` | `glm::perspective` | Mouse look, F5 1st/3rd person |
| Isometric | `orthographic_isometric` (alias `isometric`) | `glm::ortho` half-height = `ortho_size` | Fixed pitch, Q/E rotate 90°, scroll zoom, WASD in screen XZ |

`CameraPerspective` / F5 is **person mode** (1st/3rd), not projection type. In isometric, F5 is a no-op (follow-only).

## Persistence

Stored in `worlds/World_NNN/world_data.json`:

```json
"view": {
  "projection": "perspective",
  "ortho_size": 24.0,
  "iso_yaw_index": 0,
  "iso_pitch_deg": 35.264
}
```

Missing `view` → perspective defaults (old worlds unchanged).

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
| Camera apply/capture | `UCamera::ApplyWorldViewSettings` / `CaptureWorldViewSettings` |
| Iso helpers | `src/Render/Camera/Control/IsoOrbitControl.*` |

Picking uses center-screen `unProject` in both modes (`TryGetCenterViewRay`).

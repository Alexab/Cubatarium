# Fog water unfinished (A + B)

## Goal

When the streaming edge has unfinished/missing surface near water, tighten air fog earlier and widen the sky horizon band so empty ocean columns read as fog rather than a clear skydome. No proxy quads / GPU masks.

## Signal (cheap)

Per frame in fog pull-in (`WorldStreaming`):

- `hole_debt` = `VisualHoles > 0` OR `UnfinishedVisual > 0`
- `near_water_ctx` = `eye.y < sea + 12` OR `HasNearbyFluidSurface(camera, 24)`
- `NearWaterUnfinishedFog` = `fog_water_unfinished_boost` AND hole_debt AND near_water_ctx

## A — stronger pull-in

If `NearWaterUnfinishedFog`:

- fog RD pull += 1 (still floored by `fog_rd_min`)
- end margin += 24 (on top of normal hole boost)
- `EffectiveFogStartRatio` capped by `fog_water_start_ratio_cap` (default **0.28**)

## B — sky horizon

Distance fog already sets `FogHorizonBlend = 1`. Widen band:

- `FogHorizonElevation` 0.35 → **0.22** (via `UnderwaterFogPass` → `SkyGradientPass`)

Skip when camera submerged (underwater path unchanged).

## Knobs

| Key | Default |
|-----|---------|
| `fog_water_unfinished_boost` | true |
| `fog_water_start_ratio_cap` | 0.28 |

## Out of scope

Proxy water planes, SoftDefer/mesh/Capture changes, underwater fluid-span fog.

## Validate

Shore/ocean with unfinished ring: earlier fog + thicker horizon; inland land-only unfinished: previous pull-in only; dive OK; no perf regression beyond uniforms.

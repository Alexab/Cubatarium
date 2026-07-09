## Tech Debt Log: Day-Night / Weather / Lighting

This file tracks implementation compromises for the environment and lighting rollout.

### 2026-07-08: Initial MVP lighting and environment

- **Issue:** Lighting currently samples local light per greedy vertex via CPU scans.
- **Current decision:** Use bounded neighborhood sampling and simple LOS checks during mesh rebuild.
- **Risk:** Heavy chunk rebuilds may become CPU-expensive in dense scenes.
- **Follow-up:** Replace per-vertex sampling with cached chunk light fields (`skyLight` + `blockLight`) and budgeted incremental propagation queues.

- **Issue:** Emissive blocks are inferred by block name heuristics (`torch`, `lamp`, `lava`, etc.).
- **Current decision:** Heuristic fallback is used until block definitions expose explicit emission levels.
- **Risk:** False positives/negatives for custom resource packs.
- **Follow-up:** Add explicit `lighting.emission` to block definitions and migrate emissive detection to data-driven values.

- **Issue:** Skylight cave leakage is approximated with side opening probes, not full flood-fill.
- **Current decision:** Keep approximation for MVP performance and implementation simplicity.
- **Risk:** Some interiors can be overlit/underlit compared to canonical voxel flood-fill.
- **Follow-up:** Implement two-channel voxel light propagation with dirty-region updates.

### 2026-07-09: Variant D hybrid weather (streaks + particles)

- **Issue:** Previous procedural fullscreen overlay used heavy `discard` and was nearly invisible; a tint fallback looked like a white wash.
- **Current decision:** Split weather into `UWeatherRenderPass` with depth-aware streaks (`fshader_weather_streaks.glsl`) and instanced camera-volume particles (`UWeatherParticleSystem`).
- **Risk:** Streak pass still depends on default-FBO depth copy quality per driver.
- **Follow-up:** Validate depth capture on more GPUs; optional half-res streak FBO on Balanced tier.

- **Issue:** GLES depth copy for weather masking is disabled (same class of issue as opaque depth guard AND-17).
- **Current decision:** Android uses sky-band fallback (upper ~55% of screen) for streaks; particles remain the primary precipitation layer.
- **Risk:** Streaks may clip at horizon band on GLES.
- **Follow-up:** Enable `GL_OES_depth_texture` path when driver supports reliable copy.

- **Issue:** Vertex wetness is baked at mesh rebuild; async mesh results may lag weather transitions briefly.
- **Current decision:** Uniform `uEnvWetness` covers all top faces; vertex `wetness` attribute refines exposed tops when chunks rebuild. `SetWeather` invalidates mesh when precip state class changes.
- **Risk:** Wetness on GLES greedy path uses uniform only (no vertex light/wetness attributes in `gles/vshader_greedy.glsl`).
- **Follow-up:** Extend GLES greedy vertex layout for parity.

- **Issue:** Particle pass auto-cools down for 5s when pass exceeds ~2.5ms.
- **Current decision:** Simple frame hitch guard without global quality downgrade.
- **Risk:** Brief particle disappearance during spikes.
- **Follow-up:** Tiered budget reduction instead of full cooldown.

- **Issue:** Screen-space weather background on sky remains perceptually unnatural
  (regular artifacts, weak weather semantics).
- **Current decision:** Disable streak background pass for now and ship
  particles-only precipitation (`WeatherOverlayEnabled=false` by default, F8
  keeps overlay off).
- **Risk:** Distant sky has less explicit precipitation cues.
- **Follow-up:** Revisit with a new sky-weather approach (spatiotemporal blue
  noise driven, cloud-layer aware) after dedicated visual R&D.

### QA matrix (Variant D)

| Scenario | Expected |
|----------|----------|
| F8 cycle clear→rain→storm→snow→cloudy | Visible streaks + particles on precip presets |
| `weather overlay off` | Particles + atmosphere only |
| `weather particles off` | Streaks + atmosphere only |
| Both off | Sky/fog/wet uniform only |
| Fast preset | No particles; lighter streaks |
| Under overhang | Streaks rejected by depth on desktop |
| `weather debug 1` | Solid blue streak pass overlay |
| Android GLES | Sky-band streaks + reduced particle budget |

### Performance guardrails

- Target: streak pass < 0.5ms, particle pass < 1.5ms on desktop Balanced.
- Hard rule: no nested fragment loops in weather shaders (prior 0 FPS regression).
- Streak alpha cap: 0.35; particle alpha cap: 0.45.

### 2026-07-09: New sky pipeline (stars + celestials + clouds)

- **Issue:** Volumetric clouds are implemented as a compact screen-space raymarch approximation in `fshader_sky.glsl`, not full 3D world-space cloud volumes.
- **Current decision:** Keep quality-tier cloud step budgets (`Fast=6`, `Balanced=12`, `Quality=18`) to stay within frame budget while replacing the old static sky.
- **Risk:** Cloud parallax and horizon depth cues remain approximate at extreme camera FOVs.
- **Follow-up:** Move clouds to dedicated half-resolution volumetric buffer with temporal reprojection history and height-aware phase function.

- **Issue:** Celestial typing currently infers moon/sun from `id` token when data is loaded.
- **Current decision:** Preserve explicit persisted `type`, but keep ID heuristic fallback to avoid breaking old saves.
- **Risk:** Misnamed custom bodies can get incorrect defaults when legacy data omits `type`.
- **Follow-up:** Add strict schema validation and migration for `environment.celestial_bodies`.

### QA matrix (Sky)

| Scenario | Expected |
|----------|----------|
| Midnight (`time set 0.0`) with clear weather | Visible star field + moon disc |
| Daytime (`time set 0.5`) | Stars almost invisible, sun disc visible |
| `sky stars 0` | Stars disabled regardless of time |
| `sky clouds 1` | Dense cloud layer with preset-dependent quality |
| `sky reset_celestials` | Default sun/moon pair restored |
| Fast vs Quality presets | Cloud detail and stability increase with preset |

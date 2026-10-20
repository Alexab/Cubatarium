## Tech Debt Log: Day-Night / Weather / Lighting

This file tracks implementation compromises for the environment and lighting rollout.

### 2026-07-09: Chunk lightmap (sky + block channels)

- **Issue:** Per-vertex CPU light scans baked a single `vLight` channel and async meshing skipped local light entirely.
- **Current decision:** Store packed skylight/blocklight in `UChunk`, relight on edits/load/worldgen, sample lightmap in mesh, and apply day-night/weather only to skylight in shaders.
- **Risk:** Full-chunk relight on edits is simple but may spike CPU when placing large structures.
- **Follow-up:** Persist light in `.cchunk`. **Data-driven `lighting.emission` implemented (2026-07-10):** `BlockDefinition.Lighting`, `UBlockRegistry::GetLightEmission`, canonical_blocks.yaml + cross instance lightmap sampling.

- **Issue:** Emissive blocks previously used name heuristics in `LightUtil`.
- **Current decision:** **Closed (2026-07-10).** Emission is read from `lighting.emission` in block JSON / canonical metadata only.

### 2026-07-09: Variant D hybrid weather (streaks + particles)

- **Issue:** Previous procedural fullscreen overlay used heavy `discard` and was nearly invisible; a tint fallback looked like a white wash.
- **Current decision:** Split weather into `UWeatherRenderPass` with depth-aware streaks (`fshader_weather_streaks.glsl`) and instanced camera-volume particles (`UWeatherParticleSystem`).
- **Risk:** Streak pass still depends on default-FBO depth copy quality per driver.
- **Follow-up:** Validate depth capture on more GPUs; optional half-res streak FBO on Balanced tier.

- **Issue:** GLES depth copy for weather masking is disabled (same class of issue as opaque depth guard AND-17).
- **Current decision:** Android uses sky-band fallback (upper ~55% of screen) for streaks; particles remain the primary precipitation layer.
- **Risk:** Streaks may clip at horizon band on GLES.
- **Follow-up:** Enable `GL_OES_depth_texture` path when driver supports reliable copy.

- **Issue:** Baking weather state into mesh vertices caused a global camera scalar and required remeshing during weather transitions.
- **Current decision:** Vertex wetness is now a material mask; regional chunk wetness and skylight attenuation are supplied as draw uniforms. Chunk wetness is updated lazily and stored in chunk format v4 (binary and JSON); older chunks default to dry.
- **Risk:** Rendering updates persisted wetness for drawn chunks; unloaded chunks catch up when they are drawn again. Weather material response remains a subtle tint rather than a full roughness/specular water film.
- **Follow-up:** Add a dedicated wet-surface material response if the renderer gains a stable roughness/specular path.

- **Issue:** Particle pass auto-cools down for 5s when pass exceeds ~2.5ms.
- **Current decision:** Simple frame hitch guard without global quality downgrade.
- **Risk:** Brief particle disappearance during spikes.
- **Follow-up:** Tiered budget reduction instead of full cooldown.

- **Issue:** Screen-space weather background on sky remains perceptually unnatural
  (regular artifacts, weak weather semantics).
- **Current decision:** The streak layer is enabled by default as a close rain
  layer with multi-scale spatiotemporal noise and wind drift; it is depth-tested
  on desktop. Snow uses particles only. Android GLES
  keeps the sky-band fallback because depth sampling is disabled there.
- **Risk:** The streak pattern is still screen-space; it is a secondary cue,
  not a world-space precipitation volume. Balanced-tier performance must be
  measured on representative desktop hardware.
- **Follow-up:** Add cloud-aware streak density if visual QA identifies a need;
  the current pass is intentionally a secondary near-screen cue.

### QA matrix (Variant D)

| Scenario | Expected |
|----------|----------|
| F8 cycle clear→rain→storm→snow→cloudy | Visible rain streaks + particles; mixed rain/snow particles fade smoothly at the phase boundary |
| `weather overlay off` | Particles + atmosphere only |
| `weather particles off` | Streaks + atmosphere only |
| Both off | Sky/fog/wet uniform only |
| Fast / Performance presets | Reduced particle and streak budgets |
| Under overhang | Streaks rejected by depth on desktop |
| `weather debug 1` | Solid blue streak pass overlay |
| Android GLES | Upper-screen sky-band streaks + reduced particle budget |

### TD-LIGHT-001 (2026-07-11): Bulk block light skipped after create/load

- **Issue:** After cooperative create or load, emissive block light (torches, lava) is invisible until the player places a block in the chunk; skylight may be partially applied in mesh.
- **Root cause:** `UWorldCooperativeSession::BeginEmissiveBlockLightQueue` skipped the emissive pass for `Create`/`Load` and jumped straight to mesh warmup; bulk skylight used `include_block_light=false` and frontier iteration 1; mesh warmup could start before relight completed.
- **Resolution (2026-07-11):** `RelightEmissiveBlockLight` phase now runs for create/load (async when `AsyncRelight`); bulk skylight uses `kRelightFrontierIterationsFull`; mesh warmup starts only after emissive queue drains. Regression: `chunk_bulk_blocklight_test`.
- **Cross-link:** Commit-path relight spikes during movement — [TECH_DEBT_CHUNK_STREAMING.md](TECH_DEBT_CHUNK_STREAMING.md) TD-CS-021.

### Performance guardrails

- Target: streak pass < 0.5ms, particle pass < 1.5ms on desktop Balanced.
- Hard rule: no nested fragment loops in weather shaders (prior 0 FPS regression).
- Streak alpha cap: 0.18; particle alpha cap: 0.45.

### 2026-10-20: World-anchored sky and regional weather

- **Issue:** The former UV/noise cloud approximation ignored the camera projection and stretched toward the horizon. `uCloudSteps` and `uCloudJitter` did not affect sampling.
- **Current decision:** Reconstruct view rays from the inverse projection; intersect two finite world-space cloud layers; sample a periodically refreshed, camera-centred weather coverage map; composite cloud radiance through transmittance. This is a layered 2.5D sky, not a volumetric raymarch.
- **Risk:** Weather coverage texture resolution and far-horizon fallback are approximations; cloud self-shadowing and volumetric fly-through are not implemented.
- **Follow-up:** Consider a half-resolution volumetric pass only if gameplay and profiling show a need for clouds that surround the camera.

- **Issue:** Automatic weather previously classified climate from a separate noise path and applied one global episode to the whole world.
- **Current decision:** `WeatherFrontField` advects deterministic, smoothly blended fronts from the world seed and saved simulation clock. Biome weights follow the active procedural or image-map source; schema 3 biome profiles can tune temperature, precipitation likelihood, and wind exposure. Dry/precipitation fractions and episode duration/transition settings shape deterministic local precipitation windows. Sky, fog, chunk materials, and precipitation use local samples.
- **Risk:** The cloud field remains a low-resolution 2.5D approximation; close volumetric fly-through and cloud self-shadowing are not implemented. Chunk wetness is lazily updated when chunks are drawn, so untouched/unloaded chunks are not eagerly simulated.
- **Follow-up:** Evaluate a volumetric cloud pass only if gameplay and profiling show a need for close cloud interaction.

- **Issue:** Legacy celestial records did not store a body type.
- **Current decision:** An explicit persisted `type` is authoritative. ID-based inference runs once only for old records that omit `type`.
- **Risk:** Misnamed custom bodies can get incorrect defaults when legacy data omits `type`.
- **Follow-up:** Add strict schema validation and migration for `environment.celestial_bodies`.

### QA matrix (Sky)

| Scenario | Expected |
|----------|----------|
| Midnight (`time set 0.0`) with clear weather | Visible star field + moon disc |
| Daytime (`time set 0.5`) | Stars almost invisible, sun disc visible |
| `sky stars 0` | Stars disabled regardless of time |
| `sky clouds 1` | Dense cloud layer with preset-dependent quality |
| `sky reset_celestials` | Default sun/moon pair restored; star sphere inclination follows primary sun |
| Fast vs Quality presets | Weather map uses 128² vs 256² resolution |
| Camera yaw/pitch at night | Stars locked to celestial sphere (`view_dir` + `uStarCelestialInv`), not screen UV |
| `time add` through a night | Whole star field sweeps on inclined daily arcs (same orbit-frame laws as celestial bodies) |
| Dense clouds over stars and celestial discs | Clouds reduce sky, stars, and discs by accumulated transmittance |
| Weather field across biome borders and negative coordinates | Smooth front edges; active biome source controls precipitation phase and likelihood |
| Auto weather disabled or manually overridden | Field clock freezes only when automatic weather is disabled; manual weather stays global |
| Saved world and chunks from older formats | Missing field clocks and chunk wetness load with safe defaults |
| Rain and snow across all four quality presets | Visible camera-volume particles; only rain also uses the secondary streak layer |
| Desktop OpenGL and Android GLES | Desktop depth-guarded streaks; GLES upper-screen fallback without depth copy |

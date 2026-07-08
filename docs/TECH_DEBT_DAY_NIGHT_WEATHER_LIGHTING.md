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

- **Issue:** Weather visuals currently affect fog and sky tint but do not include precipitation particle passes yet.
- **Current decision:** Keep rendering impact minimal in MVP and preserve current pipeline stability.
- **Risk:** Weather feels incomplete without visible rain/snow particles.
- **Follow-up:** Add a single batched precipitation pass with quality-tier budgets.

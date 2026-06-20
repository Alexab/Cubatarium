# Worldgen — out of scope / future work

Items explicitly deferred from the worldgen enhancement roadmap (phase 5 and partial phase 4).

## Not planned (research only)

- **Terrain Diffusion / Mindcraft** — GPU diffusion or text-prompt terrain; incompatible with deterministic column streaming.
- **Full hydraulic erosion** on CPU for infinite streaming worlds.
- **Runtime `.mca` / gemblocks import** as live generators.

## Phase 4 — remaining data-driven work

- **Pipeline YAML loader** — `content/worldgen_packs/*/pipeline.yaml` is a reference stub; stages are still selected via `ComposableWorldGenConfig` in C++.
- **Hot-reload** of worldgen packs without restart.
- **Per-generator pack overrides in UI** — `worldgen_pack_id` is serialized; UI field not exposed yet (descriptor default: `default`).
- **Biome JSON** — height + optional `features` weight multipliers; palette/smooth_radius and structure queues not migrated from C++.

## Completed roadmap gaps (2026-06)

- Tundra structures/decoration in prefab pools
- Sub-biome filters and weight multipliers for prefab placement
- Coast shelf height adjustment near water
- Cave GUI (min Y, scale, max depth) and `bedrock_top_y`
- `WorldGenPack::LoadPackId`, descriptor `PackId`, biome `features` in pack JSON
- Example `image_demo` pack with PNG biome map

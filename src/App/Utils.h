#ifndef UTILS_H
#define UTILS_H

namespace cutum
{

/// Headless GL init + LoadSystem for CI / --validate-load.
int RunValidateLoad();

/// Headless world generation for CI / --create-world.
int RunCreateWorld(int argc, char **argv, int create_world_index);

/// Creature definition + texture key smoke for CI / --creature-asset-smoke.
int RunCreatureAssetSmoke();

/// Load default world and probe spawn for wolf vs box_uv mobs.
int RunCreatureSpawnSmoke();

/// Spawn several mobs and verify they move over time.
int RunCreatureMovementSmoke();

/// Wander AI + ExecuteIntent over simulated ticks.
int RunCreatureWanderSmoke();

/// Diagnostic probes for a single species (footprint, move, step-up).
int RunCreatureMovementDiagnose(const char *speciesId);

/// Terrestrial mobs climb a 1-block ledge toward +X.
int RunCreatureStepUpSmoke();

/// Three mobs at one cell should separate without vertical stacking.
int RunCreatureStackSmoke();

/// Serializer throughput smoke for chunk I/O backends.
int RunBenchChunkIo();

} // namespace cutum

#endif

#ifndef UTILS_H
#define UTILS_H

namespace cutum
{

/// Headless GL init + LoadSystem for CI / --validate-load.
int RunValidateLoad();

/// Headless world generation for CI / --create-world.
int RunCreateWorld(int argc, char **argv, int create_world_index);

/// Serializer throughput smoke for chunk I/O backends.
int RunBenchChunkIo();

} // namespace cutum

#endif

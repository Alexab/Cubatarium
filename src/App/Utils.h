#ifndef UTILS_H
#define UTILS_H

namespace cutum
{

/// Headless GL init + LoadSystem for CI / --validate-load.
int RunValidateLoad();

/// Serializer throughput smoke for chunk I/O backends.
int RunBenchChunkIo();

} // namespace cutum

#endif

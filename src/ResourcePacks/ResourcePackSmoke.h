#ifndef RESOURCEPACKSMOKE_H
#define RESOURCEPACKSMOKE_H

namespace cutum
{

class IPlatformPaths;

/// Headless resource-pack merge + worldgen refs smoke (no window/GL).
int RunResourcePackSmoke(IPlatformPaths &paths);

} // namespace cutum

#endif

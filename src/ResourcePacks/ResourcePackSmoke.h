#ifndef RESOURCEPACKSMOKE_H
#define RESOURCEPACKSMOKE_H

namespace cutum
{

class IUPlatformPaths;

/// Headless resource-pack merge + worldgen refs smoke (no window/GL).
int RunResourcePackSmoke(IUPlatformPaths &paths);

} // namespace cutum

#endif

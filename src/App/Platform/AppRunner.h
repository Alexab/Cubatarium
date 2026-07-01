#ifndef APP_RUNNER_H
#define APP_RUNNER_H

namespace cutum
{

class IUPlatformPaths;
class IUPlatformWindow;

int RunCubatarium(IUPlatformWindow &window, IUPlatformPaths &paths);

} // namespace cutum

#endif

#ifndef APP_RUNNER_H
#define APP_RUNNER_H

namespace cutum
{

class IPlatformPaths;
class IPlatformWindow;

int RunCubatarium(IPlatformWindow &window, IPlatformPaths &paths);

} // namespace cutum

#endif

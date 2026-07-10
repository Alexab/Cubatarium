#ifndef APP_RUNNER_H
#define APP_RUNNER_H

namespace cutum
{

class IUPlatformPaths;
class IUPlatformWindow;

int RunCubatarium(IUPlatformWindow &window, IUPlatformPaths &paths);

/// Hidden-window GUI smoke: Enter Game with default_world, exit after N in-game frames.
int RunEnterGameSmoke(IUPlatformPaths &paths, int in_game_frames = 5);

} // namespace cutum

#endif

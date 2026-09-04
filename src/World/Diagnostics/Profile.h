#pragma once

/// Cubatarium profiling facade.
/// With -DCUBATARIUM_ENABLE_TRACY=ON expands to Tracy zones; otherwise no-ops.
/// Prefer CUBA_ZONE over hand-rolled chrono for new instrumentation.

#if defined(CUBATARIUM_ENABLE_TRACY)
#include <tracy/Tracy.hpp>
#define CUBA_ZONE(name) ZoneScopedN(name)
#define CUBA_ZONE_S(name) ZoneScopedN(name)
#define CUBA_FRAME_MARK FrameMark
#define CUBA_FRAME_MARK_NAMED(name) FrameMarkNamed(name)
#else
#define CUBA_ZONE(name) ((void)0)
#define CUBA_ZONE_S(name) ((void)0)
#define CUBA_FRAME_MARK ((void)0)
#define CUBA_FRAME_MARK_NAMED(name) ((void)0)
#endif

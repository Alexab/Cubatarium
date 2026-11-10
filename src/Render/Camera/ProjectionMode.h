#ifndef PROJECTIONMODE_H
#define PROJECTIONMODE_H

#include "World/View/WorldViewSettings.h"
#include <cstdint>

namespace cutum
{

enum class ProjectionMode : uint8_t
{
  Perspective = 0,
  OrthographicIsometric = 1,
};

inline ProjectionMode
ProjectionModeFromWorld(WorldProjectionMode mode)
{
  return mode == WorldProjectionMode::OrthographicIsometric
             ? ProjectionMode::OrthographicIsometric
             : ProjectionMode::Perspective;
}

inline WorldProjectionMode
WorldProjectionModeFromProjection(ProjectionMode mode)
{
  return mode == ProjectionMode::OrthographicIsometric
             ? WorldProjectionMode::OrthographicIsometric
             : WorldProjectionMode::Perspective;
}

} // namespace cutum

#endif

#ifndef WORLDVIEWSETTINGS_H
#define WORLDVIEWSETTINGS_H

#include "Render/Camera/IsoViewPreset.h"
#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace cutum
{

enum class WorldProjectionMode : uint8_t
{
  Perspective = 0,
  OrthographicIsometric = 1,
};

/// Per-world camera projection (world_data.json → "view").
struct WorldViewSettings
{
  WorldProjectionMode Projection{WorldProjectionMode::Perspective};
  float OrthoSize{24.0f};
  int IsoYawIndex{0};
  float IsoPitchDeg{35.264f};
  IsoViewPreset IsoBoomPreset{IsoViewPreset::Standard};
  /// First-person hands / tool (Perspective only; isometric ignores).
  bool ShowFpWield{true};

  void Validate();
  static WorldViewSettings FromJson(const nlohmann::json &root);
  nlohmann::json ToJson() const;
};

const char *WorldProjectionModeToString(WorldProjectionMode mode);
bool WorldProjectionModeFromString(const std::string &value,
                                   WorldProjectionMode &out);

inline bool ShouldDrawFpViewmodel(const WorldViewSettings &v)
{
  return v.ShowFpWield &&
         v.Projection == WorldProjectionMode::Perspective;
}

} // namespace cutum

#endif

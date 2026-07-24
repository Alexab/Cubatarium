#ifndef WORLDVIEWSETTINGS_H
#define WORLDVIEWSETTINGS_H

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

  void Validate();
  static WorldViewSettings FromJson(const nlohmann::json &root);
  nlohmann::json ToJson() const;
};

const char *WorldProjectionModeToString(WorldProjectionMode mode);
bool WorldProjectionModeFromString(const std::string &value,
                                   WorldProjectionMode &out);

} // namespace cutum

#endif

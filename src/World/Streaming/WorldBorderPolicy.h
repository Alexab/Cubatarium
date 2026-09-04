#pragma once
// BUDGET_MS: 0.0  // perf-root P4: measure via Tracy; kill-switch required for new heuristics

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace cutum
{

/// Phase 4: soft XZ world border (product clamp) before hard sanitize 1e5.
struct WorldBorderConfig
{
  /// Soft half-extent in blocks from origin (default ~ large inland map).
  float soft_half_extent{32000.0f};
  /// Hard kill (matches IsReasonablePlayerPosition).
  float hard_half_extent{100000.0f};
  /// Slow zone width inside soft border.
  float soft_margin{256.0f};
};

inline bool IsInsideHardWorldBorder(const glm::vec3 &pos,
                                    const WorldBorderConfig &cfg)
{
  return std::abs(pos.x) <= cfg.hard_half_extent &&
         std::abs(pos.z) <= cfg.hard_half_extent;
}

inline bool IsInsideSoftWorldBorder(const glm::vec3 &pos,
                                    const WorldBorderConfig &cfg)
{
  return std::abs(pos.x) <= cfg.soft_half_extent &&
         std::abs(pos.z) <= cfg.soft_half_extent;
}

/// Clamp XZ into soft border (Y unchanged). Returns true if clamped.
inline bool ClampToSoftWorldBorder(glm::vec3 &pos, const WorldBorderConfig &cfg)
{
  bool clamped = false;
  if (pos.x > cfg.soft_half_extent)
  {
    pos.x = cfg.soft_half_extent;
    clamped = true;
  }
  else if (pos.x < -cfg.soft_half_extent)
  {
    pos.x = -cfg.soft_half_extent;
    clamped = true;
  }
  if (pos.z > cfg.soft_half_extent)
  {
    pos.z = cfg.soft_half_extent;
    clamped = true;
  }
  else if (pos.z < -cfg.soft_half_extent)
  {
    pos.z = -cfg.soft_half_extent;
    clamped = true;
  }
  return clamped;
}

/// Speed scale in soft margin (1 = full, ~0.25 at edge).
inline float SoftBorderSpeedScale(const glm::vec3 &pos,
                                  const WorldBorderConfig &cfg)
{
  const float ax = std::abs(pos.x);
  const float az = std::abs(pos.z);
  const float edge = std::max(ax, az);
  const float inner = cfg.soft_half_extent - cfg.soft_margin;
  if (edge <= inner)
  {
    return 1.0f;
  }
  if (edge >= cfg.soft_half_extent)
  {
    return 0.25f;
  }
  const float t = (edge - inner) / std::max(1.0f, cfg.soft_margin);
  return std::max(0.25f, 1.0f - 0.75f * t);
}

/// Chunk outside soft border should not gen/load.
inline bool ShouldRefuseStreamBeyondSoftBorder(int chunk_x, int chunk_z,
                                               const WorldBorderConfig &cfg,
                                               int chunk_size)
{
  const float bx = (static_cast<float>(chunk_x) + 0.5f) *
                   static_cast<float>(chunk_size);
  const float bz = (static_cast<float>(chunk_z) + 0.5f) *
                   static_cast<float>(chunk_size);
  return !IsInsideSoftWorldBorder(glm::vec3(bx, 0.0f, bz), cfg);
}

} // namespace cutum

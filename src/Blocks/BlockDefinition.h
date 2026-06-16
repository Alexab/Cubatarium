#ifndef BLOCKDEFINITION_H
#define BLOCKDEFINITION_H

#include "World/Math/BlockTypes.h"
#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

struct BlockAnimationSpec
{
  int FrameCount{1};
  int FrametimeTicks{2};
  bool Interpolate{false};
};

struct BlockMovementPhysics
{
  float Occupancy{1.0f};
  float DragHorizontal{0.0f};
  float DragVertical{0.0f};
  float SinkSpeed{0.0f};
  float RiseSpeed{0.0f};
  bool DamageOnContact{false};
};

struct BlockPhysicsProfile
{
  BlockMovementPhysics Movement;
  static BlockPhysicsProfile Solid();
  static BlockPhysicsProfile FromPreset(const std::string &preset);
};

enum class BlockRenderStyle
{
  UCube,
  Fluid,
  Cross,
};

struct FluidViewProfile
{
  glm::vec3 FogColor{0.02f, 0.12f, 0.22f};
  float FogStart{0.0f};
  float FogEnd{24.0f};
  float FogMinBlend{0.0f};
  glm::vec3 OverlayColor{0.0f};
  float OverlayAlpha{0.0f};
};

struct BlockRenderProfile
{
  bool Transparent{false};
  bool DoubleSided{false};
  BlockRenderStyle Style{BlockRenderStyle::UCube};
  FluidViewProfile FluidView;
};

struct BlockDefinition
{
  std::string Name;
  BlockId Id{BLOCK_AIR};
  BlockAnimationSpec Animation;
  BlockPhysicsProfile Physics;
  BlockRenderProfile Render;
  std::vector<std::string> Types;
};

BlockAnimationSpec ParseAnimationFromJson(const nlohmann::json &j);
BlockPhysicsProfile ParsePhysicsFromJson(const nlohmann::json &j);
BlockRenderProfile ParseRenderFromJson(const nlohmann::json &j);
void ApplyRenderPresetDefaults(BlockRenderProfile &Render,
                               const std::string &physicsPreset);

} // namespace cutum

#endif

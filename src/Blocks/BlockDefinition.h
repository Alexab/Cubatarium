#ifndef BLOCKDEFINITION_H
#define BLOCKDEFINITION_H

#include "World/Math/BlockTypes.h"
#include "World/Math/FluidCellState.h"
#include <array>
#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>
#include <optional>
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
  bool Falling{false};
  bool IsLiquid{false};
  float LiquidViscosity{1.0f};
  bool LiquidRenewable{false};
  bool Floodable{false};
  int FluidSpreadPeriodTicks{5};
  int FluidMaxLevel{7};
  std::optional<bool> FluidPermeable;
  FluidKind FluidKindPreset{FluidKind::None};
  bool Flammable{false};
  static BlockPhysicsProfile Solid();
  static BlockPhysicsProfile FromPreset(const std::string &preset);
};

enum class BlockRenderStyle
{
  UCube,
  Fluid,
  Cross,
  Cutout,
};

struct FluidViewProfile
{
  glm::vec3 FogColor{0.02f, 0.12f, 0.22f};
  float FogStart{0.0f};
  float FogEnd{24.0f};
  float FogMinBlend{0.0f};
  glm::vec3 OverlayColor{0.0f};
  float OverlayAlpha{0.0f};
  float BelowSurfaceFogMin{0.52f};
  float BelowSurfaceFogScale{0.35f};
};

struct BlockRenderProfile
{
  bool Transparent{false};
  bool DoubleSided{false};
  BlockRenderStyle Style{BlockRenderStyle::UCube};
  FluidViewProfile FluidView;
};

struct BlockLightingProfile
{
  int Emission{0};
};

struct BlockDefinition
{
  std::string Name;
  std::string DisplayName;
  BlockId Id{BLOCK_AIR};
  /// Dig hardness. 0 = unbreakable in Survival (Creative still breaks instantly).
  float Hardness{1.0f};
  BlockAnimationSpec Animation;
  BlockPhysicsProfile Physics;
  BlockRenderProfile Render;
  BlockLightingProfile Lighting;
  std::vector<std::string> Types;
};

struct ParsedBlockJson
{
  BlockDefinition Definition;
  std::array<std::string, 6> TextureStems{};
  bool Valid{false};
};

bool IsReservedBlockName(const std::string &name);

ParsedBlockJson ParseBlockFromJson(const nlohmann::json &j,
                                   bool useStablePackId = false);

BlockAnimationSpec ParseAnimationFromJson(const nlohmann::json &j);
BlockPhysicsProfile ParsePhysicsFromJson(const nlohmann::json &j);
BlockRenderProfile ParseRenderFromJson(const nlohmann::json &j);
BlockLightingProfile ParseLightingFromJson(const nlohmann::json &j);
void ApplyRenderPresetDefaults(BlockRenderProfile &Render,
                               const std::string &physicsPreset);

} // namespace cutum

#endif

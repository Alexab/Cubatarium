#ifndef BLOCKDEFINITION_H
#define BLOCKDEFINITION_H

#include "BlockTypes.h"
#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum {

struct BlockAnimationSpec {
 int frameCount{1};
 int frametimeTicks{2};
 bool interpolate{false};
};

struct BlockMovementPhysics {
 float occupancy{1.0f};
 float dragHorizontal{0.0f};
 float dragVertical{0.0f};
 float sinkSpeed{0.0f};
 float riseSpeed{0.0f};
 bool damageOnContact{false};
};

struct BlockPhysicsProfile {
 BlockMovementPhysics movement;
 static BlockPhysicsProfile Solid();
 static BlockPhysicsProfile FromPreset(const std::string& preset);
};

enum class BlockRenderStyle {
 UCube,
 Fluid,
 Cross,
};

struct FluidViewProfile {
 glm::vec3 fogColor{0.02f, 0.12f, 0.22f};
 float fogStart{0.0f};
 float fogEnd{24.0f};
 float fogMinBlend{0.0f};
 glm::vec3 overlayColor{0.0f};
 float overlayAlpha{0.0f};
};

struct BlockRenderProfile {
 bool transparent{false};
 bool doubleSided{false};
 BlockRenderStyle style{BlockRenderStyle::UCube};
 FluidViewProfile fluidView;
};

struct BlockDefinition {
 std::string name;
 BlockId id{BLOCK_AIR};
 BlockAnimationSpec animation;
 BlockPhysicsProfile physics;
 BlockRenderProfile render;
 std::vector<std::string> types;
};

BlockAnimationSpec ParseAnimationFromJson(const nlohmann::json& j);
BlockPhysicsProfile ParsePhysicsFromJson(const nlohmann::json& j);
BlockRenderProfile ParseRenderFromJson(const nlohmann::json& j);
void ApplyRenderPresetDefaults(BlockRenderProfile& render, const std::string& physicsPreset);

} // namespace cutum

#endif

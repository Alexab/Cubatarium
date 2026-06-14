#include "BlockDefinition.h"
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>

namespace cutum {

BlockPhysicsProfile BlockPhysicsProfile::Solid()
{
 BlockPhysicsProfile p;
 p.movement.occupancy = 1.0f;
 return p;
}

BlockPhysicsProfile BlockPhysicsProfile::FromPreset(const std::string& preset)
{
 BlockPhysicsProfile p = Solid();
 if (preset == "water") {
  p.movement.occupancy = 0.0f;
  p.movement.dragHorizontal = 0.55f;
  p.movement.dragVertical = 0.35f;
  p.movement.sinkSpeed = 1.2f;
  p.movement.riseSpeed = 2.5f;
 } else if (preset == "lava") {
  p.movement.occupancy = 0.0f;
  p.movement.dragHorizontal = 0.65f;
  p.movement.dragVertical = 0.45f;
  p.movement.sinkSpeed = 0.8f;
  p.movement.riseSpeed = 1.0f;
  p.movement.damageOnContact = true;
 } else if (preset == "fire") {
  p.movement.occupancy = 0.0f;
 } else if (preset == "slime") {
  p.movement.occupancy = 1.0f;
  p.movement.dragHorizontal = 0.8f;
  p.movement.dragVertical = 0.8f;
 }
 return p;
}

BlockAnimationSpec ParseAnimationFromJson(const nlohmann::json& j)
{
 BlockAnimationSpec spec;
 if (!j.is_object()) {
  return spec;
 }
 spec.frameCount = std::max(1, j.value("frame_count", 1));
 spec.frametimeTicks = std::max(1, j.value("frametime", 2));
 spec.interpolate = j.value("interpolate", false);
 return spec;
}

BlockPhysicsProfile ParsePhysicsFromJson(const nlohmann::json& j)
{
 if (!j.is_object()) {
  return BlockPhysicsProfile::Solid();
 }
 if (j.contains("preset") && j["preset"].is_string()) {
  BlockPhysicsProfile p = BlockPhysicsProfile::FromPreset(j["preset"].get<std::string>());
  if (j.contains("movement") && j["movement"].is_object()) {
   const auto& m = j["movement"];
   if (m.contains("occupancy")) {
    p.movement.occupancy = m["occupancy"].get<float>();
   }
   if (m.contains("drag_horizontal")) {
    p.movement.dragHorizontal = m["drag_horizontal"].get<float>();
   }
   if (m.contains("drag_vertical")) {
    p.movement.dragVertical = m["drag_vertical"].get<float>();
   }
   if (m.contains("sink_speed")) {
    p.movement.sinkSpeed = m["sink_speed"].get<float>();
   }
   if (m.contains("rise_speed")) {
    p.movement.riseSpeed = m["rise_speed"].get<float>();
   }
   if (m.contains("damage_on_contact")) {
    p.movement.damageOnContact = m["damage_on_contact"].get<bool>();
   }
  }
  return p;
 }
 BlockPhysicsProfile p = BlockPhysicsProfile::Solid();
 if (j.contains("movement") && j["movement"].is_object()) {
  const auto& m = j["movement"];
  if (m.contains("occupancy")) {
   p.movement.occupancy = m["occupancy"].get<float>();
  }
  if (m.contains("drag_horizontal")) {
   p.movement.dragHorizontal = m["drag_horizontal"].get<float>();
  }
  if (m.contains("drag_vertical")) {
   p.movement.dragVertical = m["drag_vertical"].get<float>();
  }
  if (m.contains("sink_speed")) {
   p.movement.sinkSpeed = m["sink_speed"].get<float>();
  }
  if (m.contains("rise_speed")) {
   p.movement.riseSpeed = m["rise_speed"].get<float>();
  }
  if (m.contains("damage_on_contact")) {
   p.movement.damageOnContact = m["damage_on_contact"].get<bool>();
  }
 }
 return p;
}

static FluidViewProfile FluidViewFromPreset(const std::string& preset)
{
 FluidViewProfile v;
 if (preset == "water") {
  v.fogColor = glm::vec3(0.05f, 0.15f, 0.35f);
  v.fogStart = 0.0f;
  v.fogEnd = 9.0f;
  v.fogMinBlend = 0.5f;
 } else if (preset == "lava") {
  v.fogColor = glm::vec3(0.45f, 0.12f, 0.02f);
  v.fogStart = 0.0f;
  v.fogEnd = 7.0f;
  v.fogMinBlend = 0.45f;
 } else if (preset == "fire") {
  v.overlayColor = glm::vec3(1.0f, 0.45f, 0.05f);
  v.overlayAlpha = 0.55f;
 }
 return v;
}

BlockRenderProfile ParseRenderFromJson(const nlohmann::json& j)
{
 BlockRenderProfile r;
 if (!j.is_object()) {
  return r;
 }
 r.transparent = j.value("transparent", false);
 r.doubleSided = j.value("double_sided", false);
 if (j.contains("style") && j["style"].is_string()) {
  const std::string style = j["style"].get<std::string>();
  if (style == "fluid") {
   r.style = BlockRenderStyle::Fluid;
  } else if (style == "cross") {
   r.style = BlockRenderStyle::Cross;
  }
 }
 if (j.contains("fog_color") && j["fog_color"].is_array() && j["fog_color"].size() >= 3) {
  r.fluidView.fogColor = glm::vec3(
      j["fog_color"][0].get<float>(),
      j["fog_color"][1].get<float>(),
      j["fog_color"][2].get<float>());
 }
 if (j.contains("fog_start")) {
  r.fluidView.fogStart = j["fog_start"].get<float>();
 }
 if (j.contains("fog_end")) {
  r.fluidView.fogEnd = j["fog_end"].get<float>();
 }
 if (j.contains("overlay_color") && j["overlay_color"].is_array() && j["overlay_color"].size() >= 3) {
  r.fluidView.overlayColor = glm::vec3(
      j["overlay_color"][0].get<float>(),
      j["overlay_color"][1].get<float>(),
      j["overlay_color"][2].get<float>());
 }
 if (j.contains("overlay_alpha")) {
  r.fluidView.overlayAlpha = j["overlay_alpha"].get<float>();
 }
 return r;
}

void ApplyRenderPresetDefaults(BlockRenderProfile& render, const std::string& physicsPreset)
{
 if (physicsPreset == "water" || physicsPreset == "lava") {
  render.transparent = true;
  if (render.style == BlockRenderStyle::UCube) {
   render.style = BlockRenderStyle::Fluid;
  }
  const FluidViewProfile preset = FluidViewFromPreset(physicsPreset);
  if (render.fluidView.fogEnd >= 24.0f - 1e-3f) {
   render.fluidView = preset;
  } else {
   render.fluidView.fogMinBlend = preset.fogMinBlend;
   render.fluidView.fogStart = preset.fogStart;
   if (render.fluidView.fogEnd > preset.fogEnd + 1.0f) {
    render.fluidView.fogEnd = preset.fogEnd;
   }
  }
 } else if (physicsPreset == "fire") {
  render.transparent = true;
  if (render.style == BlockRenderStyle::UCube) {
   render.style = BlockRenderStyle::Cross;
  }
  if (render.fluidView.overlayAlpha <= 0.0f) {
   render.fluidView = FluidViewFromPreset("fire");
  }
 }
}

} // namespace cutum

#include "Blocks/BlockDefinition.h"
#include <algorithm>
#include <array>
#include <glm/glm.hpp>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

BlockPhysicsProfile BlockPhysicsProfile::Solid()
{
  BlockPhysicsProfile p;
  p.Movement.Occupancy = 1.0f;
  return p;
}

BlockPhysicsProfile BlockPhysicsProfile::FromPreset(const std::string &preset)
{
  BlockPhysicsProfile p = Solid();
  if (preset == "water")
  {
    p.Movement.Occupancy = 0.0f;
    p.Movement.DragHorizontal = 0.55f;
    p.Movement.DragVertical = 0.35f;
    p.Movement.SinkSpeed = 1.2f;
    p.Movement.RiseSpeed = 2.5f;
  }
  else if (preset == "lava")
  {
    p.Movement.Occupancy = 0.0f;
    p.Movement.DragHorizontal = 0.65f;
    p.Movement.DragVertical = 0.45f;
    p.Movement.SinkSpeed = 0.8f;
    p.Movement.RiseSpeed = 1.0f;
    p.Movement.DamageOnContact = true;
  }
  else if (preset == "fire")
  {
    p.Movement.Occupancy = 0.0f;
  }
  else if (preset == "slime")
  {
    p.Movement.Occupancy = 1.0f;
    p.Movement.DragHorizontal = 0.8f;
    p.Movement.DragVertical = 0.8f;
  }
  return p;
}

BlockAnimationSpec ParseAnimationFromJson(const nlohmann::json &j)
{
  BlockAnimationSpec spec;
  if (!j.is_object())
  {
    return spec;
  }
  spec.FrameCount = std::max(1, j.value("frame_count", 1));
  spec.FrametimeTicks = std::max(1, j.value("frametime", 2));
  spec.Interpolate = j.value("interpolate", false);
  return spec;
}

BlockPhysicsProfile ParsePhysicsFromJson(const nlohmann::json &j)
{
  if (!j.is_object())
  {
    return BlockPhysicsProfile::Solid();
  }
  if (j.contains("preset") && j["preset"].is_string())
  {
    BlockPhysicsProfile p =
        BlockPhysicsProfile::FromPreset(j["preset"].get<std::string>());
    if (j.contains("movement") && j["movement"].is_object())
    {
      const auto &m = j["movement"];
      if (m.contains("occupancy"))
      {
        p.Movement.Occupancy = m["occupancy"].get<float>();
      }
      if (m.contains("drag_horizontal"))
      {
        p.Movement.DragHorizontal = m["drag_horizontal"].get<float>();
      }
      if (m.contains("drag_vertical"))
      {
        p.Movement.DragVertical = m["drag_vertical"].get<float>();
      }
      if (m.contains("sink_speed"))
      {
        p.Movement.SinkSpeed = m["sink_speed"].get<float>();
      }
      if (m.contains("rise_speed"))
      {
        p.Movement.RiseSpeed = m["rise_speed"].get<float>();
      }
      if (m.contains("damage_on_contact"))
      {
        p.Movement.DamageOnContact = m["damage_on_contact"].get<bool>();
      }
    }
    return p;
  }
  BlockPhysicsProfile p = BlockPhysicsProfile::Solid();
  if (j.contains("movement") && j["movement"].is_object())
  {
    const auto &m = j["movement"];
    if (m.contains("occupancy"))
    {
      p.Movement.Occupancy = m["occupancy"].get<float>();
    }
    if (m.contains("drag_horizontal"))
    {
      p.Movement.DragHorizontal = m["drag_horizontal"].get<float>();
    }
    if (m.contains("drag_vertical"))
    {
      p.Movement.DragVertical = m["drag_vertical"].get<float>();
    }
    if (m.contains("sink_speed"))
    {
      p.Movement.SinkSpeed = m["sink_speed"].get<float>();
    }
    if (m.contains("rise_speed"))
    {
      p.Movement.RiseSpeed = m["rise_speed"].get<float>();
    }
    if (m.contains("damage_on_contact"))
    {
      p.Movement.DamageOnContact = m["damage_on_contact"].get<bool>();
    }
  }
  return p;
}

static FluidViewProfile FluidViewFromPreset(const std::string &preset)
{
  FluidViewProfile v;
  if (preset == "water")
  {
    v.FogColor = glm::vec3(0.05f, 0.15f, 0.35f);
    v.FogStart = 0.0f;
    v.FogEnd = 9.0f;
    v.FogMinBlend = 0.5f;
  }
  else if (preset == "lava")
  {
    v.FogColor = glm::vec3(0.45f, 0.12f, 0.02f);
    v.FogStart = 0.0f;
    v.FogEnd = 7.0f;
    v.FogMinBlend = 0.45f;
  }
  else if (preset == "fire")
  {
    v.OverlayColor = glm::vec3(1.0f, 0.45f, 0.05f);
    v.OverlayAlpha = 0.55f;
  }
  return v;
}

BlockRenderProfile ParseRenderFromJson(const nlohmann::json &j)
{
  BlockRenderProfile r;
  if (!j.is_object())
  {
    return r;
  }
  r.Transparent = j.value("transparent", false);
  r.DoubleSided = j.value("double_sided", false);
  if (j.contains("style") && j["style"].is_string())
  {
    const std::string style = j["style"].get<std::string>();
    if (style == "fluid")
    {
      r.Style = BlockRenderStyle::Fluid;
    }
    else if (style == "cross")
    {
      r.Style = BlockRenderStyle::Cross;
    }
  }
  if (j.contains("fog_color") && j["fog_color"].is_array() &&
      j["fog_color"].size() >= 3)
  {
    r.FluidView.FogColor = glm::vec3(j["fog_color"][0].get<float>(),
                                     j["fog_color"][1].get<float>(),
                                     j["fog_color"][2].get<float>());
  }
  if (j.contains("fog_start"))
  {
    r.FluidView.FogStart = j["fog_start"].get<float>();
  }
  if (j.contains("fog_end"))
  {
    r.FluidView.FogEnd = j["fog_end"].get<float>();
  }
  if (j.contains("overlay_color") && j["overlay_color"].is_array() &&
      j["overlay_color"].size() >= 3)
  {
    r.FluidView.OverlayColor = glm::vec3(j["overlay_color"][0].get<float>(),
                                         j["overlay_color"][1].get<float>(),
                                         j["overlay_color"][2].get<float>());
  }
  if (j.contains("overlay_alpha"))
  {
    r.FluidView.OverlayAlpha = j["overlay_alpha"].get<float>();
  }
  return r;
}

void ApplyRenderPresetDefaults(BlockRenderProfile &Render,
                               const std::string &physicsPreset)
{
  if (physicsPreset == "water" || physicsPreset == "lava")
  {
    Render.Transparent = true;
    if (Render.Style == BlockRenderStyle::UCube)
    {
      Render.Style = BlockRenderStyle::Fluid;
    }
    const FluidViewProfile preset = FluidViewFromPreset(physicsPreset);
    if (Render.FluidView.FogEnd >= 24.0f - 1e-3f)
    {
      Render.FluidView = preset;
    }
    else
    {
      Render.FluidView.FogMinBlend = preset.FogMinBlend;
      Render.FluidView.FogStart = preset.FogStart;
      if (Render.FluidView.FogEnd > preset.FogEnd + 1.0f)
      {
        Render.FluidView.FogEnd = preset.FogEnd;
      }
    }
  }
  else if (physicsPreset == "fire")
  {
    Render.Transparent = true;
    if (Render.Style == BlockRenderStyle::UCube)
    {
      Render.Style = BlockRenderStyle::Cross;
    }
    if (Render.FluidView.OverlayAlpha <= 0.0f)
    {
      Render.FluidView = FluidViewFromPreset("fire");
    }
  }
}

bool IsReservedBlockName(const std::string &name)
{
  return name == "__missing__" || name == "__air__";
}

ParsedBlockJson ParseBlockFromJson(const nlohmann::json &j, bool warnLegacyId)
{
  ParsedBlockJson out;
  if (!j.is_object())
  {
    return out;
  }
  out.Definition.Name = j.value("name", "");
  if (out.Definition.Name.empty() || IsReservedBlockName(out.Definition.Name))
  {
    return out;
  }
  if (j.contains("id"))
  {
    if (warnLegacyId)
    {
      std::cerr << "ParseBlockFromJson: ignoring legacy id field for block '"
                << out.Definition.Name << "'" << std::endl;
    }
  }
  if (j.contains("animation"))
  {
    out.Definition.Animation = ParseAnimationFromJson(j["animation"]);
  }
  if (j.contains("physics"))
  {
    out.Definition.Physics = ParsePhysicsFromJson(j["physics"]);
  }
  else
  {
    out.Definition.Physics = BlockPhysicsProfile::Solid();
  }
  if (j.contains("render"))
  {
    out.Definition.Render = ParseRenderFromJson(j["render"]);
  }
  if (j.contains("types") && j["types"].is_array())
  {
    for (const auto &t : j["types"])
    {
      if (t.is_string())
      {
        out.Definition.Types.push_back(t.get<std::string>());
      }
    }
  }
  if (j.contains("physics") && j["physics"].is_object() &&
      j["physics"].contains("preset") && j["physics"]["preset"].is_string())
  {
    ApplyRenderPresetDefaults(out.Definition.Render,
                              j["physics"]["preset"].get<std::string>());
  }
  const auto textures = j.value("textures", nlohmann::json::array());
  if (!textures.is_array() || textures.size() < 6)
  {
    return out;
  }
  for (int i = 0; i < 6; ++i)
  {
    if (!textures[i].is_string())
    {
      return out;
    }
    out.TextureStems[static_cast<size_t>(i)] = textures[i].get<std::string>();
  }
  out.Valid = true;
  return out;
}

} // namespace cutum

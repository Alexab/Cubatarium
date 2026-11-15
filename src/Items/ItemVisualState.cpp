#include "Game/Inventory/InventoryTypes.h"
#include "Items/ItemPresentationMath.h"
#include "Items/ItemRenderAsset.h"
#include <algorithm>
#include <glm/gtc/quaternion.hpp>
#include <limits>

namespace cutum
{
namespace
{
glm::mat4 TurnBetween(const glm::vec3 &from, const glm::vec3 &to)
{
  const float d = std::clamp(glm::dot(from, to), -1.f, 1.f);
  if (d < -.999999f)
  {
    const auto axis = glm::normalize(
        glm::cross(from, std::abs(from.x) < .9f ? glm::vec3(1, 0, 0)
                                                : glm::vec3(0, 1, 0)));
    return glm::mat4_cast(glm::quat(0.f, axis));
  }
  return glm::mat4_cast(
      glm::normalize(glm::quat(1.f + d, glm::cross(from, to))));
}
} // namespace

ItemVisualInputs MakeItemVisualInputs(const InventoryEntryRef *entry,
                                      float bowCharge, float usePhase,
                                      bool hasAmmo)
{
  ItemVisualInputs inputs;
  if (!entry || entry->empty)
    return inputs;
  inputs.ContentsUnits = std::max(0, entry->contentsUnits);
  inputs.HasWater = entry->contentsId == "water";
  if (entry->broken)
    return inputs;
  inputs.BowCharge = std::clamp(bowCharge, 0.f, 1.f);
  inputs.UsePhase = std::clamp(usePhase, 0.f, 1.f);
  inputs.HasAmmo = hasAmmo;
  return inputs;
}
bool ItemNodeVisible(const ItemRenderAsset &asset, const ItemVisualState &state,
                     size_t node)
{
  if (state.HiddenNodes.empty() && state.VisibleRoots.empty())
    return true;
  bool selected = state.VisibleRoots.empty();
  for (int i = int(node); i >= 0; i = asset.Nodes[size_t(i)].Parent)
  {
    if (state.HiddenNodes.count(asset.Nodes[size_t(i)].Name))
      return false;
    selected =
        selected || state.VisibleRoots.count(asset.Nodes[size_t(i)].Name);
  }
  return selected;
}
ItemVisualState EvaluateItemVisualState(const ItemRenderAsset &asset,
                                        const ItemVisualInputs &inputs)
{
  ItemVisualState state;
  for (const auto &b : asset.Bindings)
  {
    if (b.Span)
      continue;
    float value = 0;
    switch (b.Channel)
    {
    case ItemVisualChannel::BowCharge:
      value = inputs.BowCharge;
      break;
    case ItemVisualChannel::BowLoaded:
      value = inputs.HasAmmo ? inputs.BowCharge : 0.f;
      break;
    case ItemVisualChannel::UsePhase:
      value = inputs.UsePhase;
      break;
    case ItemVisualChannel::FillUnits:
      value = inputs.HasWater ? float(inputs.ContentsUnits) : 0.f;
      break;
    }
    if (!std::isfinite(value))
      value = 0;
    const float t =
        std::clamp((value - b.Minimum) / (b.Maximum - b.Minimum), 0.f, 1.f);
    if (b.VisibleAbove >= 0 && t <= b.VisibleAbove)
      state.HiddenNodes.insert(asset.Nodes[b.Node].Name);
    const float weight = b.Pulse ? std::sin(t * 3.14159265359f) : t;
    state.NodeTransforms[asset.Nodes[b.Node].Name] =
        glm::translate(glm::mat4(1),
                       glm::mix(b.TranslationFrom, b.TranslationTo, weight)) *
        ItemEulerMatrix(glm::mix(b.RotationFrom, b.RotationTo, weight)) *
        glm::scale(glm::mat4(1), glm::mix(b.ScaleFrom, b.ScaleTo, weight));
  }
  const auto transforms = ResolveItemNodeTransforms(asset, state);
  for (const auto &b : asset.Bindings)
    if (b.Span)
    {
      const auto restA = glm::vec3(asset.Nodes[b.StartNode].Transform *
                                   glm::vec4(b.StartPoint, 1));
      const auto restB = glm::vec3(asset.Nodes[b.EndNode].Transform *
                                   glm::vec4(b.EndPoint, 1));
      const auto a =
          glm::vec3(transforms[b.StartNode] * glm::vec4(b.StartPoint, 1));
      const auto z =
          glm::vec3(transforms[b.EndNode] * glm::vec4(b.EndPoint, 1));
      const auto direction = glm::normalize(restB - restA);
      const auto newDirection = z - a;
      const float length = glm::length(newDirection);
      if (length < 1e-6f)
      {
        state.HiddenNodes.insert(asset.Nodes[b.Node].Name);
        continue;
      }
      const auto align = TurnBetween(glm::vec3(0, 1, 0), direction);
      const auto stretch =
          align *
          glm::scale(glm::mat4(1),
                     glm::vec3(1, length / glm::length(restB - restA), 1)) *
          glm::inverse(align);
      const auto turn = TurnBetween(direction, newDirection / length);
      const auto global = glm::translate(glm::mat4(1), (a + z) * .5f) * turn *
                          stretch *
                          glm::translate(glm::mat4(1), -(restA + restB) * .5f) *
                          asset.Nodes[b.Node].Transform;
      const int parent = asset.Nodes[b.Node].Parent;
      state.NodeTransforms[asset.Nodes[b.Node].Name] =
          glm::inverse(asset.Nodes[b.Node].Local) *
          (parent < 0 ? glm::mat4(1)
                      : glm::inverse(transforms[size_t(parent)])) *
          global;
    }
  return state;
}
std::pair<glm::vec3, glm::vec3> ItemVisualBounds(const ItemRenderAsset &asset,
                                                 const ItemVisualState &state)
{
  if (state.NodeTransforms.empty() && state.HiddenNodes.empty() &&
      state.VisibleRoots.empty())
    return {asset.Low, asset.High};
  if (state.NodeTransforms.empty() && state.HiddenNodes.empty() &&
      state.VisibleRoots.size() == 1)
  {
    const auto cached =
        asset.StaticWearSections.find(*state.VisibleRoots.begin());
    if (cached != asset.StaticWearSections.end())
      return {cached->second.Low, cached->second.High};
  }
  const auto transforms = ResolveItemNodeTransforms(asset, state);
  glm::vec3 low(std::numeric_limits<float>::max()),
      high(-std::numeric_limits<float>::max());
  bool any = false;
  for (const auto &p : asset.Primitives)
  {
    if (!ItemNodeVisible(asset, state, p.Node))
      continue;
    for (int x : {0, 1})
      for (int y : {0, 1})
        for (int z : {0, 1})
        {
          const auto v = glm::vec3(transforms[p.Node] *
                                   glm::vec4(x ? p.High.x : p.Low.x,
                                             y ? p.High.y : p.Low.y,
                                             z ? p.High.z : p.Low.z, 1));
          low = glm::min(low, v);
          high = glm::max(high, v);
          any = true;
        }
  }
  return any ? std::make_pair(low, high)
             : std::make_pair(glm::vec3(0), glm::vec3(0));
}
} // namespace cutum

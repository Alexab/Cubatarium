#pragma once
#include "Creatures/Core/HumanAppearance.h"
#include "Creatures/Visual/CreatureVisual.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Gui/Preview/CreaturePreviewRenderer.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ItemDrawRequest.h"
#include "Render/GlIncludes.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <nlohmann/json.hpp>

namespace cutum
{
inline bool SaveItemQaTexture(unsigned int texture,
                              const std::filesystem::path &path, int width,
                              int height);
inline bool HeadwearBlocksSegment(glm::vec3 from, glm::vec3 to, glm::vec3 a,
                                  glm::vec3 b, glm::vec3 c)
{
  const auto direction = to - from, edge1 = b - a, edge2 = c - a;
  const auto p = glm::cross(direction, edge2);
  const float determinant = glm::dot(edge1, p);
  if (std::abs(determinant) < 1e-9f)
    return false;
  const float inverse = 1.f / determinant;
  const auto offset = from - a;
  const float u = glm::dot(offset, p) * inverse;
  if (u < 0 || u > 1)
    return false;
  const auto q = glm::cross(offset, edge1);
  const float v = glm::dot(direction, q) * inverse;
  if (v < 0 || u + v > 1)
    return false;
  const float distance = glm::dot(edge2, q) * inverse;
  return distance > .0001f && distance < 1.f;
}
inline void RunItemHeadwearQa(const ItemDefinition &definition,
                              UCreaturePreviewRenderer &preview,
                              const std::filesystem::path &frames,
                              nlohmann::json &report)
{
  if (std::find(definition.Armor.Slots.begin(), definition.Armor.Slots.end(),
                "head") == definition.Armor.Slots.end())
    return;
  auto check = [&](const std::string &name, bool ok)
  {
    report["checks"].push_back({{"name", name}, {"passed", ok}});
    if (!ok)
      report["passed"] = false;
  };
  struct Cleanup
  {
    UCreaturePreviewRenderer &Preview;
    ~Cleanup()
    {
      Preview.SetSkeletonPoseObserver({});
      Preview.SetItemSurfaceObserver({});
    }
  } cleanup{preview};
  std::array<WornArmorPreviewSlot, kWornArmorSlotCount> armor;
  armor[0].ItemId = definition.Id;
  for (const char *body : HumanAppearance::Bodies)
  {
    glm::mat4 head(1);
    glm::vec3 low(0), high(0);
    bool hasHead = false;
    std::array<glm::vec3, 12> eyes{}, starts{};
    std::array<bool, 12> blocked{};
    int draws = 0;
    preview.SetSkeletonPoseObserver(
        [&](const IUCreatureVisual &visual)
        {
          hasHead = visual.GetWorldBoneTransform("head", head) &&
                    visual.GetBoneLocalBounds("head", low, high);
          if (!hasHead)
            return;
          for (int side = 0; side < 2; ++side)
            for (int column = 0; column < 2; ++column)
              for (int row = 0; row < 3; ++row)
              {
                // Actual atlas eye rectangles: Y 28..29, |X| 1..3;
                // head spans Y 24..32 and X -4..4 logical texels.
                const int index = side * 6 + column * 3 + row;
                const float x =
                    .5f + (side ? 1.f : -1.f) * (1.5f + column) / 8.f;
                const float y = .5f + (.1f + row * .4f) / 8.f;
                const auto eye =
                    glm::vec3(glm::mix(low.x, high.x, x),
                              glm::mix(low.y, high.y, y), low.z - .005f);
                eyes[index] = glm::vec3(head * glm::vec4(eye, 1));
                starts[index] = glm::vec3(
                    head * glm::vec4(eye + glm::vec3(0, 0, -.35f), 1));
              }
        });
    preview.SetItemSurfaceObserver(
        [&](const ItemDrawRequest &request)
        {
          if (request.Path.parent_path().filename() != definition.Id)
            return;
          ++draws;
          const auto nodes =
              ResolveItemNodeTransforms(*request.Asset, request.State);
          for (const auto &primitive : request.Asset->Primitives)
          {
            if (!ItemNodeVisible(*request.Asset, request.State, primitive.Node))
              continue;
            const auto model = request.Model * nodes[primitive.Node];
            auto point = [&](unsigned index)
            {
              const auto i = size_t(index) * 8;
              return glm::vec3(model * glm::vec4(primitive.Vertices[i],
                                                 primitive.Vertices[i + 1],
                                                 primitive.Vertices[i + 2], 1));
            };
            for (size_t triangle = 0; triangle + 2 < primitive.Indices.size();
                 triangle += 3)
              for (int side = 0; side < 12; ++side)
                blocked[side] = blocked[side] ||
                                HeadwearBlocksSegment(
                                    starts[side], eyes[side],
                                    point(primitive.Indices[triangle]),
                                    point(primitive.Indices[triangle + 1]),
                                    point(primitive.Indices[triangle + 2]));
          }
        });
    HumanAppearance appearance;
    appearance.bodyVariant = body;
    const auto texture =
        preview.RenderToUniqueTexture("human", "", 512, 0, 12, 0, false, &armor,
                                      nullptr, nullptr, &appearance);
    preview.SetSkeletonPoseObserver({});
    preview.SetItemSurfaceObserver({});
    const auto label = definition.Id + " eye clearance " + body;
    check(label, hasHead && draws > 0 &&
                     std::none_of(blocked.begin(), blocked.end(),
                                  [](bool hit) { return hit; }));
    check(label + " capture",
          SaveItemQaTexture(texture,
                            frames / (definition.Id + "-eyes-" + body + ".png"),
                            512, 512));
    report["headwear_eye_clearance"][definition.Id][body] = {
        {"head_bounds_low", {low.x, low.y, low.z}},
        {"head_bounds_high", {high.x, high.y, high.z}},
        {"eye_height_fractions", {.5125, .5625, .6125}},
        {"probes", 12},
        {"left_blocked", std::any_of(blocked.begin(), blocked.begin() + 6,
                                     [](bool hit) { return hit; })},
        {"right_blocked", std::any_of(blocked.begin() + 6, blocked.end(),
                                      [](bool hit) { return hit; })},
        {"armor_draws", draws}};
    glDeleteTextures(1, &texture);
  }
}
} // namespace cutum

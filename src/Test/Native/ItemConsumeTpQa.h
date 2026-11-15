#pragma once
#include "Creatures/Core/HumanAppearance.h"
#include "Creatures/Visual/CreatureVisual.h"
#include "Gui/Preview/CreaturePreviewRenderer.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ItemDrawRequest.h"
#include "Render/GlIncludes.h"
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <vector>

namespace cutum
{
// SaveItemQaTexture is provided by the native presentation fixture.
inline bool SaveItemQaTexture(unsigned int texture,
                              const std::filesystem::path &path, int width,
                              int height);
inline void RunItemConsumeTpQa(const ItemDefinition &definition,
                               UCreaturePreviewRenderer &preview,
                               const std::filesystem::path &frames,
                               nlohmann::json &report)
{
  if (definition.Use.Action != ItemUseActionKind::Eat &&
      definition.Use.Action != ItemUseActionKind::Drink)
    return;
  auto check = [&](const std::string &name, bool passed)
  {
    report["checks"].push_back({{"name", name}, {"passed", passed}});
    if (!passed)
      report["passed"] = false;
  };
  auto bytes = [](const std::filesystem::path &path)
  {
    std::ifstream file(path, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(file), {});
  };
  struct Cleanup
  {
    UCreaturePreviewRenderer &Preview;
    ~Cleanup()
    {
      Preview.SetItemSurfaceObserver({});
      Preview.SetSkeletonPoseObserver({});
    }
  } cleanup{preview};
  for (const char *body : HumanAppearance::Bodies)
    for (bool walk : {false, true})
      for (int yaw : {0, 90, 180})
      {
        HumanAppearance appearance;
        appearance.bodyVariant = body;
        const auto label = definition.Id + "-consume-tp-" + body + "-" +
                           (walk ? "walk-" : "still-") + std::to_string(yaw);
        std::array<std::filesystem::path, 4> images;
        std::array<float, 4> heights{};
        for (int step = 0; step < 4; ++step)
        {
          ItemVisualInputs inputs;
          inputs.UsePhase = step == 1 ? .5f : step == 2 ? 1.f : 0.f;
          if (definition.Use.Action == ItemUseActionKind::Drink)
          {
            inputs.HasWater = true;
            inputs.ContentsUnits = 1;
          }
          size_t draws = 0;
          bool framed = true;
          bool hasHead = false, contact = step == 0 || step == 3;
          glm::mat4 head(1.f);
          glm::vec3 headLow, headHigh;
          preview.SetSkeletonPoseObserver(
              [&](const IUCreatureVisual &visual)
              {
                hasHead = visual.GetWorldBoneTransform("head", head) &&
                          visual.GetBoneLocalBounds("head", headLow, headHigh);
              });
          preview.SetItemSurfaceObserver(
              [&](const ItemDrawRequest &request)
              {
                if (request.Path.parent_path().filename() != definition.Id)
                  return;
                ++draws;
                const auto palm =
                    request.Model * glm::vec4(request.Asset->Grip.RightGrip, 1);
                const auto headLocalPalm = glm::vec3(glm::inverse(head) * palm);
                if (step == 1 || step == 2)
                  contact = hasHead && headLocalPalm.z < headLow.z - .02f &&
                            glm::distance(headLocalPalm,
                                          glm::vec3(0, .16f, -.28f)) < .35f;
                heights[step] =
                    glm::vec3(request.Model *
                              glm::vec4((request.Low + request.High) * .5f, 1))
                        .y;
                for (int x : {0, 1})
                  for (int y : {0, 1})
                    for (int z : {0, 1})
                    {
                      const auto clip =
                          request.ViewProjection * request.Model *
                          glm::vec4(x ? request.High.x : request.Low.x,
                                    y ? request.High.y : request.Low.y,
                                    z ? request.High.z : request.Low.z, 1);
                      framed = framed && clip.w > 0 &&
                               std::abs(clip.x) <= clip.w &&
                               std::abs(clip.y) <= clip.w;
                    }
              });
          const auto texture = preview.RenderToUniqueTexture(
              "human", "", 512, float(yaw), 12, walk ? .35f : 0.f, walk,
              nullptr, &definition.Id, nullptr, &appearance, &inputs);
          preview.SetItemSurfaceObserver({});
          preview.SetSkeletonPoseObserver({});
          images[step] = frames / (label + "-" + std::to_string(step) + ".png");
          check(label + " capture " + std::to_string(step),
                SaveItemQaTexture(texture, images[step], 512, 512));
          check(label + " actual item framed " + std::to_string(step),
                draws == 1 && framed);
          check(label + " contact outside face " + std::to_string(step),
                contact);
          glDeleteTextures(1, &texture);
        }
        check(label + " raises item in world space",
              heights[2] > heights[0] + .1f);
        check(label + " pose changes", bytes(images[0]) != bytes(images[1]));
        check(label + " hold contact persists",
              bytes(images[1]) == bytes(images[2]));
        check(label + " cancel restores pose",
              bytes(images[0]) == bytes(images[3]));
        report["consumption_tp"][label] = {{"item_center_y", heights}};
      }
}
} // namespace cutum

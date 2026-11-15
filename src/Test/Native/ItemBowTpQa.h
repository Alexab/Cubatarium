#pragma once
#include "Creatures/Core/HumanAppearance.h"
#include "Creatures/Visual/CreatureEquipmentFit.h"
#include "Creatures/Visual/CreatureVisual.h"
#include "Gui/Preview/CreaturePreviewRenderer.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ItemDrawRequest.h"
#include "Items/ItemPresentationMath.h"
#include "Render/GlIncludes.h"
#include <array>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>

namespace cutum
{
inline bool SaveItemQaTexture(unsigned int texture,
                              const std::filesystem::path &path, int width,
                              int height);
inline void RunItemBowTpQa(const ItemDefinition &definition,
                           UCreaturePreviewRenderer &preview,
                           const std::filesystem::path &frames,
                           nlohmann::json &report)
{
  if (!definition.Ranged.Enabled)
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
  const std::string shield = "wood_shield";
  for (const char *body : HumanAppearance::Bodies)
    for (bool walk : {false, true})
      for (int yaw : {0, 90, 180})
      {
        HumanAppearance appearance;
        appearance.bodyVariant = body;
        const auto label = definition.Id + "-bow-tp-" + body + "-" +
                           (walk ? "walk-" : "still-") + std::to_string(yaw);
        std::array<std::filesystem::path, 4> images;
        for (int step = 0; step < 4; ++step)
        {
          ItemVisualInputs inputs;
          inputs.BowCharge = step == 1 ? .5f : step == 2 ? 1.f : 0.f;
          inputs.HasAmmo = true;
          glm::mat4 head(1);
          glm::vec3 palms[2]{};
          bool skeleton = true, contact = false,
               support = inputs.BowCharge == 0;
          bool aimed = false;
          int bows = 0, shields = 0;
          float mainGap = 1e9f, supportGap = 0;
          preview.SetSkeletonPoseObserver(
              [&](const IUCreatureVisual &visual)
              {
                skeleton = visual.GetWorldBoneTransform("head", head);
                for (int hand = 0; hand < 2; ++hand)
                {
                  const char *name = hand ? "leftItem" : "rightItem";
                  glm::mat4 frame(1);
                  glm::vec3 low, high;
                  skeleton = visual.GetWorldBoneTransform(name, frame) &&
                             visual.GetBoneLocalBounds(name, low, high) &&
                             skeleton;
                  palms[hand] = glm::vec3(
                      frame *
                      glm::vec4(CreaturePalmGripPosition(low, high), 1));
                }
              });
          preview.SetItemSurfaceObserver(
              [&](const ItemDrawRequest &request)
              {
                const auto id = request.Path.parent_path().filename().string();
                if (id == shield)
                  ++shields;
                if (id != definition.Id)
                  return;
                ++bows;
                mainGap = glm::distance(
                    palms[0],
                    glm::vec3(request.Model *
                              glm::vec4(request.Asset->Grip.RightGrip, 1)));
                contact = skeleton && mainGap < .0001f;
                aimed = glm::dot(glm::normalize(
                                     glm::vec3(head * glm::vec4(0, 0, -1, 0))),
                                 glm::normalize(glm::vec3(
                                     request.Model * glm::vec4(0, 0, -1, 0)))) >
                        .99999f;
                if (inputs.BowCharge > 0 && ItemHasSupportGrip(*request.Asset))
                {
                  const auto nodes =
                      ResolveItemNodeTransforms(*request.Asset, request.State);
                  supportGap = glm::distance(
                      palms[1],
                      glm::vec3(
                          request.Model *
                          nodes[request.Asset->Grip.SupportGripIndex][3]));
                  support = skeleton && supportGap < .0001f;
                }
              });
          const auto texture = preview.RenderToUniqueTexture(
              "human", "", 512, float(yaw), 12, walk ? .35f : 0.f, walk,
              nullptr, &definition.Id, &shield, &appearance, &inputs);
          preview.SetItemSurfaceObserver({});
          preview.SetSkeletonPoseObserver({});
          images[step] = frames / (label + "-" + std::to_string(step) + ".png");
          check(label + " capture " + std::to_string(step),
                SaveItemQaTexture(texture, images[step], 512, 512));
          check(label + " loaded bow and main palm " + std::to_string(step),
                bows == 1 && contact && aimed);
          check(label + " actual string contact " + std::to_string(step),
                support);
          check(label + " shield occupancy " + std::to_string(step),
                shields == (inputs.BowCharge > 0 ? 0 : 1));
          report["bow_tp"][label][std::to_string(step)] = {
              {"main_grip_gap", mainGap},
              {"support_gap", supportGap},
              {"bow_draws", bows},
              {"shield_draws", shields}};
          glDeleteTextures(1, &texture);
        }
        check(label + " cancel restores pose",
              bytes(images[0]) == bytes(images[3]));
        check(label + " draw changes pose",
              bytes(images[1]) != bytes(images[2]));
      }
}
} // namespace cutum

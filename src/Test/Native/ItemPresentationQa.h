#pragma once
#include "App/Platform/IUPlatformPaths.h"
#include "Creatures/Core/HumanAppearance.h"
#include "Creatures/Visual/CreatureDrawQueue.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Gui/Preview/CreaturePreviewRenderer.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ItemDrawRequest.h"
#include "Items/ItemPresentationMath.h"
#include "Items/ItemSurfaceRenderer.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/GlIncludes.h"
#include "Render/Pipeline/GlStateScope.h"
#include "ThirdParty/stb_image_write.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace cutum
{
inline bool SaveItemQaTexture(unsigned texture,
                              const std::filesystem::path &path)
{
  if (!texture)
    return false;
  UGlStateScope state(kGlMaskIconFbo);
  unsigned fbo = 0;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture, 0);
  bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  if (ok)
  {
    std::vector<unsigned char> pixels(512 * 512 * 4);
    glReadPixels(0, 0, 512, 512, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (int y = 0; y < 256; ++y)
      std::swap_ranges(pixels.begin() + y * 512 * 4,
                       pixels.begin() + (y + 1) * 512 * 4,
                       pixels.begin() + (511 - y) * 512 * 4);
    std::filesystem::create_directories(path.parent_path());
    ok = stbi_write_png(path.string().c_str(), 512, 512, 4, pixels.data(),
                        512 * 4) != 0;
  }
  glDeleteFramebuffers(1, &fbo);
  return ok;
}

class UItemQaTarget
{
public:
  UItemQaTarget()
  {
    glGenTextures(1, &Texture);
    glBindTexture(GL_TEXTURE_2D, Texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 512, 512, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenFramebuffers(1, &Fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, Fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           Texture, 0);
    glGenRenderbuffers(1, &Depth);
    glBindRenderbuffer(GL_RENDERBUFFER, Depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, Depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      throw std::runtime_error("Cannot create item QA target");
  }
  ~UItemQaTarget()
  {
    glDeleteFramebuffers(1, &Fbo);
    glDeleteRenderbuffers(1, &Depth);
    glDeleteTextures(1, &Texture);
  }
  void Bind()
  {
    glBindFramebuffer(GL_FRAMEBUFFER, Fbo);
    glViewport(0, 0, 512, 512);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(.08f, .09f, .12f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }
  unsigned Texture{0}, Fbo{0}, Depth{0};
};
class UItemQaSink : public ICreatureDrawSink
{
public:
  explicit UItemQaSink(ItemSurfaceRenderer &renderer) : Renderer(renderer) {}
  ItemSurfaceRenderer &Renderer;
  ItemPresentationProfile Profile;
  int Draws{0};
  void DrawItemSurface(const ItemDrawRequest &r) override
  {
    ++Draws;
    Renderer.Draw(r.Asset, r.Path, r.ViewProjection, r.Model, r.State, Profile);
  }
  void DrawTexturedPart(const glm::mat4 &, unsigned, CreaturePartMesh) override
  {
  }
  void DrawBoneSkeletonMesh(const glm::mat4 &, unsigned,
                            const BoneSkeletonCubeMeshCpu &) override
  {
  }
  void DrawSkinnedMesh(
      const glm::mat4 &, unsigned, const GltfPrimitiveCpu &,
      const std::shared_ptr<const std::vector<glm::mat4>> &) override
  {
  }
  void DrawBoxWireframe(const glm::mat4 &, const glm::vec4 &) override {}
  void DrawBillboard(const glm::mat4 &, unsigned, const glm::vec4 &) override {}
};
inline void RunItemPresentationQa(const std::filesystem::path &path,
                                  const UItemDefinitionStorage &items,
                                  UCreaturePreviewRenderer &preview,
                                  UShaderManager &shaders)
{
  UGlStateScope state(kGlMaskIconFbo | GlStateBit::DepthMask |
                      GlStateBit::DepthFunc | GlStateBit::ColorMask);
  struct Restore
  {
    const UItemDefinitionStorage *Definitions;
    UCreaturePreviewRenderer &Preview;
    GLboolean Scissor;
    ~Restore()
    {
      WornEquipmentDrawer::SetItemDefinitions(Definitions);
      Preview.SetItemSurfaceObserver({});
      if (Scissor)
        glEnable(GL_SCISSOR_TEST);
      else
        glDisable(GL_SCISSOR_TEST);
    }
  } restore{WornEquipmentDrawer::ItemDefinitions(), preview,
            glIsEnabled(GL_SCISSOR_TEST)};
  WornEquipmentDrawer::SetItemDefinitions(&items);
  nlohmann::json report = {
      {"passed", true},
      {"checks", nlohmann::json::array()},
      {"scope", "synthetic native attachments and loose item submissions"},
      {"surface_ids", nlohmann::json::array()}};
  const auto frames = std::filesystem::path(path.string() + ".frames");
  auto check = [&](const std::string &name, bool passed)
  {
    report["checks"].push_back({{"name", name}, {"passed", passed}});
    if (!passed)
      report["passed"] = false;
  };
  ItemSurfaceRenderer renderer;
  check("item renderer initializes", renderer.Initialize(shaders));
  UItemQaTarget target;
  for (const auto &id : items.ListCatalogIds())
  {
    const auto *def = items.Get(id);
    const auto model = IUPlatformPaths::Current().AssetRoot() / def->ModelPath;
    const auto surface =
        FindItemSurfaceAsset(model, UItemDefinitionStorage::ContentRevision());
    if (!surface)
      continue;
    report["surface_ids"].push_back(id);
    for (const char *body : HumanAppearance::Bodies)
      for (int phase = 0; phase < 2; ++phase)
      {
        HumanAppearance appearance;
        appearance.bodyVariant = body;
        for (int hand = 0; hand < 2; ++hand)
        {
          int draws = 0;
          preview.SetItemSurfaceObserver(
              [&](const ItemDrawRequest &r)
              {
                if (r.Path.parent_path().filename() == id)
                  ++draws;
              });
          const std::string empty;
          const auto tex = preview.RenderToUniqueTexture(
              "human", "", 512, 30, 12, phase * .35f, phase != 0, nullptr,
              hand == 0 ? &id : &empty, hand == 1 ? &id : &empty, &appearance);
          const auto label = id + "-" + body + "-" +
                             (hand == 0 ? "right" : "left") + "-" +
                             std::to_string(phase);
          check(label + " draws item surface", draws == 1);
          check(label + " capture",
                SaveItemQaTexture(tex, frames / (label + ".png")));
          glDeleteTextures(1, &tex);
        }
        if (!def->Armor.Slots.empty())
        {
          std::array<WornArmorPreviewSlot, kWornArmorSlotCount> slots;
          const std::array<std::string, 4> names = {"head", "chest", "legs",
                                                    "feet"};
          for (size_t i = 0; i < names.size(); ++i)
            if (std::find(def->Armor.Slots.begin(), def->Armor.Slots.end(),
                          names[i]) != def->Armor.Slots.end())
              slots[i].ItemId = id;
          int draws = 0;
          preview.SetItemSurfaceObserver(
              [&](const ItemDrawRequest &r)
              {
                if (r.Path.parent_path().filename() == id)
                  ++draws;
              });
          const auto tex = preview.RenderToUniqueTexture(
              "human", "", 512, 30, 12, phase * .35f, phase != 0, &slots,
              nullptr, nullptr, &appearance);
          const auto label =
              id + "-" + body + "-armor-" + std::to_string(phase);
          check(label + " draws item surface", draws > 0);
          check(label + " capture",
                SaveItemQaTexture(tex, frames / (label + ".png")));
          glDeleteTextures(1, &tex);
        }
      }
    preview.SetItemSurfaceObserver({});
    const auto extent = surface->High - surface->Low;
    const auto focus = glm::vec3(0, extent.y * .5f, 0);
    const auto eye = focus + glm::normalize(glm::vec3(1, .7f, 1.4f)) *
                                 std::max(.15f, glm::length(extent)) * 1.7f;
    const auto vp = glm::perspective(glm::radians(45.f), 1.f, .01f, 100.f) *
                    glm::lookAt(eye, focus, glm::vec3(0, 1, 0));
    for (int light = 0; light < 4; ++light)
    {
      target.Bind();
      CreatureDrawQueue queue;
      WornEquipmentDrawer::SubmitLoose(
          queue, id, vp,
          glm::translate(glm::mat4(1),
                         glm::vec3(0, def->Physical.size.y * .5f, 0)),
          def->Physical.size, 0, true);
      UItemQaSink sink(renderer);
      sink.Profile.CameraPosition = eye;
      sink.Profile.LightIntensity = ItemWorldLightIntensity(
          light == 2 ? glm::vec2(0, 0) : glm::vec2(1, 0),
          light == 0 || light == 3 ? 1.f : 0.f, .2f, 1.f, .12f);
      sink.Profile.Fog = light == 3 ? .65f : 0.f;
      sink.Profile.FogColor = glm::vec3(.25f, .3f, .4f);
      queue.Flush(sink);
      const auto label = id + "-loose-" + std::to_string(light);
      check(label + " draws item surface", sink.Draws == 1);
      check(label + " capture",
            SaveItemQaTexture(target.Texture, frames / (label + ".png")));
    }
  }
  report["surface_count"] = report["surface_ids"].size();
  std::ofstream output(path);
  output << report.dump(2) << '\n';
}
} // namespace cutum

#pragma once
#include "App/Platform/IUPlatformPaths.h"
#include "Creatures/Core/HumanAppearance.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Visual/CreatureAppearance.h"
#include "Creatures/Visual/CreatureDrawQueue.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Gui/Preview/CreaturePreviewRenderer.h"
#include "Gui/Preview/ItemPreviewRenderer.h"
#include "Items/FpViewmodelRenderer.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ItemDrawRequest.h"
#include "Items/ItemPresentationMath.h"
#include "Items/ItemSurfaceRenderer.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/GlIncludes.h"
#include "Render/Pipeline/GlStateScope.h"
#include "ThirdParty/stb_image_write.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <stdexcept>

namespace cutum
{
inline bool SaveItemQaTexture(unsigned texture,
                              const std::filesystem::path &path,
                              int width = 512, int height = 512)
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
    std::vector<unsigned char> pixels(size_t(width) * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (int y = 0; y < height / 2; ++y)
      std::swap_ranges(pixels.begin() + size_t(y) * width * 4,
                       pixels.begin() + size_t(y + 1) * width * 4,
                       pixels.begin() + size_t(height - 1 - y) * width * 4);
    std::filesystem::create_directories(path.parent_path());
    ok = stbi_write_png(path.string().c_str(), width, height, 4, pixels.data(),
                        width * 4) != 0;
  }
  glDeleteFramebuffers(1, &fbo);
  return ok;
}

class UItemQaTarget
{
public:
  UItemQaTarget(int width = 512, int height = 512)
      : Width(width), Height(height)
  {
    glGenTextures(1, &Texture);
    glBindTexture(GL_TEXTURE_2D, Texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, Width, Height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenFramebuffers(1, &Fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, Fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           Texture, 0);
    glGenRenderbuffers(1, &Depth);
    glBindRenderbuffer(GL_RENDERBUFFER, Depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, Width, Height);
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
    glViewport(0, 0, Width, Height);
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
  int Width{512}, Height{512};
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
inline bool CaptureFpItemState(UFpViewmodelRenderer &renderer,
                               UCreaturePreviewRenderer &preview,
                               UItemQaTarget &target, const std::string &id,
                               const ItemVisualInputs &inputs, const char *body,
                               bool left, const std::filesystem::path &path)
{
  HumanAppearance human;
  human.bodyVariant = body;
  const auto look =
      std::make_shared<ResolvedCreatureAppearance>(ResolveCreatureAppearance(
          *preview.GetSpecies(), *preview.GetSkins(), "human", "", human));
  InventoryEntryRef entry;
  entry.empty = false;
  entry.kind = InventoryEntryKind::Item;
  entry.Id = id;
  entry.count = 1;
  entry.contentsUnits = inputs.ContentsUnits;
  entry.contentsId = inputs.HasWater ? "water" : "";
  target.Bind();
  FpViewmodelDrawAudit audit;
  FpViewmodelDrawParams params;
  params.FramebufferW = target.Width;
  params.FramebufferH = target.Height;
  params.TargetFramebuffer = target.Fbo;
  params.Audit = &audit;
  params.SpeciesId = "human";
  params.Definition = preview.GetSpecies()->Get("human");
  params.Appearance = look;
  params.Active = left ? nullptr : &entry;
  params.Offhand = left ? &entry : nullptr;
  params.BowCharge = left ? 0.f : inputs.BowCharge;
  params.ItemUsePhase = left ? 0.f : inputs.UsePhase;
  params.HasAmmo = inputs.HasAmmo;
  renderer.ClearHeldVisual();
  renderer.DrawWorldOverlay(params);
  const bool saved =
      SaveItemQaTexture(target.Texture, path, target.Width, target.Height);
  if (audit.Items.size() != 1 || audit.Items[0].Id != id)
    return false;
  const auto &item = audit.Items[0];
  const glm::vec2 extent = item.NdcHigh - item.NdcLow;
  const glm::vec2 visible =
      glm::max(glm::vec2(0), glm::min(item.NdcHigh, glm::vec2(1)) -
                                 glm::max(item.NdcLow, glm::vec2(-1)));
  const float fraction =
      visible.x * visible.y / std::max(1e-8f, extent.x * extent.y);
  const nlohmann::json framing = {
      {"ndc_low", {item.NdcLow.x, item.NdcLow.y}},
      {"ndc_high", {item.NdcHigh.x, item.NdcHigh.y}},
      {"all_in_front", item.AllInFront},
      {"visible_bounds_fraction", fraction}};
  std::ofstream(path.string() + ".json") << framing.dump(2) << '\n';
  return saved && item.VisiblePrimitives > 0 && item.AllInFront &&
         fraction >= .6f;
}
inline ItemVisualInputs ItemQaInputs(const ItemRenderAsset &asset, int pose)
{
  ItemVisualInputs inputs;
  inputs.BowCharge = pose * .5f;
  inputs.UsePhase = pose * .25f;
  inputs.HasAmmo = pose > 0;
  inputs.HasWater = pose > 0;
  for (const auto &b : asset.Bindings)
    if (b.Channel == ItemVisualChannel::FillUnits)
      inputs.ContentsUnits = std::max(inputs.ContentsUnits,
                                      int(std::lround(b.Maximum * pose * .5f)));
  return inputs;
}
inline void RunItemStateQa(const std::string &id, const ItemRenderAsset &asset,
                           UItemPreviewRenderer &orbit,
                           UCreaturePreviewRenderer &preview,
                           UFpViewmodelRenderer *fp, UItemQaTarget &fpTarget,
                           const std::filesystem::path &model,
                           const std::filesystem::path &frames,
                           nlohmann::json &report)
{
  auto check = [&](const std::string &name, bool passed)
  {
    report["checks"].push_back({{"name", name}, {"passed", passed}});
    if (!passed)
      report["passed"] = false;
  };
  for (const char *body : HumanAppearance::Bodies)
    for (bool left : {false, true})
      if (fp)
        check(
            id + " FP " + body + (left ? " left" : " right"),
            CaptureFpItemState(*fp, preview, fpTarget, id, {}, body, left,
                               frames / (id + "-fp-" + body +
                                         (left ? "-left.png" : "-right.png"))));
  if (asset.Bindings.empty())
    return;
  report["state_ids"].push_back(id);
  for (int step = 0; step < 4; ++step)
  {
    const auto inputs = ItemQaInputs(asset, step == 3 ? 0 : step);
    const auto label = id + "-state-" + std::to_string(step);
    const auto texture =
        orbit.RenderInspectionTexture(id, model, 512, 35, 25, 0, .65f, inputs);
    check(label + " orbit",
          SaveItemQaTexture(texture, frames / (label + "-orbit.png")));
    glDeleteTextures(1, &texture);
    for (const char *body : HumanAppearance::Bodies)
    {
      HumanAppearance human;
      human.bodyVariant = body;
      for (bool left : {false, true})
      {
        const std::string empty;
        const auto tex = preview.RenderToUniqueTexture(
            "human", "", 512, 30, 12, 0.f, false, nullptr, left ? &empty : &id,
            left ? &id : &empty, &human, left ? nullptr : &inputs,
            left ? &inputs : nullptr);
        const auto file =
            frames / (label + "-" + body + (left ? "-left.png" : "-right.png"));
        check(label + " TP " + body + (left ? " left" : " right"),
              SaveItemQaTexture(tex, file));
        glDeleteTextures(1, &tex);
      }
      if (fp)
        check(label + " FP " + body,
              CaptureFpItemState(*fp, preview, fpTarget, id, inputs, body,
                                 false,
                                 frames / (label + "-fp-" + body + ".png")));
    }
  }
  auto bytes = [](const std::filesystem::path &p)
  {
    std::ifstream f(p, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(f), {});
  };
  const auto rest = bytes(frames / (id + "-state-0-orbit.png"));
  check(id + " active state changes rendered pixels",
        !rest.empty() && rest != bytes(frames / (id + "-state-2-orbit.png")));
  check(id + " cancellation restores rendered pixels",
        !rest.empty() && rest == bytes(frames / (id + "-state-3-orbit.png")));
}
inline void
RunItemPresentationQa(const std::filesystem::path &path,
                      std::shared_ptr<UItemDefinitionStorage> definitions,
                      UCreaturePreviewRenderer &preview,
                      std::shared_ptr<UShaderManager> shaderManager,
                      UFpViewmodelRenderer *fp)
{
  UGlStateScope state(kGlMaskIconFbo | GlStateBit::DepthMask |
                      GlStateBit::DepthFunc | GlStateBit::ColorMask);
  const auto &items = *definitions;
  auto &shaders = *shaderManager;
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
      {"surface_ids", nlohmann::json::array()},
      {"state_ids", nlohmann::json::array()}};
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
  UItemQaTarget fpTarget(1280, 720);
  UItemPreviewRenderer orbit(definitions, shaderManager);
  check("state preview renderer initializes", orbit.Initialize());
  std::set<std::string> requested;
  if (const char *selected = std::getenv("CUBA_ITEM_PRESENTATION_IDS"))
  {
    std::istringstream input(selected);
    for (std::string id; std::getline(input, id, ',');)
      if (!id.empty())
        requested.insert(id);
  }
  for (const auto &id : items.ListCatalogIds())
  {
    if (!requested.empty() && !requested.count(id))
      continue;
    const auto *def = items.Get(id);
    const auto model = IUPlatformPaths::Current().AssetRoot() / def->ModelPath;
    const auto surface =
        FindItemSurfaceAsset(model, UItemDefinitionStorage::ContentRevision());
    if (!surface)
      continue;
    report["surface_ids"].push_back(id);
    RunItemStateQa(id, *surface, orbit, preview, fp, fpTarget, model, frames,
                   report);
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
  if (!requested.empty())
    check("all requested item surfaces inspected",
          report["surface_ids"].size() == requested.size());
  std::ofstream output(path);
  output << report.dump(2) << '\n';
}
} // namespace cutum

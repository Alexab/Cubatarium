#pragma once
#include "App/Platform/IUPlatformPaths.h"
#include "Creatures/Core/HumanAppearance.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Visual/CreatureAppearance.h"
#include "Creatures/Visual/CreatureDrawQueue.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Gui/Batch/UiTexturedQuadBatch.h"
#include "Gui/Cache/InventoryIconService.h"
#include "Gui/Preview/CreaturePreviewRenderer.h"
#include "Gui/Preview/ItemPreviewRenderer.h"
#include "Items/FpViewmodelRenderer.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ItemDrawRequest.h"
#include "Items/ItemGltfTextureCache.h"
#include "Items/ItemPresentationMath.h"
#include "Items/ItemSurfaceRenderer.h"
#include "Items/ItemVisualDefaults.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/GlIncludes.h"
#include "Render/Pipeline/GlStateScope.h"
#include "Test/Native/ItemBowTpQa.h"
#include "Test/Native/ItemConsumeTpQa.h"
#include "Test/Native/ItemHeadwearQa.h"
#include "ThirdParty/stb_image_write.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
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
  ItemDrawRequest Last;
  void DrawItemSurface(const ItemDrawRequest &r) override
  {
    ++Draws;
    Last = r;
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
                               UItemQaTarget &target,
                               const ItemDefinition &definition,
                               const ItemVisualInputs &inputs, const char *body,
                               bool left, const std::filesystem::path &path,
                               float swingSeconds = -1.f)
{
  const auto &id = definition.Id;
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
  params.BowCharge =
      left || !definition.Ranged.Enabled ? 0.f : inputs.BowCharge;
  params.ItemUsePhase = left ? 0.f : inputs.UsePhase;
  params.ItemUseHeld = !left && inputs.UsePhase > 0;
  params.HasAmmo = definition.Ranged.Enabled && inputs.HasAmmo;
  renderer.ClearHeldVisual();
  if (swingSeconds >= 0)
  {
    renderer.SetActiveItemHint(id);
    renderer.NotifySwing(FpSwingKind::Melee);
    for (float remaining = swingSeconds; remaining > 0;)
    {
      const float dt = std::min(remaining, 1.f / 60);
      renderer.Update(dt, 0, 0, 0);
      remaining -= dt;
    }
  }
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
  const bool programs =
      !audit.Arms.empty() &&
      std::all_of(audit.Arms.begin(), audit.Arms.end(),
                  [](const auto &arm) { return arm.ProgramActive; });
  float gripGap = 1e9f;
  for (const auto &arm : audit.Arms)
    if (arm.Left == left)
    {
      const auto palm = arm.RootMvp * glm::vec4(arm.PalmLocal, 1);
      if (palm.w > 0 && item.GripClip.w > 0)
        gripGap = glm::length(glm::vec3(palm) / palm.w -
                              glm::vec3(item.GripClip) / item.GripClip.w);
      break;
    }
  const bool bowContact =
      !params.BowCharge ||
      (audit.BowSupport && audit.BowPoseWeight == 1 &&
       glm::distance(audit.BowSupportTarget, audit.BowSupportPalm) < .0001f);
  auto modelValues = nlohmann::json::array();
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      modelValues.push_back(item.ModelWorld[c][r]);
  const nlohmann::json framing = {
      {"id", id},
      {"left", left},
      {"model_world", modelValues},
      {"view_forward",
       {audit.ViewForward.x, audit.ViewForward.y, audit.ViewForward.z}},
      {"ndc_low", {item.NdcLow.x, item.NdcLow.y}},
      {"ndc_high", {item.NdcHigh.x, item.NdcHigh.y}},
      {"all_in_front", item.AllInFront},
      {"visible_bounds_fraction", fraction},
      {"skin_programs_active", programs},
      {"main_grip_ndc_gap", gripGap},
      {"bow_support_applied", audit.BowSupport},
      {"bow_pose_weight", audit.BowPoseWeight},
      {"bow_support_gap",
       glm::distance(audit.BowSupportTarget, audit.BowSupportPalm)}};
  std::ofstream(path.string() + ".json") << framing.dump(2) << '\n';
  return saved && item.VisiblePrimitives > 0 && item.AllInFront &&
         fraction >= .6f && programs && gripGap < .0001f && bowContact;
}
inline ItemVisualInputs ItemQaInputs(const ItemDefinition &definition,
                                     const ItemRenderAsset &asset, int pose)
{
  ItemVisualInputs inputs;
  inputs.BowCharge = definition.Ranged.Enabled ? pose * .5f : 0.f;
  inputs.UsePhase = pose * .25f;
  inputs.HasAmmo = definition.Ranged.Enabled && pose > 0;
  for (const auto &b : asset.Bindings)
    if (b.Channel == ItemVisualChannel::FillUnits)
      inputs.ContentsUnits = std::max(inputs.ContentsUnits,
                                      int(std::lround(b.Maximum * pose * .5f)));
  inputs.HasWater = inputs.ContentsUnits > 0;
  return inputs;
}
inline void
RunItemStateQa(const ItemDefinition &definition, const ItemRenderAsset &asset,
               UItemPreviewRenderer &orbit, UCreaturePreviewRenderer &preview,
               UFpViewmodelRenderer *fp, UItemQaTarget &fpTarget,
               const std::filesystem::path &model,
               const std::filesystem::path &frames, nlohmann::json &report)
{
  const auto &id = definition.Id;
  auto check = [&](const std::string &name, bool passed)
  {
    report["checks"].push_back({{"name", name}, {"passed", passed}});
    if (!passed)
      report["passed"] = false;
  };
  for (const char *body : HumanAppearance::Bodies)
    for (bool left : {false, true})
      if (fp)
        check(id + " FP " + body + (left ? " left" : " right"),
              CaptureFpItemState(
                  *fp, preview, fpTarget, definition, {}, body, left,
                  frames / (id + "-fp-" + body +
                            (left ? "-left.png" : "-right.png"))));
  if (asset.Bindings.empty())
    return;
  report["state_ids"].push_back(id);
  for (int step = 0; step < 4; ++step)
  {
    const auto inputs = ItemQaInputs(definition, asset, step == 3 ? 0 : step);
    const auto label = id + "-state-" + std::to_string(step);
    if (id == "wood_bow")
    {
      const auto nock = std::find_if(
          asset.Nodes.begin(), asset.Nodes.end(), [](const auto &node)
          { return node.Name == "component_loaded_arrow__nock"; });
      const auto pull =
          std::find_if(asset.Nodes.begin(), asset.Nodes.end(),
                       [](const auto &node) { return node.Name == "pull"; });
      const bool anchors =
          nock != asset.Nodes.end() && pull != asset.Nodes.end();
      check(label + " shared arrow anchors exist", anchors);
      if (anchors)
      {
        const auto state = EvaluateItemVisualState(asset, inputs);
        const auto transforms = ResolveItemNodeTransforms(asset, state);
        const auto nockIndex = size_t(nock - asset.Nodes.begin());
        const auto nockPoint = glm::vec3(transforms[nockIndex][3]);
        const auto pullPoint =
            glm::vec3(transforms[size_t(pull - asset.Nodes.begin())][3]);
        const auto gap = glm::length(nockPoint - pullPoint);
        const bool visible = ItemNodeVisible(asset, state, nockIndex);
        check(label + " shared arrow nock follows string", gap < .00001f);
        check(label + " shared arrow visibility follows ammunition/draw",
              visible == (inputs.HasAmmo && inputs.BowCharge > .001f));
        report["bow_nock_contacts"].push_back({{"charge", inputs.BowCharge},
                                               {"gap_metres", gap},
                                               {"arrow_visible", visible}});
      }
    }
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
              CaptureFpItemState(*fp, preview, fpTarget, definition, inputs,
                                 body, false,
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
inline void RunItemConsumePoseQa(const ItemDefinition &definition,
                                 UCreaturePreviewRenderer &preview,
                                 UFpViewmodelRenderer *fp,
                                 UItemQaTarget &target,
                                 const std::filesystem::path &frames,
                                 nlohmann::json &report)
{
  if (!fp || (definition.Use.Action != ItemUseActionKind::Eat &&
              definition.Use.Action != ItemUseActionKind::Drink))
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
  for (const char *body : HumanAppearance::Bodies)
  {
    std::array<std::filesystem::path, 4> images;
    for (int step = 0; step < 4; ++step)
    {
      ItemVisualInputs inputs;
      if (definition.Use.Action == ItemUseActionKind::Drink)
      {
        inputs.HasWater = true;
        inputs.ContentsUnits = 1;
      }
      inputs.UsePhase = step == 1 ? .5f : step == 2 ? 1.f : 0.f;
      const auto label =
          definition.Id + "-consume-" + body + "-" + std::to_string(step);
      images[step] = frames / (label + ".png");
      check(label + " visible",
            CaptureFpItemState(*fp, preview, target, definition, inputs, body,
                               false, images[step]));
      std::ifstream file(images[step].string() + ".json");
      nlohmann::json framing;
      file >> framing;
      check(label + " remains inside frame",
            framing["visible_bounds_fraction"].get<float>() >= .98f);
    }
    const auto rest = bytes(images[0]);
    const auto raised = bytes(images[1]);
    const auto label = definition.Id + " consume " + body;
    check(label + " raises item", !raised.empty() && raised != rest);
    auto centerY = [](const std::filesystem::path &path)
    {
      std::ifstream file(path.string() + ".json");
      nlohmann::json framing;
      file >> framing;
      return .5f * (framing["ndc_low"][1].get<float>() +
                    framing["ndc_high"][1].get<float>());
    };
    check(label + " moves toward mouth height",
          centerY(images[2]) > centerY(images[0]) + .05f);
    check(label + " holds pose until release", raised == bytes(images[2]));
    check(label + " cancellation restores pose", rest == bytes(images[3]));
  }
}
// An isolated native cache exercises real GPU transfers, exact pixels and
// cancellation without touching the player's persistent icon cache.
inline void RunItemIconReadbackQa(const std::filesystem::path &frames,
                                  nlohmann::json &report)
{
  const auto check = [&](const std::string &name, bool passed)
  {
    report["checks"].push_back({{"name", name}, {"passed", passed}});
    if (!passed)
      report["passed"] = false;
  };
  UGlStateScope state(kGlMaskIconFbo);
  GLint oldPack = 0, oldAlignment = 0, oldRows = 0, oldSkipRows = 0,
        oldSkipPixels = 0, oldRead = 0, oldDraw = 0;
  glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &oldPack);
  glGetIntegerv(GL_PACK_ALIGNMENT, &oldAlignment);
  glGetIntegerv(GL_PACK_ROW_LENGTH, &oldRows);
  glGetIntegerv(GL_PACK_SKIP_ROWS, &oldSkipRows);
  glGetIntegerv(GL_PACK_SKIP_PIXELS, &oldSkipPixels);
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldRead);
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldDraw);
  GLuint texture = 0, sentinel = 0, fbos[2]{};
  const std::array<unsigned char, 16> expected = {
      11, 22, 33, 255, 44, 55, 66, 127, 77, 88, 99, 0, 101, 112, 123, 255};
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               expected.data());
  glGenBuffers(1, &sentinel);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, sentinel);
  glBufferData(GL_PIXEL_PACK_BUFFER, 128, nullptr, GL_STREAM_READ);
  glPixelStorei(GL_PACK_ALIGNMENT, 8);
  glPixelStorei(GL_PACK_ROW_LENGTH, 7);
  glPixelStorei(GL_PACK_SKIP_ROWS, 1);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 2);
  glGenFramebuffers(2, fbos);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[0]);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos[1]);
  const auto preserved = [&]()
  {
    GLint buffer{}, alignment{}, rows{}, skipRows{}, skipPixels{}, read{},
        draw{};
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &buffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &rows);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows);
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    return buffer == GLint(sentinel) && alignment == 8 && rows == 7 &&
           skipRows == 1 && skipPixels == 2 && read == GLint(fbos[0]) &&
           draw == GLint(fbos[1]);
  };
  UInventoryIconService service;
  check("icon readback isolated cache initializes",
        service.Initialize(frames / "readback-cache"));
  service.InvalidateAll();
  check("item disk store accepts asynchronous readback",
        service.StoreIconTexture("item", "readback-probe", "", "first", 2,
                                 texture));
  check("queued disk store has no premature manifest entry",
        service.GetStats().RenderStores == 0);
  check("enqueue preserves pack and separate framebuffer state", preserved());
  // Only the test waits; normal frame draining always uses a zero timeout.
  glFinish();
  service.FlushManifest();
  check("ready disk readback drains exactly once",
        service.GetStats().RenderStores == 1);
  check("drain preserves pack and separate framebuffer state", preserved());
  GLuint loaded = 0;
  check("completed asynchronous icon survives manifest reload",
        [&]()
        {
          UInventoryIconService reload;
          return reload.Initialize(frames / "readback-cache") &&
                 reload.TryLoadIconTexture("item", "readback-probe", "",
                                           "first", 2, loaded);
        }());
  std::array<unsigned char, 16> actual{};
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  glPixelStorei(GL_PACK_SKIP_ROWS, 0);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[0]);
  glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, loaded, 0);
  if (loaded)
    glReadPixels(0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
  check("asynchronous PNG roundtrip preserves every RGBA byte",
        loaded && actual == expected);
  check("readback invalidation probe queues",
        service.StoreIconTexture("item", "readback-probe", "", "cancelled", 2,
                                 texture));
  service.InvalidateKind("item");
  glFinish();
  service.FlushManifest();
  GLuint absent = 0;
  check("invalidated pending icon cannot recreate stale disk entry",
        service.GetStats().RenderStores == 1 &&
            !service.TryLoadIconTexture("item", "readback-probe", "",
                                        "cancelled", 2, absent));
  service.InvalidateAll();
  glBindBuffer(GL_PIXEL_PACK_BUFFER, oldPack);
  glPixelStorei(GL_PACK_ALIGNMENT, oldAlignment);
  glPixelStorei(GL_PACK_ROW_LENGTH, oldRows);
  glPixelStorei(GL_PACK_SKIP_ROWS, oldSkipRows);
  glPixelStorei(GL_PACK_SKIP_PIXELS, oldSkipPixels);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, oldRead);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, oldDraw);
  glDeleteBuffers(1, &sentinel);
  glDeleteFramebuffers(2, fbos);
  glDeleteTextures(1, &texture);
  glDeleteTextures(1, &loaded);
}

inline void RunGuiStaticQuadQa(UShaderManager &shaders, nlohmann::json &report)
{
  UGlStateScope scope(kGlMaskIconFbo | GlStateBit::DepthMask |
                      GlStateBit::ColorMask);
  struct RestoreEdges
  {
    GLint ScissorBox[4]{}, SrcRgb{}, DstRgb{}, SrcAlpha{}, DstAlpha{};
    bool Scissor{glIsEnabled(GL_SCISSOR_TEST) != 0};
    RestoreEdges()
    {
      glGetIntegerv(GL_SCISSOR_BOX, ScissorBox);
      glGetIntegerv(GL_BLEND_SRC_RGB, &SrcRgb);
      glGetIntegerv(GL_BLEND_DST_RGB, &DstRgb);
      glGetIntegerv(GL_BLEND_SRC_ALPHA, &SrcAlpha);
      glGetIntegerv(GL_BLEND_DST_ALPHA, &DstAlpha);
    }
    ~RestoreEdges()
    {
      glScissor(ScissorBox[0], ScissorBox[1], ScissorBox[2], ScissorBox[3]);
      if (Scissor)
        glEnable(GL_SCISSOR_TEST);
      else
        glDisable(GL_SCISSOR_TEST);
      glBlendFuncSeparate(GLenum(SrcRgb), GLenum(DstRgb), GLenum(SrcAlpha),
                          GLenum(DstAlpha));
    }
  } restoreEdges;
  const auto check = [&](const std::string &name, bool passed)
  {
    report["checks"].push_back({{"name", name}, {"passed", passed}});
    if (!passed)
      report["passed"] = false;
  };
  const auto shader =
      shaders.CreateShader("gui_quad_pixel_qa", "shaders/gui_textured_v.glsl",
                           "shaders/gui_textured_f.glsl");
  const auto legacyShader =
      shaders.CreateShaderFromStrings("gui_quad_legacy_qa",
                                      R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aTexCoord;
out vec2 TexCoord;
uniform vec2 screenSize;
void main() {
  gl_Position=vec4((aPos/screenSize)*2.0-1.0,0.0,1.0);
  TexCoord=aTexCoord;
})",
                                      R"(#version 330 core
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture0;
uniform vec4 tint;
void main() { FragColor=texture(texture0,TexCoord)*tint; }
)");
  UGuiTexturedQuadBatch batch, legacyBatch;
  check("static GUI quad initializes", batch.Initialize(shader));
  check("legacy GUI shader fallback initializes",
        legacyBatch.Initialize(legacyShader));
  if (!shader || !legacyShader)
    return;
  GLuint texture = 0, vao = 0, vbo = 0;
  const unsigned char colors[] = {255, 20, 40,  255, 30,  255, 60, 127,
                                  70,  80, 255, 64,  220, 100, 40, 0};
  glGenTextures(1, &texture);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               colors);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        reinterpret_cast<void *>(2 * sizeof(float)));
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  for (const int width : {1280, 1920})
    for (const float scale : {1.f, 1.5f, 2.f})
      for (const bool clipped : {false, true})
      {
        const int height = width * 9 / 16;
        UItemQaTarget target(width, height);
        const std::array<GuiRect, 3> rects = {
            GuiRect{-int(7 * scale), int(19 * scale), int(61 * scale),
                    int(43 * scale)},
            GuiRect{width / 2, height / 3, int(79 * scale), int(57 * scale)},
            GuiRect{width - 23, height - 20, 40, 50}};
        const auto draw = [&](int mode)
        {
          target.Bind();
          auto &activeBatch = mode == 2 ? legacyBatch : batch;
          activeBatch.Begin(width, height);
          if (clipped)
          {
            glEnable(GL_SCISSOR_TEST);
            glScissor(3, 5, width - 9, height - 13);
          }
          for (size_t i = 0; i < rects.size(); ++i)
          {
            const auto &r = rects[i];
            const glm::vec4 tint(.6f + float(i) * .1f, .8f, 1.f, .65f);
            if (mode != 0)
              activeBatch.DrawTexturedRect(r, texture, tint);
            else
            {
              // Independent pre-change CPU coordinates and vertex shader.
              const float x0 = float(r.X), x1 = float(r.X + r.W),
                          y0 = float(height) - float(r.Y + r.H),
                          y1 = float(height) - float(r.Y);
              const float vertices[] = {x0, y0, 0, 0, x1, y0, 1, 0,
                                        x0, y1, 0, 1, x0, y1, 0, 1,
                                        x1, y0, 1, 0, x1, y1, 1, 1};
              legacyShader->Use();
              legacyShader->SetVec2("screenSize", glm::vec2(width, height));
              legacyShader->SetVec4("tint", tint);
              legacyShader->SetInt("texture0", 0);
              glActiveTexture(GL_TEXTURE0);
              glBindTexture(GL_TEXTURE_2D, texture);
              glBindVertexArray(vao);
              glBindBuffer(GL_ARRAY_BUFFER, vbo);
              glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices,
                           GL_STREAM_DRAW);
              glDrawArrays(GL_TRIANGLES, 0, 6);
              legacyShader->Unuse();
            }
          }
          activeBatch.End();
          std::vector<unsigned char> pixels(size_t(width) * height * 4);
          glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                       pixels.data());
          return pixels;
        };
        const auto reference = draw(0), actual = draw(1), fallback = draw(2);
        bool colored = false;
        for (size_t i = 4; i < reference.size(); i += 4)
          colored = colored || reference[i] != reference[0] ||
                    reference[i + 1] != reference[1] ||
                    reference[i + 2] != reference[2];
        check("static GUI quad preserves RGBA " + std::to_string(width) +
                  " scale " + std::to_string(scale) +
                  (clipped ? " clipped" : " unclipped"),
              colored && reference == actual);
        check("legacy GUI fallback preserves RGBA " + std::to_string(width) +
                  " scale " + std::to_string(scale) +
                  (clipped ? " clipped" : " unclipped"),
              colored && reference == fallback);
      }
  glDisable(GL_SCISSOR_TEST);
  glDeleteBuffers(1, &vbo);
  glDeleteVertexArrays(1, &vao);
  glDeleteTextures(1, &texture);
}

inline void RunItemColorQa(ItemSurfaceRenderer &renderer,
                           const std::filesystem::path &frames,
                           nlohmann::json &report)
{
  auto check = [&](const std::string &name, bool passed)
  {
    report["checks"].push_back({{"name", name}, {"passed", passed}});
    if (!passed)
      report["passed"] = false;
  };
  std::filesystem::create_directories(frames);
  const auto image = frames / "srgb-checker.png";
  const unsigned char pixels[] = {0,   0,   0,   255, 255, 255, 255, 255,
                                  255, 255, 255, 255, 0,   0,   0,   255};
  check("color fixture PNG writes",
        stbi_write_png(image.string().c_str(), 2, 2, 4, pixels, 8) != 0);
  auto &cache = ItemGltfTextureCache::Instance();
  const auto texture = cache.GetLinearAlbedo(image);
  check("albedo path aliases share texture",
        texture && texture == cache.GetLinearAlbedo(frames / "." /
                                                    "srgb-checker.png"));
  const auto beforeLookups = cache.GetLookupStats();
  for (int i = 0; i < 10; ++i)
    cache.GetLinearAlbedo(image);
  const auto afterLookups = cache.GetLookupStats();
  check("warm albedo lookup avoids repeated filesystem resolution",
        afterLookups.CanonicalResolutions ==
                beforeLookups.CanonicalResolutions &&
            afterLookups.FastHits == beforeLookups.FastHits + 10);
  glBindTexture(GL_TEXTURE_2D, texture);
  GLint format = 0, filter = 0, width = 0;
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT,
                           &format);
  glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &filter);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &width);
  check("item albedo uses sRGB storage and complete mip chain",
        format == GL_SRGB8_ALPHA8 && filter == GL_LINEAR_MIPMAP_LINEAR &&
            width == 1);
#if !defined(__ANDROID__)
  unsigned char mip[4] = {};
  glGetTexImage(GL_TEXTURE_2D, 1, GL_RGBA, GL_UNSIGNED_BYTE, mip);
  check("mip average is linear-light black and white",
        mip[0] >= 186 && mip[0] <= 189);
  report["color_pipeline"]["mip_encoded_rgb"] = mip[0];
#endif
  auto asset = std::make_shared<ItemRenderAsset>();
  asset->Materials.push_back({});
  asset->Materials[0].Albedo = image;
  const auto bind = glm::translate(glm::mat4(1), glm::vec3(.0625f, .03125f, 0));
  asset->Nodes.push_back({"fixture_root", bind, bind, -1});
  ItemRenderPrimitive primitive;
  primitive.Vertices = {-1,  -1,  0,  .5f, .5f, 0,   0,   1, 1,   -1,  0,
                        .5f, .5f, 0,  0,   1,   1,   1,   0, .5f, .5f, 0,
                        0,   1,   -1, 1,   0,   .5f, .5f, 0, 0,   1};
  primitive.Indices = {0, 1, 2, 0, 2, 3};
  asset->Primitives.push_back(std::move(primitive));
  ItemPresentationProfile profile;
  profile.InspectionMode = 3;
  UItemQaTarget target(16, 16);
  for (int factor = 0; factor < 2; ++factor)
  {
    target.Bind();
    asset->Materials[0].Color = glm::vec4(factor ? .5f : 1.f);
    renderer.Draw(asset, {}, glm::mat4(1.f), glm::mat4(1.f), {}, profile);
    unsigned char pixel[4] = {};
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    const int expected = factor ? 137 : 188;
    check(factor ? "linear material factor applies once"
                 : "bilinear sampling precedes display encoding",
          std::abs(int(pixel[0]) - expected) <= 2 && pixel[0] == pixel[1] &&
              pixel[1] == pixel[2]);
    report["color_pipeline"][factor ? "half_factor_encoded_rgb"
                                    : "filtered_encoded_rgb"] = pixel[0];
  }

  // Exercise every cached uniform, switching profiles/materials and inspection
  // modes in one batch. No geometry, ordering or pixels may change.
  auto combined = std::make_shared<ItemRenderAsset>(*asset);
  combined->Materials.push_back(combined->Materials[0]);
  auto rightHalf = combined->Primitives[0];
  rightHalf.Material = 1;
  for (size_t i = 0; i < rightHalf.Vertices.size(); i += 8)
  {
    rightHalf.Vertices[i] = (rightHalf.Vertices[i] + 1.f) * .5f;
    rightHalf.Vertices[i + 2] = -.1f;
  }
  combined->Primitives.push_back(rightHalf);
  combined->StaticWearSections["fixture_root"].Primitives = {0, 1};
  auto first = std::make_shared<ItemRenderAsset>(*asset);
  auto second = std::make_shared<ItemRenderAsset>(*asset);
  rightHalf.Material = 0;
  second->Primitives[0] = rightHalf;
  const auto renderCase = [&](int index, bool split = false)
  {
    target.Bind();
    auto p = profile;
    p.InspectionMode = index % 7;
    p.CameraPosition = {float(index % 3) * .2f, .1f, 3.f};
    p.LightDirection = {.2f + float(index % 4) * .1f, .7f, 1.f};
    p.Ambient = .2f + float(index % 5) * .1f;
    p.LightIntensity = .3f + float(index % 6) * .1f;
    p.Fog = float(index % 3) * .2f;
    p.FogColor = {.2f, .3f + float(index % 2) * .1f, .4f};
    auto &material = combined->Materials[0];
    material.Color = {1.f, .4f + float(index % 4) * .1f, .6f, 1.f};
    material.Metallic = index % 2 ? .7f : 0.f;
    material.Roughness = .2f + float(index % 5) * .1f;
    material.Emissive = {float(index % 3) * .03f, 0.f, .01f};
    combined->Materials[1] = material;
    combined->Materials[1].Color = {.5f, .7f, .9f, 1.f};
    combined->Materials[1].Roughness = .3f + float(index % 3) * .1f;
    ItemVisualState selected;
    selected.VisibleRoots.insert("fixture_root");
    if (split)
    {
      selected.NodeTransforms["fixture_root"] = glm::mat4(1);
      first->Materials[0] = combined->Materials[0];
      second->Materials[0] = combined->Materials[1];
      renderer.Draw(first, {}, glm::mat4(1), glm::mat4(1), selected, p);
      renderer.Draw(second, {}, glm::mat4(1), glm::mat4(1), selected, p);
    }
    else
      renderer.Draw(combined, {}, glm::mat4(1), glm::mat4(1), selected, p);
    std::vector<unsigned char> result(16 * 16 * 4);
    glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
    return result;
  };
  std::vector<std::vector<unsigned char>> reference;
  for (int i = 0; i < 28; ++i)
    reference.push_back(renderCase(i, true));
  for (int i = 0; i < 28; ++i)
    check("local item material reuse preserves pixels " + std::to_string(i),
          reference[size_t(i)] == renderCase(i));
  {
    UItemSurfaceBatchScope batch(renderer);
    for (int i = 0; i < 28; ++i)
      check("contiguous item batch preserves pixels " + std::to_string(i),
            reference[size_t(i)] == renderCase(i));
  }
  check("item uniforms reset outside batch", reference[0] == renderCase(0));
}

inline void RunItemIconSamplingQa(UItemPreviewRenderer &preview,
                                  nlohmann::json &report)
{
  UGlStateScope scope(kGlMaskIconFbo);
  const bool oldScissor = glIsEnabled(GL_SCISSOR_TEST);
  GLint oldActive = 0;
  glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
  auto check = [&](const std::string &name, bool ok)
  {
    report["checks"].push_back({{"name", name}, {"passed", ok}});
    if (!ok)
      report["passed"] = false;
  };
  std::array<unsigned char, 64> coveragePixels{};
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x)
    {
      const int index = (y * 4 + x) * 4;
      coveragePixels[index] = coveragePixels[index + 1] =
          coveragePixels[index + 2] = (x + y) % 2 ? 255 : 0;
      coveragePixels[index + 3] = 255;
    }
  GLuint source = 0;
  glGenTextures(1, &source);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, source);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               coveragePixels.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glViewport(7, 9, 13, 11);
  glEnable(GL_SCISSOR_TEST);
  glActiveTexture(GL_TEXTURE1);
  const auto result = preview.ResolveIconTexture(source, 1);
  GLint viewport[4]{}, active = 0;
  glGetIntegerv(GL_VIEWPORT, viewport);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
  check("icon sampling restores caller viewport, scissor and active unit",
        viewport[0] == 7 && viewport[1] == 9 && viewport[2] == 13 &&
            viewport[3] == 11 && glIsEnabled(GL_SCISSOR_TEST) &&
            active == GL_TEXTURE1);
  check("icon sampling rejects mismatched source dimensions",
        preview.ResolveIconTexture(source, 2) == 0);
  GLuint fbo = 0;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         result, 0);
  std::array<unsigned char, 4> pixel{};
  if (result &&
      glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
  check("icon coverage averages in linear light then encodes once",
        result && std::abs(int(pixel[0]) - 188) <= 2 && pixel[0] == pixel[1] &&
            pixel[1] == pixel[2] && pixel[3] == 255);
  report["icon_sampling"] = {{"samples_per_pixel", 16},
                             {"encoded_checker_rgb", pixel[0]}};
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &result);
  glDeleteTextures(1, &source);
  glActiveTexture(GLenum(oldActive));
  if (!oldScissor)
    glDisable(GL_SCISSOR_TEST);
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
  RunGuiStaticQuadQa(shaders, report);
  RunItemIconReadbackQa(frames, report);
  RunItemColorQa(renderer, frames, report);
  UItemQaTarget target;
  UItemQaTarget fpTarget(1280, 720);
  UItemPreviewRenderer orbit(definitions, shaderManager);
  const bool initialized = orbit.Initialize();
  check("state preview renderer initializes", initialized);
  if (!initialized)
    throw std::runtime_error("Cannot initialize native item inspection");
  RunItemIconSamplingQa(orbit, report);
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
    if (fp)
    {
      const auto *preset = items.VisualPresets().Get(
          DefaultSwingPreset(*def, FpSwingKind::Melee));
      const float duration = preset ? preset->Duration : .28f;
      for (const char *body : HumanAppearance::Bodies)
        for (int step = 0; step < 3; ++step)
        {
          const auto label = id + "-swing-" + body + "-" + std::to_string(step);
          const float seconds = step == 0   ? 0.f
                                : step == 1 ? duration * .5f
                                            : duration * 1.01f;
          check(label + " actual FP geometry and palm contact",
                CaptureFpItemState(*fp, preview, fpTarget, *def, {}, body,
                                   false, frames / (label + ".png"), seconds));
        }
    }
    RunItemStateQa(*def, *surface, orbit, preview, fp, fpTarget, model, frames,
                   report);
    RunItemConsumePoseQa(*def, preview, fp, fpTarget, frames, report);
    RunItemConsumeTpQa(*def, preview, frames, report);
    RunItemHeadwearQa(*def, preview, frames, report);
    RunItemBowTpQa(*def, preview, frames, report);
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
          size_t expectedSections = 0;
          std::set<std::string> expectedRoots, observedRoots;
          for (const auto &section : surface->WearSections)
            if (!slots[section.Slot].ItemId.empty())
            {
              expectedSections += section.Bones.size();
              expectedRoots.insert(section.Node);
            }
          float minimumY = std::numeric_limits<float>::max();
          preview.SetItemSurfaceObserver(
              [&](const ItemDrawRequest &r)
              {
                if (r.Path.parent_path().filename() == id)
                {
                  ++draws;
                  observedRoots.insert(r.State.VisibleRoots.begin(),
                                       r.State.VisibleRoots.end());
                  const auto transforms =
                      ResolveItemNodeTransforms(*r.Asset, r.State);
                  for (const auto &primitive : r.Asset->Primitives)
                    if (ItemNodeVisible(*r.Asset, r.State, primitive.Node))
                      for (size_t i = 0; i + 7 < primitive.Vertices.size();
                           i += 8)
                        minimumY = std::min(
                            minimumY, (r.Model * transforms[primitive.Node] *
                                       glm::vec4(primitive.Vertices[i],
                                                 primitive.Vertices[i + 1],
                                                 primitive.Vertices[i + 2], 1))
                                          .y);
                }
              });
          const auto tex = preview.RenderToUniqueTexture(
              "human", "", 512, 30, 12, phase * .35f, phase != 0, &slots,
              nullptr, nullptr, &appearance);
          const auto label =
              id + "-" + body + "-armor-" + std::to_string(phase);
          check(label + " draws item surface",
                expectedSections ? size_t(draws) == expectedSections &&
                                       observedRoots == expectedRoots
                                 : draws > 0);
          report["wear_geometry"][label] = {{"draws", draws},
                                            {"minimum_y", minimumY},
                                            {"roots", observedRoots}};
          if (!slots[3].ItemId.empty())
            check(label + " sole remains above support tolerance",
                  minimumY >= -.025f);
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
    {
      CreatureDrawQueue queue;
      const auto model = glm::translate(glm::mat4(1), glm::vec3(1, 2, 3)) *
                         glm::rotate(glm::mat4(1), .8f, glm::vec3(0, 1, 0)) *
                         glm::scale(glm::mat4(1), glm::vec3(.8f));
      const auto axis = glm::normalize(glm::vec3(-.8f, 0, .6f));
      WornEquipmentDrawer::SubmitCarried(queue, id, vp, model,
                                         def->Physical.size, axis);
      UItemQaSink sink(renderer);
      queue.Flush(sink);
      glm::vec3 right, left;
      check(id + " both carried contacts agree with observed draw transform",
            sink.Draws == 1 &&
                WornEquipmentDrawer::CarriedContacts(id, axis, 1.f, 2, right,
                                                     left) &&
                glm::distance(glm::vec3(model * glm::vec4(right, 1)),
                              glm::vec3(sink.Last.Model *
                                        glm::vec4(surface->Grip.CarryRightGrip,
                                                  1))) < .00001f &&
                glm::distance(glm::vec3(model * glm::vec4(left, 1)),
                              glm::vec3(sink.Last.Model *
                                        glm::vec4(surface->Grip.CarryLeftGrip,
                                                  1))) < .00001f);
    }
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

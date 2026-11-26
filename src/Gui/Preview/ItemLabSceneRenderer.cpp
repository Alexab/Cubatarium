#include "Gui/Preview/ItemLabSceneRenderer.h"
#include "Core/Memory/VisualAssetCensus.h"
#include "Creatures/Core/HumanAppearance.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Visual/CreatureAppearance.h"
#include "Creatures/Visual/CreatureDrawQueue.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Gui/Preview/CreaturePreviewRenderer.h"
#include "Gui/Preview/ItemLabOverlayRenderer.h"
#include "Gui/Preview/ItemPreviewRenderer.h"
#include "Items/FpViewmodelRenderer.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ItemDrawRequest.h"
#include "Items/ItemInspectionSource.h"
#include "Items/ItemSurfaceRenderer.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/GlIncludes.h"
#include "Render/Pipeline/GlStateScope.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>

namespace cutum
{
namespace
{
class UStudioTarget
{
public:
  explicit UStudioTarget(int width = 512, int height = 512,
                         bool silhouette = false)
  {
    glGenTextures(1, &Texture);
    glBindTexture(GL_TEXTURE_2D, Texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenFramebuffers(1, &Fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, Fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           Texture, 0);
    glGenRenderbuffers(1, &Depth);
    glBindRenderbuffer(GL_RENDERBUFFER, Depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, Depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
      glDeleteFramebuffers(1, &Fbo);
      glDeleteRenderbuffers(1, &Depth);
      glDeleteTextures(1, &Texture);
      throw std::runtime_error("Item Lab framebuffer incomplete");
    }
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (silhouette)
      glClearColor(.95f, .95f, .95f, 1);
    else
      glClearColor(.08f, .09f, .12f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }
  ~UStudioTarget()
  {
    glDeleteFramebuffers(1, &Fbo);
    glDeleteRenderbuffers(1, &Depth);
    if (Texture)
      glDeleteTextures(1, &Texture);
  }
  unsigned Take()
  {
    const auto result = Texture;
    Texture = 0;
    return result;
  }
  unsigned Texture{0}, Fbo{0}, Depth{0};
};
class UStudioSink final : public ICreatureDrawSink
{
public:
  explicit UStudioSink(ItemSurfaceRenderer &surface) : Surface(surface) {}
  ItemSurfaceRenderer &Surface;
  ItemPresentationProfile Profile;
  size_t Count{0};
  void DrawItemSurface(const ItemDrawRequest &r) override
  {
    ++Count;
    Surface.Draw(r.Asset, r.Path, r.ViewProjection, r.Model, r.State, Profile);
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
void Blit(unsigned source, unsigned destination, int width, int height, int x,
          int y, int w, int h)
{
  unsigned read = 0;
  glGenFramebuffers(1, &read);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
  glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, source, 0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
  glBlitFramebuffer(0, 0, width, height, x, y, x + w, y + h,
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glDeleteFramebuffers(1, &read);
}
} // namespace
UItemLabSceneRenderer::UItemLabSceneRenderer(
    std::shared_ptr<UItemDefinitionStorage> items,
    std::shared_ptr<UBlockDefinitionStorage> blocks,
    std::shared_ptr<UTextureCubeStorage> textures,
    std::shared_ptr<UCreatureTextureStorage> creatureTextures,
    std::shared_ptr<UCreatureDefinitionStorage> species,
    std::shared_ptr<USkinDefinitionStorage> skins,
    std::shared_ptr<UShaderManager> shaders)
    : Items(std::move(items))
{
  Creature = std::make_unique<UCreaturePreviewRenderer>(
      species, skins, creatureTextures, shaders);
  Fp = std::make_unique<UFpViewmodelRenderer>(Items, blocks, textures,
                                              creatureTextures, shaders);
  Surface = std::make_unique<ItemSurfaceRenderer>();
  Overlay = std::make_unique<UItemLabOverlayRenderer>(*shaders);
  if (!Creature->Initialize() || !Fp->Initialize() ||
      !Surface->Initialize(*shaders))
    throw std::runtime_error("Cannot initialize Item Lab scenes");
}
UItemLabSceneRenderer::~UItemLabSceneRenderer() = default;
void UItemLabSceneRenderer::InspectGpuBuffers(TerrainGpuBufferCensus &out) const
{
  Creature->InspectGpuBuffers(out);
  Fp->InspectPreviewGpuBuffers(out);
  Surface->InspectGpuBuffers(out);
  Overlay->InspectGpuBuffers(out);
}

void UItemLabSceneRenderer::InspectRenderImages(RenderImageCensus &out) const
{
  Creature->InspectRenderImages(out);
}

void UItemLabSceneRenderer::InspectVisualAssets(VisualAssetCensus &census) const
{
  if (Creature)
    Creature->InspectVisualAssets(census);
  if (Fp)
    Fp->InspectVisualAssets(census);
  if (Surface)
    Surface->InspectVisualAssets(census, VisualAssetOwner::Preview);
}

void UItemLabSceneRenderer::Invalidate()
{
  Creature->Invalidate();
  Surface->Clear();
  Fp->ClearHeldVisual();
}
unsigned UItemLabSceneRenderer::Render(
    UItemPreviewRenderer &orbit, const std::string &id,
    const std::filesystem::path &path,
    const std::shared_ptr<const ItemRenderAsset> &asset,
    const ItemLabSceneParams &p)
{
  LastDrawCount = 1;
  LastSourceMatches = true;
  LastBowSupportContact = false;
  LastDiagnosticVertices = 0;
  LastDiagnosticsFramed = true;
  if (p.Scene == 6)
  {
    const auto *definition = Items->Get(id);
    if (!definition)
      throw std::runtime_error("Missing diagnostic item definition");
    const auto state = EvaluateItemVisualState(*asset, p.Inputs);
    const auto lines =
        ItemLabContactLines(*asset, state, definition->Physical.size);
    auto bounds = ItemVisualBounds(*asset, state);
    for (const auto &line : lines)
    {
      bounds.first = glm::min(bounds.first, line.Position);
      bounds.second = glm::max(bounds.second, line.Position);
    }
    orbit.SetInspectionFramingBounds(bounds);
    orbit.SetInspectionDrawObserver(
        [&](const ItemDrawRequest &request)
        {
          LastSourceMatches = request.Asset == asset && request.Path == path;
          LastDiagnosticVertices = Overlay->DrawContacts(
              *request.Asset, request.State, definition->Physical.size,
              request.ViewProjection * request.Model);
          for (const auto &line : lines)
          {
            const auto clip = request.ViewProjection * request.Model *
                              glm::vec4(line.Position, 1);
            LastDiagnosticsFramed = LastDiagnosticsFramed && clip.w > 0 &&
                                    std::abs(clip.x) <= clip.w &&
                                    std::abs(clip.y) <= clip.w;
          }
        });
    struct ResetObserver
    {
      UItemPreviewRenderer &Renderer;
      ~ResetObserver()
      {
        Renderer.SetInspectionDrawObserver({});
        Renderer.SetInspectionFramingBounds({});
      }
    } reset{orbit};
    return orbit.RenderInspectionTexture(id, path, 512, p.Yaw, p.Pitch, p.Mode,
                                         p.Ambient, p.Inputs);
  }
  if (p.Scene == 0)
    return orbit.RenderInspectionTexture(id, path, 512, p.Yaw, p.Pitch, p.Mode,
                                         p.Ambient, p.Inputs);
  UGlStateScope state(kGlMaskIconFbo | GlStateBit::DepthMask |
                      GlStateBit::DepthFunc | GlStateBit::ColorMask);
  const bool scissor = glIsEnabled(GL_SCISSOR_TEST);
  struct RestoreScissor
  {
    bool Enabled;
    ~RestoreScissor()
    {
      if (Enabled)
        glEnable(GL_SCISSOR_TEST);
      else
        glDisable(GL_SCISSOR_TEST);
    }
  } restoreScissor{scissor};
  UItemInspectionScope inspection({id, path, asset});
  struct RestoreDefinitions
  {
    const UItemDefinitionStorage *Previous;
    ~RestoreDefinitions() { WornEquipmentDrawer::SetItemDefinitions(Previous); }
  } restore{WornEquipmentDrawer::ItemDefinitions()};
  WornEquipmentDrawer::SetItemDefinitions(Items.get());
  HumanAppearance human;
  human.bodyVariant = p.Female ? "female" : "male";
  if (p.Scene == 2 || p.Scene == 4)
  {
    std::array<WornArmorPreviewSlot, kWornArmorSlotCount> slots;
    if (p.Scene == 4)
    {
      const auto *def = Items->Get(id);
      if (!def || def->Armor.Slots.empty())
        throw std::runtime_error("Select armor for the worn scene");
      const char *names[] = {"head", "chest", "legs", "feet"};
      for (size_t i = 0; i < 4; ++i)
        if (std::find(def->Armor.Slots.begin(), def->Armor.Slots.end(),
                      names[i]) != def->Armor.Slots.end())
          slots[i].ItemId = id;
    }
    Creature->SetItemInspection(p.Mode, p.Ambient);
    LastDrawCount = 0;
    Creature->SetItemSurfaceObserver(
        [&](const ItemDrawRequest &r)
        {
          ++LastDrawCount;
          LastSourceMatches =
              LastSourceMatches && r.Path == path && r.Asset == asset;
        });
    struct ResetObserver
    {
      UCreaturePreviewRenderer &Renderer;
      ~ResetObserver() { Renderer.SetItemSurfaceObserver({}); }
    } reset{*Creature};
    const std::string empty;
    return Creature->RenderToUniqueTexture(
        "human", "", 512, p.Yaw, p.Pitch, p.Time, p.Walk,
        p.Scene == 4 ? &slots : nullptr, p.Scene == 2 && !p.Left ? &id : &empty,
        p.Scene == 2 && p.Left ? &id : &empty, &human, &p.Inputs, &p.Inputs);
  }
  if (p.Scene == 1)
  {
    UStudioTarget fp(1280, 720, p.Mode == 1);
    InventoryEntryRef entry;
    entry.empty = false;
    entry.kind = InventoryEntryKind::Item;
    entry.Id = id;
    entry.count = 1;
    entry.contentsUnits = p.Inputs.ContentsUnits;
    entry.contentsId = p.Inputs.HasWater ? "water" : "";
    FpViewmodelDrawParams params;
    params.FramebufferW = 1280;
    params.FramebufferH = 720;
    params.TargetFramebuffer = fp.Fbo;
    params.SpeciesId = "human";
    params.Definition = Creature->GetSpecies()->Get("human");
    params.Appearance = std::make_shared<ResolvedCreatureAppearance>(
        ResolveCreatureAppearance(*Creature->GetSpecies(),
                                  *Creature->GetSkins(), "human", "", human));
    params.Active = p.Left ? nullptr : &entry;
    params.Offhand = p.Left ? &entry : nullptr;
    params.BowCharge = p.Left ? 0.f : p.Inputs.BowCharge;
    params.ItemUsePhase = p.Left ? 0.f : p.Inputs.UsePhase;
    params.ItemUseHeld = !p.Left && p.Inputs.UsePhase > 0;
    params.HasAmmo = p.Inputs.HasAmmo;
    FpViewmodelDrawAudit audit;
    params.Audit = &audit;
    params.ItemInspectionMode = p.Mode;
    params.ItemAmbient = p.Ambient;
    // Reference/candidate renders share the renderer but are independent
    // snapshots, without a gameplay Update tick between them.
    Fp->ClearHeldVisual();
    Fp->DrawWorldOverlay(params);
    LastBowSupportContact =
        audit.BowSupport && audit.BowPoseWeight == 1 &&
        glm::distance(audit.BowSupportTarget, audit.BowSupportPalm) < .0001f;
    LastDrawCount = audit.Items.size();
    if (audit.Items.size() != 1 || !audit.Items[0].VisiblePrimitives)
      throw std::runtime_error(
          "FP scene did not submit the selected item surface");
    UStudioTarget result(512, 512, p.Mode == 1);
    Blit(fp.Texture, result.Fbo, 1280, 720, 0, 112, 512, 288);
    return result.Take();
  }
  UStudioTarget result(512, 512, p.Mode == 1);
  if (p.Scene == 7)
  {
    LastDiagnosticVertices = Overlay->DrawUvTiles(*asset);
    LastDrawCount = LastDiagnosticVertices > 0 ? 1 : 0;
  }
  else if (p.Scene == 5)
  {
    LastDrawCount = 3;
    int column = 0;
    for (int size : {32, 48, 64})
    {
      const auto icon = orbit.RenderInspectionTexture(
          id, path, size, 35, 25, p.Mode, p.Ambient, p.Inputs);
      glBindTexture(GL_TEXTURE_2D, icon);
      GLint width = 0, height = 0;
      glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
      glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
      if (width != size || height != size)
      {
        glDeleteTextures(1, &icon);
        throw std::runtime_error(
            "Item Lab thumbnail has incorrect native dimensions");
      }
      Blit(icon, result.Fbo, size, size, 80 + column * 160 - size / 2,
           256 - size / 2, size, size);
      glDeleteTextures(1, &icon);
      ++column;
    }
  }
  else
  {
    const auto *def = Items->Get(id);
    if (!def)
      throw std::runtime_error("Missing item definition");
    const auto extent = asset->High - asset->Low;
    const auto focus = glm::vec3(0, extent.y * .5f, 0);
    const float yaw = glm::radians(p.Yaw), pitch = glm::radians(p.Pitch);
    const auto eye =
        focus + glm::vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                          std::cos(yaw) * std::cos(pitch)) *
                    std::max(.15f, glm::length(extent)) * 1.7f;
    const auto vp = glm::perspective(glm::radians(45.f), 1.f, .01f, 100.f) *
                    glm::lookAt(eye, focus, glm::vec3(0, 1, 0));
    CreatureDrawQueue queue;
    WornEquipmentDrawer::SubmitLoose(
        queue, id, vp,
        glm::translate(glm::mat4(1),
                       glm::vec3(0, def->Physical.size.y * .5f, 0)),
        def->Physical.size, 0, true, p.Inputs);
    UStudioSink sink(*Surface);
    sink.Profile.CameraPosition = eye;
    sink.Profile.InspectionMode = p.Mode;
    sink.Profile.Ambient = p.Ambient;
    queue.Flush(sink);
    LastDrawCount = sink.Count;
  }
  return result.Take();
}
} // namespace cutum

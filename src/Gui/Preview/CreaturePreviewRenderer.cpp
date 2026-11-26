#include "Gui/Preview/CreaturePreviewRenderer.h"
#include "Core/Memory/VisualAssetCensus.h"

#include "App/Settings/RenderSettings.h"
#include "Core/Memory/StlSourceStorage.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonHierarchy.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonModelSpace.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonCache.h"
#include "Creatures/Visual/CreatureAppearance.h"
#include "Creatures/Visual/CreatureBonePaletteGpu.h"
#include "Creatures/Visual/CreatureDrawContext.h"
#include "Creatures/Visual/CreatureDrawQueue.h"
#include "Creatures/Visual/CreatureDrawSink.h"
#include "Creatures/Visual/CreatureMeshGpuCache.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/CreatureRootTransform.h"
#include "Creatures/Visual/CreatureTextureResolver.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Creatures/Visual/CreatureVisual.h"
#include "Creatures/Visual/CreatureVisualFactory.h"
#include "Creatures/Visual/Gltf/CreatureAnimationClipMap.h"
#include "Creatures/Visual/Gltf/CreatureGltfBonePalette.h"
#include "Creatures/Visual/Gltf/CreatureGltfCache.h"
#include "Creatures/Visual/Gltf/CreatureGltfModelSpace.h"
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Gui/Preview/CreatureGltfPreviewBounds.h"
#include "Gui/Preview/CreaturePreviewBoundsSink.h"
#include "Gui/Preview/CreaturePreviewLayout.h"
#include "Gui/Preview/PreviewRendererStorage.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ItemDrawRequest.h"
#include "Items/ItemSurfaceRenderer.h"
#include "Pose/BoneSkeleton/BoneSkeletonPoseEngine.h"
#include "Pose/CreaturePosePresenterRegistry.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/Engine/RenderImageStorage.h"
#include "Render/Pipeline/GlStateMask.h"
#include "Render/Pipeline/GlStateScope.h"
#include "Render/Textures/GlTextureOwner.h"

#include "Render/GlIncludes.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace cutum
{

namespace
{

constexpr float kOrbitDistance = 3.8f;
constexpr float kFovDeg = 35.0f;
std::unordered_set<std::string> gPreviewSkeletalMissingLogged;
std::unordered_set<std::string> gPreviewGltfMissingLogged;

bool UploadIconCubeMesh(PreviewCubeMesh &mesh,
                        const float *texCoords)
{
  float vertices[24 * 5];
  for (int v = 0; v < 24; ++v)
  {
    vertices[v * 5 + 0] = kCreaturePartPositions[v * 3 + 0];
    vertices[v * 5 + 1] = kCreaturePartPositions[v * 3 + 1];
    vertices[v * 5 + 2] = kCreaturePartPositions[v * 3 + 2];
    vertices[v * 5 + 3] = texCoords[v * 2 + 0];
    vertices[v * 5 + 4] = texCoords[v * 2 + 1];
  }
  return mesh.Initialize(vertices, kCreaturePartIndices);
}

void UploadPreviewSkinnedBones(class UShaderProgram &shader,
                               const std::vector<glm::mat4> &bones)
{
  if (bones.empty())
  {
    return;
  }
  BindCreatureBonePaletteBlock(shader.GetProgramID(),
                               kCreatureBonePaletteBindingPoint);
  UploadCreatureBonePaletteGpu(bones);
}

} // namespace

UCreaturePreviewRenderer::UCreaturePreviewRenderer(
    std::shared_ptr<UCreatureDefinitionStorage> species,
    std::shared_ptr<USkinDefinitionStorage> skins,
    std::shared_ptr<UCreatureTextureStorage> textures,
    std::shared_ptr<UShaderManager> shaderManager)
    : Species(std::move(species)), Skins(std::move(skins)),
      Textures(std::move(textures)), ShaderManager(std::move(shaderManager))
{
}

UCreaturePreviewRenderer::~UCreaturePreviewRenderer() { Shutdown(); }

std::shared_ptr<UCreaturePreviewRenderer> UCreaturePreviewRenderer::Create(
    std::shared_ptr<UCreatureDefinitionStorage> species, std::shared_ptr<USkinDefinitionStorage> skins,
    std::shared_ptr<UCreatureTextureStorage> textures, std::shared_ptr<UShaderManager> shaderManager)
{
  auto renderer = std::make_shared<UCreaturePreviewRenderer>(
      std::move(species), std::move(skins), std::move(textures), std::move(shaderManager));
  renderer->FactoryOwner = true;
  return renderer;
}

void UCreaturePreviewRenderer::InspectVisualAssets(VisualAssetCensus &census) const
{
  if (Species)
    Species->InspectVisualAssets(census, VisualAssetOwner::Preview);
  for (const auto &entry : GltfBoundsCache)
    census.Add(entry.second.asset, VisualAssetOwner::Preview);
  if (ItemRenderer)
    ItemRenderer->InspectVisualAssets(census, VisualAssetOwner::Preview);
}

void UCreaturePreviewRenderer::InspectGpuBuffers(TerrainGpuBufferCensus &out) const
{
  for (const auto *mesh : {&Cube, &HeadCube, &BodyCube, &RigidHeadCube})
    mesh->InspectGpuBuffers(out);
  if (ItemRenderer)
    ItemRenderer->InspectGpuBuffers(out);
}

void UCreaturePreviewRenderer::InspectRenderImages(RenderImageCensus &out) const
{
  out.Add(RenderImageRole::CreaturePreviewColor, GL_TEXTURE_2D, Target.Color);
  out.Add(RenderImageRole::CreaturePreviewDepth, GL_RENDERBUFFER, Target.Depth);
}

PreviewRendererStorage UCreaturePreviewRenderer::InspectOwnedStorage(
    const UCreatureDefinitionStorage *species, const USkinDefinitionStorage *skins,
    const UCreatureTextureStorage *textures, const UShaderManager *shaders) const noexcept
{
  PreviewRendererStorage out;
  out.present = true;
  out.factoryOwner = FactoryOwner;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  if (FactoryOwner)
    out.requestBytes = sizeof(std::_Ref_count_obj2<UCreaturePreviewRenderer>);
#endif
  StlSourceStorage tables, total;
  tables.Hash(GltfBoundsCache);
  for (const auto &[key, bounds] : GltfBoundsCache)
  {
    tables.Text(key);
    out.assetReferences += bool(bounds.asset);
  }
  tables.Hash(ProceduralBoundsCache);
  for (const auto &bounds : ProceduralBoundsCache)
    tables.Text(bounds.first);
  if (ItemRenderer)
    out.surface = ItemRenderer->InspectOwnedStorage();
  out.tableBytes = tables.bytes.requested;
  total.Add(out.requestBytes);
  total.Add(out.tableBytes);
  total.Add(out.surface.cpuBytes);
  out.cpuBytes = total.bytes.requested;
  const bool target = InspectPreviewTarget(out, Target, FboSize);
  bool meshes = true;
  for (const auto *mesh : {&Cube, &HeadCube, &BodyCube, &RigidHeadCube})
    meshes &= InspectPreviewCube(out, *mesh);
  out.supported = species == Species.get() && skins == Skins.get() && textures == Textures.get() &&
      shaders == ShaderManager.get() && FactoryOwner && target && meshes &&
      (out.meshes == 0 || out.meshes == 4) && (!ItemRenderer || out.surface.supported) &&
      !AttachmentObserver && !AttachmentBoundsObserver && !QaSubmissionObserver && !ItemSurfaceObserver &&
      !SkeletonPoseObserver && tables.bytes.supported && total.bytes.supported;
  return out;
}

bool UCreaturePreviewRenderer::InitCubeMesh()
{
  if (Cube.Vao != 0)
  {
    return true;
  }
  float boxUv[48];
  float headUv[48];
  float bodyUv[48];
  float rigidHeadUv[48];
  BuildCreatureBoxTexCoords(boxUv);
  BuildCreatureHeadTexCoords(headUv);
  BuildCreatureBodyTexCoords(bodyUv);
  BuildCreatureRigidHeadTexCoords(rigidHeadUv);
  PreviewCubeMesh cube, head, body, rigidHead;
  if (!UploadIconCubeMesh(cube, boxUv) || !UploadIconCubeMesh(head, headUv) ||
      !UploadIconCubeMesh(body, bodyUv) || !UploadIconCubeMesh(rigidHead, rigidHeadUv))
    return false;
  Cube.Swap(cube);
  HeadCube.Swap(head);
  BodyCube.Swap(body);
  RigidHeadCube.Swap(rigidHead);
  return true;
}

bool UCreaturePreviewRenderer::Initialize()
{
  if (!ShaderManager || !Species || !Skins || !Textures)
  {
    return false;
  }
  Shader =
      ShaderManager->CreateShader("creature_preview", "shaders/vshader.glsl",
                                  "shaders/fshader_creature.glsl");
  SkinnedShader = ShaderManager->CreateShader(
      "creature_preview_skinned", "shaders/vshader_creature_skinned.glsl",
      "shaders/fshader_creature_skinned.glsl");
  if (!Shader || !Shader->IsValid() || !SkinnedShader ||
      !SkinnedShader->IsValid() || !InitCubeMesh())
  {
    return false;
  }
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  InitCreatureBonePaletteGpu();
  BindCreatureBonePaletteBlock(SkinnedShader->GetProgramID(),
                               kCreatureBonePaletteBindingPoint);
#endif
  ItemRenderer = std::make_unique<ItemSurfaceRenderer>();
  return ItemRenderer->Initialize(*ShaderManager) && EnsureFboSize(256);
}

void UCreaturePreviewRenderer::Invalidate()
{
  if (ItemRenderer)
    ItemRenderer->Clear();
  GltfBoundsCache.clear();
  ProceduralBoundsCache.clear();
  Target.Reset();
  FboSize = 0;
}

void UCreaturePreviewRenderer::Shutdown()
{
  Invalidate();
  ItemRenderer.reset();
  Shader.reset();
  SkinnedShader.reset();
  Cube.Reset();
  HeadCube.Reset();
  BodyCube.Reset();
  RigidHeadCube.Reset();
}

bool UCreaturePreviewRenderer::EnsureFboSize(int size)
{
  const int clamped = std::clamp(size, 64, 512);
  if (Target.Framebuffer && FboSize == clamped)
    return true;
  if (!Target.Initialize(clamped))
    return false;
  FboSize = clamped;
  return true;
}

glm::mat4 UCreaturePreviewRenderer::OrbitView(float yawDeg, float pitchDeg,
                                              float distance) const
{
  const float yaw = glm::radians(yawDeg);
  const float pitch = glm::radians(pitchDeg);
  const float cosPitch = std::cos(pitch);
  const glm::vec3 eye(distance * cosPitch * std::sin(yaw),
                      distance * std::sin(pitch),
                      distance * cosPitch * std::cos(yaw));
  // Exact top/bottom inspection must not use an up vector parallel to view.
  const glm::vec3 up = std::abs(cosPitch) < .001f
                           ? glm::vec3(0.f, 0.f, pitchDeg > 0.f ? -1.f : 1.f)
                           : glm::vec3(0.f, 1.f, 0.f);
  return glm::lookAt(eye, glm::vec3(0.0f), up);
}

GLuint UCreaturePreviewRenderer::TryGetDirectSpeciesIcon(
    const std::string &speciesId) const
{
  if (!Species || !Textures || speciesId.empty())
  {
    return 0;
  }
  const CreatureDefinition *def = Species->Get(speciesId);
  if (!def)
  {
    return 0;
  }
  const GLuint iconTex = Textures->GetTexture(speciesId + "/icon");
  if (iconTex == 0)
  {
    return 0;
  }
  // Respect per-species icon mode: species/skin texture icons can be used
  // directly regardless of visual backend.
  if (def->visual.iconMode == "species_texture" ||
      def->visual.iconMode == "skin_texture")
  {
    return iconTex;
  }
  return 0;
}

GLuint UCreaturePreviewRenderer::CreateSolidColorTexture(int size, float r,
                                                         float g, float b,
                                                         float a)
{
  const int clamped = std::max(1, std::min(size, 512));
  UGlStateScope glState(kGlMaskIconFbo | GlStateBit::DepthMask);
  if (!EnsureFboSize(FboSize > 0 ? FboSize : 256))
  {
    return 0;
  }

  GLuint tex = 0;
  glGenTextures(1, &tex);
  UGlTextureOwner texture(tex);
  if (!tex)
    return 0;
  glBindTexture(GL_TEXTURE_2D, tex);
  AllocatePreviewColor(clamped);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  UGlIconColorAttachment attachment(Target.Framebuffer, tex, Target.Color, Target.Depth, clamped, FboSize);
  if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    return 0;
  glViewport(0, 0, clamped, clamped);
  glClearColor(r, g, b, a);
  glClear(GL_COLOR_BUFFER_BIT);

  return texture.Release();
}

// Queue sink for the preview FBO: draws creature requests with the preview
// shaders and the shared GPU mesh cache (audit 2.5). Wireframes are not
// shown in icon previews.
class UPreviewCreatureDrawSink final : public cutum::ICreatureDrawSink
{
public:
  UPreviewCreatureDrawSink(
      cutum::UShaderProgram *shader, cutum::UShaderProgram *skinnedShader,
      GLuint cubeVao, GLuint headVao, GLuint bodyVao, GLuint rigidHeadVao,
      cutum::ItemSurfaceRenderer &items, const glm::vec3 &camera,
      const std::function<void(const cutum::ItemDrawRequest &)> &observer,
      int mode, float ambient)
      : Shader(shader), SkinnedShader(skinnedShader), CubeVao(cubeVao),
        HeadVao(headVao), BodyVao(bodyVao), RigidHeadVao(rigidHeadVao),
        ItemRenderer(items), ItemObserver(observer)
  {
    ItemProfile.CameraPosition = camera;
    ItemProfile.InspectionMode = mode;
    ItemProfile.Ambient = ambient;
  }
  void DrawItemSurface(const cutum::ItemDrawRequest &request) override
  {
    if (ItemObserver)
      ItemObserver(request);
    ItemRenderer.Draw(request.Asset, request.Path, request.ViewProjection,
                      request.Model, request.State, ItemProfile);
  }

  void DrawTexturedPart(const glm::mat4 &mvp, GLuint texture,
                        cutum::CreaturePartMesh mesh) override
  {
    if (!Shader || !Shader->IsValid() || texture == 0)
    {
      return;
    }
    GLuint vao = CubeVao;
    if (mesh == cutum::CreaturePartMesh::Head)
    {
      vao = HeadVao;
    }
    else if (mesh == cutum::CreaturePartMesh::Body)
    {
      vao = BodyVao;
    }
    else if (mesh == cutum::CreaturePartMesh::RigidHead)
    {
      vao = RigidHeadVao;
    }
    DrawWithShader(*Shader, vao, mvp, texture, 36, glm::vec4(1.f));
  }

  void DrawBoneSkeletonMesh(const glm::mat4 &mvp, GLuint texture,
                            const cutum::BoneSkeletonCubeMeshCpu &mesh) override
  {
    if (!Shader || !Shader->IsValid() || texture == 0 ||
        mesh.interleavedPosUv.empty() || mesh.indices.empty())
    {
      return;
    }
    const GLuint vao =
        cutum::CreatureMeshGpuCache::Instance().GetOrCreateSkeletalMeshVao(
            mesh);
    DrawWithShader(*Shader, vao, mvp, texture,
                   static_cast<GLsizei>(mesh.indices.size()), glm::vec4(1.f));
  }

  void DrawSkinnedMesh(const glm::mat4 &mvp, GLuint texture,
                       const cutum::GltfPrimitiveCpu &mesh,
                       const std::shared_ptr<const std::vector<glm::mat4>>
                           &boneMatrices) override
  {
    if (!SkinnedShader || !SkinnedShader->IsValid() || texture == 0 ||
        mesh.mesh.interleavedPosUv.empty() || mesh.mesh.indices.empty() ||
        !boneMatrices || boneMatrices->empty())
    {
      return;
    }
    size_t indexCount = 0;
    const GLuint vao =
        cutum::CreatureMeshGpuCache::Instance().GetOrCreateGltfSkinnedMeshVao(
            mesh, indexCount);
    if (vao == 0)
    {
      return;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    SkinnedShader->Use();
    SkinnedShader->SetInt("texture0", 0);
    SkinnedShader->SetMat4("mvp_matrix", mvp);
    UploadPreviewSkinnedBones(*SkinnedShader, *boneMatrices);
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount),
                   GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    SkinnedShader->Unuse();
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  void DrawBoxWireframe(const glm::mat4 &mvp, const glm::vec4 &color) override
  {
    (void)mvp;
    (void)color;
  }

  void DrawBillboard(const glm::mat4 &mvp, GLuint texture,
                     const glm::vec4 &tint) override
  {
    if (!Shader || !Shader->IsValid() || texture == 0)
    {
      return;
    }
    // First cube face has full-tile UVs. Collapse its depth to the same
    // camera-facing plane used by the world billboard pass.
    DrawWithShader(*Shader, CubeVao,
                   mvp * glm::scale(glm::mat4(1.f), glm::vec3(1.f, 1.f, 0.f)),
                   texture, 6, tint);
  }

private:
  void DrawWithShader(cutum::UShaderProgram &shader, GLuint vao,
                      const glm::mat4 &mvp, GLuint texture, GLsizei count,
                      const glm::vec4 &tint)
  {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    shader.Use();
    shader.SetInt("texture0", 0);
    shader.SetVec4("uTint", tint);
    shader.SetMat4("mvp_matrix", mvp);
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    shader.Unuse();
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  cutum::UShaderProgram *Shader;
  cutum::UShaderProgram *SkinnedShader;
  GLuint CubeVao;
  GLuint HeadVao;
  GLuint BodyVao;
  GLuint RigidHeadVao;
  cutum::ItemSurfaceRenderer &ItemRenderer;
  cutum::ItemPresentationProfile ItemProfile;
  const std::function<void(const cutum::ItemDrawRequest &)> &ItemObserver;
};

bool UCreaturePreviewRenderer::DrawSpeciesParts(
    const std::string &speciesId, const std::string &skinId, int viewportSize,
    float yawDeg, float pitchDeg, float animTimeSec, bool animateWalk,
    const std::array<WornArmorPreviewSlot, kWornArmorSlotCount> *armor,
    const std::string *mainItemId, const std::string *offhandItemId,
    const HumanAppearance *human, const ItemVisualInputs *mainInputs,
    const ItemVisualInputs *offInputs)
{
  if (!Species || !Skins || !Textures || !Shader || viewportSize <= 0)
  {
    return false;
  }
  if (ItemRenderer && ItemRevision != UItemDefinitionStorage::ContentRevision())
  {
    ItemRenderer->Clear();
    ItemRevision = UItemDefinitionStorage::ContentRevision();
  }
  const CreatureDefinition *def = Species->Get(speciesId);
  if (!def)
  {
    return false;
  }

  const uint64_t definitionsRevision =
      Species->Revision.load(std::memory_order_relaxed);
  const uint64_t boneRevision =
      CreatureBoneSkeletonCache::Instance().GetRevision();
  const uint64_t gltfRevision = CreatureGltfCache::Instance().GetRevision();
  if (BoundsDefinitionsRevision != definitionsRevision ||
      BoundsBoneRevision != boneRevision || BoundsGltfRevision != gltfRevision)
  {
    ProceduralBoundsCache.clear();
    if (ItemRenderer)
      ItemRenderer->Clear();
    GltfBoundsCache.clear();
    BoundsDefinitionsRevision = definitionsRevision;
    BoundsBoneRevision = boneRevision;
    BoundsGltfRevision = gltfRevision;
  }

  const CreatureVisualBackend backend =
      ParseCreatureVisualBackend(def->visual.backend);
  if (backend == CreatureVisualBackend::GltfSkeleton ||
      backend == CreatureVisualBackend::BoneSkeleton ||
      backend == CreatureVisualBackend::RigidVoxels)
  {
    // Shared visual backends draw the preview (audit 2.5): a throwaway
    // creature standing at the origin, preview locomotion facts, and the
    // submitted requests flushed through the preview sink. This replaces
    // per-backend re-implementations with their own VAO caches.
    const glm::vec3 eyeOffset(0.0f, def->eyeHeight, 0.0f);
    UCreature previewCreature(1, speciesId, glm::vec3(0.0f), eyeOffset);
    previewCreature.GetBoundsMutable().profile = def->bounds;
    previewCreature.GetBoundsMutable().currentSizeBlocks =
        def->bounds.restSizeBlocks;
    if (!skinId.empty())
    {
      previewCreature.SetSkinId(skinId);
    }

    if (human)
      previewCreature.SetHumanAppearance(*human);
    if (speciesId == "human")
    {
      previewCreature.GetInventory().SetItemDefinitions(
          WornEquipmentDrawer::ItemDefinitions());
      if (mainItemId && !mainItemId->empty())
        previewCreature.GetInventory().AssignToHotbar(
            0, 0, {InventoryEntryKind::Item, *mainItemId, 1, false});
      if (offhandItemId && !offhandItemId->empty())
        previewCreature.GetInventory().EquipOffhand(
            {InventoryEntryKind::Item, *offhandItemId, 1, false});
      const auto *items = WornEquipmentDrawer::ItemDefinitions();
      const auto *item =
          items && mainItemId ? items->Get(*mainItemId) : nullptr;
      if (item && mainInputs)
      {
        if (item->Ranged.Enabled)
          previewCreature.BowCharge = mainInputs->BowCharge;
        if (item->Use.Action == ItemUseActionKind::Eat ||
            item->Use.Action == ItemUseActionKind::Drink)
          if (const auto *entry =
                  previewCreature.GetInventory().GetActiveEntryRef();
              entry && mainInputs->UsePhase > 0)
            previewCreature.ItemUse.Update(
                entry->Id, entry->instanceId,
                previewCreature.ItemVisualSelection(), mainInputs->UsePhase,
                1.f);
      }
    }
    CreatureLocomotionFacts facts;
    facts.supportValid[0] = facts.supportValid[1] = true;
    facts.animPhase =
        animTimeSec * def->visual.Animation.walkCycleHz * 6.283185307179586f;
    if (animateWalk)
    {
      facts.state = AnimationPreviewState.value_or(LocomotionState::Walk);
      facts.horizontalSpeed =
          def->locomotion.walkSpeed * AnimationPreviewSpeedRatio;
      if (facts.state == LocomotionState::Crouch)
        facts.stanceBlend = 1.f;
      if (facts.state == LocomotionState::Jump)
      {
        facts.onGround = false;
        facts.verticalSpeed = 2.f;
      }
      if (facts.state == LocomotionState::Swim)
      {
        facts.onGround = false;
        facts.inFluid = true;
        facts.swimming = true;
      }
    }
    else
    {
      facts.state = LocomotionState::Idle;
    }

    if (facts.state == LocomotionState::Fly ||
        facts.state == LocomotionState::Hover)
    {
      facts.mode = CreatureMovementMode::Flying;
      facts.onGround = false;
    }
    if (speciesId == "human" && facts.state == LocomotionState::Hover)
      facts.horizontalSpeed = 0.f;
    if (speciesId == "human" && facts.state == LocomotionState::Tread)
    {
      facts.onGround = false;
      facts.inFluid = true;
      facts.swimming = true;
      facts.horizontalSpeed = 0.f;
    }
    if (facts.state == LocomotionState::Fall)
    {
      facts.onGround = false;
      facts.verticalSpeed = -6.f;
    }
    facts.actualVelocity = {0.f, facts.verticalSpeed, facts.horizontalSpeed};
    auto seedHumanPreview =
        [&](const CreatureLocomotionFacts &f, bool sampling = false)
    {
      previewCreature.HumanMotion = {};
      previewCreature.HumanMotion.distancePhase = f.animPhase;
      previewCreature.HumanMotion.idlePhase =
          sampling ? f.animPhase : animTimeSec * 2.5f;
      if (f.state == LocomotionState::Action)
        previewCreature.HumanMotion.actionRemaining = .14f;
    };
    seedHumanPreview(facts);
    std::unique_ptr<IUCreatureVisual> visual = CreateCreatureVisual(*def);
    auto appearance =
        ResolveCreatureAppearance(*Species, *Skins, speciesId, skinId,
                                  previewCreature.GetHumanAppearance());
    appearance.hideHair = armor && !(*armor)[0].ItemId.empty();
    visual->SetAppearance(appearance);
    // Fresh visual each render: advance its animation clock by the preview
    // anim time in one step.
    UCreaturePosePresenterRegistry presenters;
    RegisterDefaultCreaturePosePresenters(presenters);
    CreaturePoseParams presented;
    if (auto *presenter = presenters.Get(def->locomotionArchetype))
    {
      presented = presenter->Compute(facts, *def, 0.f);
    }
    visual->UpdatePose(previewCreature, facts, presented, *def, 0.f);

    RenderSettings previewSettings;
    CreatureDrawQueue queue;
    CreatureDrawContext ctx;
    ctx.Queue = &queue;
    ctx.Textures = Textures.get();
    ctx.Settings = &previewSettings;
    ctx.SkipWieldedItems = true;

    // Collision bounds can be shorter than ears or a raised head. Fit the
    // rendered rest geometry without changing gameplay bounds or world scale.
    glm::vec3 previewSize = def->bounds.restSizeBlocks;
    glm::vec3 previewCenter(0.f, previewSize.y * .5f, 0.f);
    bool geometryFit = false;
    if (backend != CreatureVisualBackend::GltfSkeleton)
    {
      const std::string key = speciesId + "|" + skinId + "|" +
                              previewCreature.GetHumanAppearance().Key() +
                              (appearance.hideHair ? "|helmet|" : "|") +
                              ToString(facts.state) +
                              (animateWalk ? "|cycle|" : "|rest|") +
                              std::to_string(AnimationPreviewSpeedRatio) + "|" +
                              std::to_string(Species->Revision.load()) + "|" +
                              std::to_string(Skins->Revision.load());
      auto cached = ProceduralBoundsCache.find(key);
      if (cached == ProceduralBoundsCache.end())
      {
        UCreaturePreviewBoundsSink bounds;
        CreatureDrawQueue boundsQueue;
        CreatureDrawContext boundsContext = ctx;
        boundsContext.Queue = &boundsQueue;
        boundsContext.CameraPosition = glm::vec3(0.f, 0.f, kOrbitDistance);
        const int samples = animateWalk ? 64 : 0;
        for (int sample = 0; sample <= samples; ++sample)
        {
          auto sampledFacts = facts;
          sampledFacts.animPhase =
              samples ? sample * 6.283185307179586f / samples : 0.f;
          CreaturePoseParams sampledPose;
          if (auto *presenter = presenters.Get(def->locomotionArchetype))
            sampledPose = presenter->Compute(sampledFacts, *def, 0.f);
          seedHumanPreview(sampledFacts, true);
          visual->UpdatePose(previewCreature, sampledFacts, sampledPose, *def,
                             0.f);
          visual->SubmitDraw(boundsContext, glm::mat4(1.f));
          boundsQueue.Flush(bounds);
          boundsQueue.Clear();
        }
        if (bounds.any)
          cached = ProceduralBoundsCache
                       .emplace(key, std::make_pair(bounds.low, bounds.high))
                       .first;
        seedHumanPreview(facts);
        visual->UpdatePose(previewCreature, facts, presented, *def, 0.f);
      }
      if (cached != ProceduralBoundsCache.end())
      {
        previewSize = cached->second.second - cached->second.first;
        previewCenter = (cached->second.second + cached->second.first) * .5f;
        geometryFit = true;
      }
    }
    if (backend == CreatureVisualBackend::GltfSkeleton)
    {
      const auto asset = CreatureGltfCache::Instance().Load(
          speciesId,
          def->visual.gltf.modelPath.empty() ? "model.gltf"
                                             : def->visual.gltf.modelPath,
          def->visual.sourceDirectory);
      if (asset)
      {
        const std::string clipName =
            ResolveAnimationClipId(facts.state, def->visual.Animation.stateMap)
                .value_or("idle");
        const auto clip = asset->animationIndexByName.find(clipName);
        const auto *animation = clip == asset->animationIndexByName.end()
                                    ? nullptr
                                    : &asset->animations[clip->second];
        const std::string key =
            speciesId + "|" + clipName + (animateWalk ? "|cycle" : "|pose0");
        auto cached = GltfBoundsCache.find(key);
        if (cached == GltfBoundsCache.end() ||
            cached->second.asset.get() != asset.get())
        {
          if (const auto bounds =
                  ComputeGltfPreviewBounds(*asset, animation, animateWalk))
            cached =
                GltfBoundsCache
                    .insert_or_assign(key, GltfBoundsEntry{asset, bounds->first,
                                                           bounds->second})
                    .first;
        }
        if (cached != GltfBoundsCache.end() &&
            cached->second.asset.get() == asset.get())
        {
          const auto &bounds = cached->second;
          const glm::mat4 root = BuildCreatureRootMatrix(
              glm::vec3(0.f), def->visual.modelYawOffsetDeg,
              def->visual.gltf.modelOffsetY -
                  asset->bindMinY * def->visual.gltf.modelScale,
              []() { return GltfEntityConventionMatrix(); },
              def->visual.gltf.modelScale);
          glm::vec3 low(std::numeric_limits<float>::max()),
              high(-std::numeric_limits<float>::max());
          for (int x : {0, 1})
            for (int y : {0, 1})
              for (int z : {0, 1})
              {
                const glm::vec3 point(
                    root * glm::vec4(x ? bounds.high.x : bounds.low.x,
                                     y ? bounds.high.y : bounds.low.y,
                                     z ? bounds.high.z : bounds.low.z, 1.f));
                low = glm::min(low, point);
                high = glm::max(high, point);
              }
          previewSize = high - low;
          previewCenter = (high + low) * .5f;
          geometryFit = true;
        }
      }
    }
    if (QaFramingBounds)
    {
      previewSize = QaFramingBounds->second - QaFramingBounds->first;
      previewCenter = (QaFramingBounds->second + QaFramingBounds->first) * .5f;
      geometryFit = true;
    }
    float fitScale = std::max(
        0.1f, std::max({previewSize.x, previewSize.y, previewSize.z}) / 1.8f);
    if (geometryFit)
      fitScale = std::max(fitScale, glm::length(previewSize) * .55f /
                                        (std::sin(glm::radians(kFovDeg * .5f)) *
                                         kOrbitDistance));
    const glm::mat4 projection =
        glm::perspective(glm::radians(kFovDeg), 1.0f, 0.1f,
                         std::max(30.f, kOrbitDistance * fitScale * 3.f));
    const glm::mat4 view =
        OrbitView(yawDeg, pitchDeg, kOrbitDistance * fitScale) *
        glm::translate(glm::mat4(1.0f), -previewCenter);
    const glm::mat4 viewProj = QaViewProjection.value_or(projection * view);
    ctx.CameraPosition =
        QaCameraPosition.value_or(glm::vec3(glm::inverse(view)[3]));

    visual->SubmitDraw(ctx, viewProj);
    if (SkeletonPoseObserver)
      SkeletonPoseObserver(*visual);
    if (queue.Size() == 0)
    {
      const std::string key = speciesId + "|preview";
      if (gPreviewGltfMissingLogged.insert(key).second)
      {
        std::cerr << "CreaturePreviewRenderer: no drawable parts for species="
                  << speciesId << std::endl;
      }
      return false;
    }

    const glm::vec4 bg = ItemInspectionMode == 1
                             ? glm::vec4(.95f)
                             : def->visual.wireframeColor * 0.25f;
    glClearColor(bg.r, bg.g, bg.b, TransparentBackground ? 0.f : 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    UPreviewCreatureDrawSink drawSink(
        Shader.get(), SkinnedShader.get(), Cube.Vao, HeadCube.Vao, BodyCube.Vao,
        RigidHeadCube.Vao, *ItemRenderer, ctx.CameraPosition,
        ItemSurfaceObserver, ItemInspectionMode, ItemInspectionAmbient);

    // Armor / wielded overlays keep their own submission, aligned with the
    // backend root matrix the body was drawn with.
    const bool wantsEquipment = (armor != nullptr) ||
                                (mainItemId && !mainItemId->empty()) ||
                                (offhandItemId && !offhandItemId->empty());
    if (backend == CreatureVisualBackend::BoneSkeleton && wantsEquipment)
    {
      // Reuse the body's actual cached pose and common root (including ground
      // clearance). Re-evaluating the skeleton here used to leave overlays
      // below the body in phases where its feet needed a small lift.
      const glm::mat4 bodyMat(1.f);
      const BoneMatrixLookupFn boneLookup = [&](const std::string &boneName,
                                                glm::mat4 &out) -> bool
      {
        if (!visual->GetWorldBoneTransform(boneName, out))
          return false;
        if (AttachmentObserver)
          AttachmentObserver(boneName, out);
        return true;
      };
      const BoneBoundsLookupFn bounds =
          [&](const std::string &name, glm::vec3 &low, glm::vec3 &high)
      {
        if (!visual->GetBoneLocalBounds(name, low, high))
          return false;
        if (AttachmentBoundsObserver)
          AttachmentBoundsObserver(name, low, high);
        return true;
      };
      if (armor)
        WornEquipmentDrawer::SubmitPreview(queue, *armor, viewProj, bodyMat,
                                           boneLookup, bounds);
      if ((mainItemId && !mainItemId->empty()) ||
          (offhandItemId && !offhandItemId->empty()))
        WornEquipmentDrawer::SubmitWieldedPreview(
            &queue, nullptr, mainItemId ? *mainItemId : std::string(),
            offhandItemId ? *offhandItemId : std::string(),
            WornEquipmentDrawer::ItemDefinitions(), viewProj, bodyMat,
            boneLookup, false, bounds,
            mainInputs
                ? *mainInputs
                : MakeItemVisualInputs(
                      previewCreature.GetInventory().GetActiveEntryRef()),
            offInputs
                ? *offInputs
                : MakeItemVisualInputs(
                      &previewCreature.GetInventory().GetEquippedOffhand()));
    }
    if (QaSubmissionObserver)
      QaSubmissionObserver(queue, viewProj);
    queue.Flush(drawSink);
    return true;
  }

  const ResolvedCreatureAppearance appearance =
      ResolveCreatureAppearance(*Species, *Skins, speciesId, skinId);

  const glm::vec4 bg = ItemInspectionMode == 1
                           ? glm::vec4(.95f)
                           : def->visual.wireframeColor * 0.25f;
  glClearColor(bg.r, bg.g, bg.b, TransparentBackground ? 0.f : 1.f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  const CreaturePreviewFit fit = FitCreaturePreview(appearance.Parts);
  const glm::mat4 projection =
      glm::perspective(glm::radians(kFovDeg), 1.0f, 0.1f, 30.0f);
  const glm::mat4 view = OrbitView(yawDeg, pitchDeg, kOrbitDistance);

  Shader->Use();
  Shader->SetInt("texture0", 0);
  for (const ResolvedCreaturePart &part : appearance.Parts)
  {
    const GLuint tex = ResolveCreaturePartTexture(*Textures, part);
    if (tex == 0)
    {
      continue;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    GLuint partVao = Cube.Vao;
    if (ParseCreatureTextureLayout(appearance.textureLayout) ==
        CreatureTextureLayout::PlayerSkinAtlas)
    {
      if (part.partId == "head")
      {
        partVao = HeadCube.Vao;
      }
      else if (part.partId == "torso")
      {
        partVao = BodyCube.Vao;
      }
    }
    else if (UsesRigidFaceTexture(part.baseTextureAssetKey.empty()
                                      ? part.textureAssetKey
                                      : part.baseTextureAssetKey))
    {
      partVao = RigidHeadCube.Vao;
    }
    glBindVertexArray(partVao);
    const glm::mat4 model = CreaturePreviewPartModel(fit, part);
    Shader->SetMat4("mvp_matrix", projection * view * model);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
  }

  glBindVertexArray(0);
  Shader->Unuse();
  return true;
}

GLuint UCreaturePreviewRenderer::Render(const std::string &speciesId,
                                        const std::string &skinId, int size,
                                        float yawDeg, float pitchDeg)
{
  if (speciesId.empty() || !Shader || !EnsureFboSize(size))
  {
    return 0;
  }

  UGlStateScope glState(kGlMaskIconFbo | GlStateBit::DepthMask);
  glBindFramebuffer(GL_FRAMEBUFFER, Target.Framebuffer);
  glViewport(0, 0, FboSize, FboSize);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDisable(GL_CULL_FACE);

  if (!DrawSpeciesParts(speciesId, skinId, FboSize, yawDeg, pitchDeg))
  {
    return 0;
  }
  return Target.Color;
}

GLuint UCreaturePreviewRenderer::RenderToUniqueTexture(
    const std::string &speciesId, const std::string &skinId, int size,
    float yawDeg, float pitchDeg, float animTimeSec, bool animateWalk,
    const std::array<WornArmorPreviewSlot, kWornArmorSlotCount> *armor,
    const std::string *mainItemId, const std::string *offhandItemId,
    const HumanAppearance *human, const ItemVisualInputs *mainInputs,
    const ItemVisualInputs *offInputs)
{
  if (speciesId.empty() || !Shader)
  {
    return 0;
  }
  const int clamped = std::max(64, std::min(size, 512));

  UGlStateScope glState(kGlMaskIconFbo | GlStateBit::DepthMask);
  if (Target.Framebuffer == 0 && !EnsureFboSize(clamped))
    return 0;
  GLuint tex = 0;
  glGenTextures(1, &tex);
  UGlTextureOwner texture(tex);
  if (!tex)
    return 0;
  glBindTexture(GL_TEXTURE_2D, tex);
  AllocatePreviewColor(clamped);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  UGlIconColorAttachment attachment(Target.Framebuffer, tex, Target.Color, Target.Depth, clamped, FboSize);
  if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    return 0;
  glViewport(0, 0, clamped, clamped);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDisable(GL_CULL_FACE);

  if (!DrawSpeciesParts(speciesId, skinId, clamped, yawDeg, pitchDeg,
                        animTimeSec, animateWalk, armor, mainItemId,
                        offhandItemId, human, mainInputs, offInputs))
    return 0;
  return texture.Release();
}

} // namespace cutum

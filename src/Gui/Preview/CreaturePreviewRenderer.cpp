#include "Gui/Preview/CreaturePreviewRenderer.h"

#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include "App/Settings/RenderSettings.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Visual/CreatureAppearance.h"
#include "Creatures/Visual/CreatureDrawContext.h"
#include "Creatures/Visual/CreatureDrawQueue.h"
#include "Creatures/Visual/CreatureDrawSink.h"
#include "Creatures/Visual/CreatureVisual.h"
#include "Creatures/Visual/CreatureVisualFactory.h"
#include "Creatures/Visual/CreatureMeshGpuCache.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/CreatureRootTransform.h"
#include "Creatures/Visual/CreatureTextureResolver.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Creatures/Visual/CreatureBonePaletteGpu.h"
#include "Creatures/Visual/Gltf/CreatureGltfBonePalette.h"
#include "Creatures/Visual/Gltf/CreatureGltfCache.h"
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonHierarchy.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonCache.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonModelSpace.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Gui/Preview/CreaturePreviewLayout.h"
#include "Pose/BoneSkeleton/BoneSkeletonPoseEngine.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/Pipeline/GlStateMask.h"
#include "Render/Pipeline/GlStateScope.h"

#include "Render/GlIncludes.h"
#include <algorithm>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
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

void UploadIconCubeMesh(GLuint &vao, GLuint &vbo, GLuint &ebo,
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
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glGenBuffers(1, &ebo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kCreaturePartIndices),
               kCreaturePartIndices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        reinterpret_cast<void *>(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glBindVertexArray(0);
}

void UploadPreviewSkinnedBones(class UShaderProgram &shader,
                               const std::vector<glm::mat4> &bones)
{
  if (bones.empty())
  {
    return;
  }
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  constexpr int kMaxSkinnedBoneUniforms = 48;
  const int boneCount = static_cast<int>(
      std::min<size_t>(bones.size(), kMaxSkinnedBoneUniforms));
  for (int i = 0; i < boneCount; ++i)
  {
    shader.SetMat4("uBones[" + std::to_string(i) + "]",
                   bones[static_cast<size_t>(i)]);
  }
#else
  BindCreatureBonePaletteBlock(shader.GetProgramID(),
                               kCreatureBonePaletteBindingPoint);
  UploadCreatureBonePaletteGpu(bones);
#endif
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

bool UCreaturePreviewRenderer::InitCubeMesh()
{
  if (CubeVao != 0)
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
  UploadIconCubeMesh(CubeVao, CubeVbo, CubeEbo, boxUv);
  UploadIconCubeMesh(HeadCubeVao, HeadCubeVbo, HeadCubeEbo, headUv);
  UploadIconCubeMesh(BodyCubeVao, BodyCubeVbo, BodyCubeEbo, bodyUv);
  UploadIconCubeMesh(RigidHeadCubeVao, RigidHeadCubeVbo, RigidHeadCubeEbo,
                     rigidHeadUv);
  return CubeVao != 0 && HeadCubeVao != 0 && BodyCubeVao != 0 &&
         RigidHeadCubeVao != 0;
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
  return EnsureFboSize(256);
}

void UCreaturePreviewRenderer::Invalidate()
{
  if (DepthRbo != 0)
  {
    glDeleteRenderbuffers(1, &DepthRbo);
    DepthRbo = 0;
  }
  if (ColorTex != 0)
  {
    glDeleteTextures(1, &ColorTex);
    ColorTex = 0;
  }
  if (Fbo != 0)
  {
    glDeleteFramebuffers(1, &Fbo);
    Fbo = 0;
  }
  FboSize = 0;
}

void UCreaturePreviewRenderer::Shutdown()
{
  Invalidate();
  Shader.reset();
  if (BodyCubeEbo != 0)
  {
    glDeleteBuffers(1, &BodyCubeEbo);
    BodyCubeEbo = 0;
  }
  if (BodyCubeVbo != 0)
  {
    glDeleteBuffers(1, &BodyCubeVbo);
    BodyCubeVbo = 0;
  }
  if (BodyCubeVao != 0)
  {
    glDeleteVertexArrays(1, &BodyCubeVao);
    BodyCubeVao = 0;
  }
  if (RigidHeadCubeEbo != 0)
  {
    glDeleteBuffers(1, &RigidHeadCubeEbo);
    RigidHeadCubeEbo = 0;
  }
  if (RigidHeadCubeVbo != 0)
  {
    glDeleteBuffers(1, &RigidHeadCubeVbo);
    RigidHeadCubeVbo = 0;
  }
  if (RigidHeadCubeVao != 0)
  {
    glDeleteVertexArrays(1, &RigidHeadCubeVao);
    RigidHeadCubeVao = 0;
  }
  if (HeadCubeEbo != 0)
  {
    glDeleteBuffers(1, &HeadCubeEbo);
    HeadCubeEbo = 0;
  }
  if (HeadCubeVbo != 0)
  {
    glDeleteBuffers(1, &HeadCubeVbo);
    HeadCubeVbo = 0;
  }
  if (HeadCubeVao != 0)
  {
    glDeleteVertexArrays(1, &HeadCubeVao);
    HeadCubeVao = 0;
  }
  if (CubeEbo != 0)
  {
    glDeleteBuffers(1, &CubeEbo);
    CubeEbo = 0;
  }
  if (CubeVbo != 0)
  {
    glDeleteBuffers(1, &CubeVbo);
    CubeVbo = 0;
  }
  if (CubeVao != 0)
  {
    glDeleteVertexArrays(1, &CubeVao);
    CubeVao = 0;
  }
}

bool UCreaturePreviewRenderer::EnsureFboSize(int size)
{
  const int clamped = std::max(64, std::min(size, 512));
  if (Fbo != 0 && FboSize == clamped)
  {
    return true;
  }

  Invalidate();

  glGenFramebuffers(1, &Fbo);
  glGenTextures(1, &ColorTex);
  glBindTexture(GL_TEXTURE_2D, ColorTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, clamped, clamped, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindFramebuffer(GL_FRAMEBUFFER, Fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         ColorTex, 0);

  glGenRenderbuffers(1, &DepthRbo);
  glBindRenderbuffer(GL_RENDERBUFFER, DepthRbo);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, clamped,
                        clamped);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, DepthRbo);

  const bool complete =
      glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  FboSize = complete ? clamped : 0;
  return complete;
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
  return glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
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
  if (!EnsureFboSize(FboSize > 0 ? FboSize : 256))
  {
    return 0;
  }

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, clamped, clamped, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  UGlStateScope glState(kGlMaskIconFbo);
  glBindFramebuffer(GL_FRAMEBUFFER, Fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         tex, 0);
  if (DepthRbo != 0 && FboSize != clamped)
  {
    glBindRenderbuffer(GL_RENDERBUFFER, DepthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, clamped,
                          clamped);
  }
  glViewport(0, 0, clamped, clamped);
  glClearColor(r, g, b, a);
  glClear(GL_COLOR_BUFFER_BIT);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         ColorTex, 0);
  if (DepthRbo != 0 && FboSize != clamped)
  {
    glBindRenderbuffer(GL_RENDERBUFFER, DepthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, FboSize,
                          FboSize);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}

// Queue sink for the preview FBO: draws creature requests with the preview
// shaders and the shared GPU mesh cache (audit 2.5). Wireframes are not
// shown in icon previews.
class PreviewCreatureDrawSink final : public cutum::ICreatureDrawSink
{
public:
  PreviewCreatureDrawSink(cutum::UShaderProgram *shader,
                          cutum::UShaderProgram *skinnedShader, GLuint cubeVao,
                          GLuint headVao, GLuint bodyVao, GLuint rigidHeadVao)
      : Shader(shader), SkinnedShader(skinnedShader), CubeVao(cubeVao),
        HeadVao(headVao), BodyVao(bodyVao), RigidHeadVao(rigidHeadVao) {}

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
    const GLuint vao = cutum::CreatureMeshGpuCache::Instance()
                           .GetOrCreateSkeletalMeshVao(mesh);
    DrawWithShader(*Shader, vao, mvp, texture,
                   static_cast<GLsizei>(mesh.indices.size()), glm::vec4(1.f));
  }

  void DrawSkinnedMesh(
      const glm::mat4 &mvp, GLuint texture, const cutum::GltfPrimitiveCpu &mesh,
      const std::shared_ptr<const std::vector<glm::mat4>> &boneMatrices) override
  {
    if (!SkinnedShader || !SkinnedShader->IsValid() || texture == 0 ||
        mesh.mesh.interleavedPosUv.empty() || mesh.mesh.indices.empty() ||
        !boneMatrices || boneMatrices->empty())
    {
      return;
    }
    size_t indexCount = 0;
    const GLuint vao = cutum::CreatureMeshGpuCache::Instance()
                           .GetOrCreateGltfSkinnedMeshVao(mesh, indexCount);
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

  void DrawBoxWireframe(const glm::mat4 &mvp,
                        const glm::vec4 &color) override
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
    // Billboards render as a tinted textured cube in icon previews.
    DrawWithShader(*Shader, CubeVao, mvp, texture, 36, tint);
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
    shader.SetInt("uAnimFrame", 0);
    shader.SetInt("uAnimFrameCount", 1);
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
};

bool UCreaturePreviewRenderer::DrawSpeciesParts(const std::string &speciesId,
                                                const std::string &skinId,
                                                int viewportSize, float yawDeg,
                                                float pitchDeg,
                                                float animTimeSec,
                                                bool animateWalk,
                                                const std::array<WornArmorPreviewSlot, 6> *armor,
                                                const std::string *mainItemId,
                                                const std::string *offhandItemId)
{
  if (!Species || !Skins || !Textures || !Shader || viewportSize <= 0)
  {
    return false;
  }
  const CreatureDefinition *def = Species->Get(speciesId);
  if (!def)
  {
    return false;
  }

  const CreatureVisualBackend backend =
      ParseCreatureVisualBackend(def->visual.backend);
  if (backend == CreatureVisualBackend::GltfSkeleton ||
      backend == CreatureVisualBackend::BoneSkeleton)
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

    CreatureLocomotionFacts facts;
    if (animateWalk)
    {
      facts.state = LocomotionState::Walk;
      facts.horizontalSpeed = def->locomotion.walkSpeed;
    }
    else
    {
      facts.state = LocomotionState::Idle;
    }

    std::unique_ptr<IUCreatureVisual> visual = CreateCreatureVisual(*def);
    visual->SetAppearance(
        ResolveCreatureAppearance(*Species, *Skins, speciesId, skinId));
    // Fresh visual each render: advance its animation clock by the preview
    // anim time in one step.
    visual->UpdatePose(previewCreature, facts, CreaturePoseParams{}, *def,
                       animTimeSec);

    RenderSettings previewSettings;
    CreatureDrawQueue queue;
    CreatureDrawContext ctx;
    ctx.Queue = &queue;
    ctx.Textures = Textures.get();
    ctx.Settings = &previewSettings;

    // Backends place the model on its feet at the origin growing along +Y;
    // aim the orbit camera at mid-height and scale distance by body size.
    const float height = std::max(def->bounds.restSizeBlocks.y, 0.1f);
    const float fitScale = glm::clamp(height / 1.8f, 0.55f, 3.0f);
    const glm::mat4 projection =
        glm::perspective(glm::radians(kFovDeg), 1.0f, 0.1f, 30.0f);
    const glm::mat4 view =
        OrbitView(yawDeg, pitchDeg, kOrbitDistance * fitScale) *
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, height * 0.5f, 0.0f));
    const glm::mat4 viewProj = projection * view;

    visual->SubmitDraw(ctx, viewProj);
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

    const glm::vec4 bg = def->visual.wireframeColor * 0.25f;
    glClearColor(bg.r, bg.g, bg.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    PreviewCreatureDrawSink drawSink(Shader.get(), SkinnedShader.get(),
                                     CubeVao, HeadCubeVao, BodyCubeVao,
                                     RigidHeadCubeVao);
    queue.Flush(drawSink);

    // Armor / wielded overlays keep their own submission, aligned with the
    // backend root matrix the body was drawn with.
    const bool wantsEquipment =
        (armor != nullptr) || (mainItemId && !mainItemId->empty()) ||
        (offhandItemId && !offhandItemId->empty());
    if (backend == CreatureVisualBackend::BoneSkeleton && wantsEquipment)
    {
      const std::string geoFile =
          def->visual.boneSkeleton.geometryFile.empty()
              ? "geometry.geo.json"
              : def->visual.boneSkeleton.geometryFile;
      const auto meshAsset = CreatureBoneSkeletonCache::Instance().Load(
          speciesId, geoFile, def->visual.boneSkeleton.geometryId);
      if (meshAsset)
      {
        BoneSkeletonHierarchy hierarchy(meshAsset->geometry);
        const BoneSkeletonPose pose =
            BoneSkeletonPoseEngine::Compute(facts, *def, 0.f);
        const glm::mat4 bodyMat =
            BuildCreatureRootMatrix(glm::vec3(0.0f),
                                    def->visual.modelYawOffsetDeg, 0.f, []()
                                    { return BoneSkeletonEntityConventionMatrix(); });
        const BoneMatrixLookupFn boneLookup =
            [&](const std::string &boneName, glm::mat4 &out) -> bool
        {
          const auto it = meshAsset->geometry.boneIndexByName.find(boneName);
          if (it == meshAsset->geometry.boneIndexByName.end())
          {
            return false;
          }
          out = hierarchy.ComputeBoneMatrix(it->second, pose);
          return true;
        };
        if (armor)
        {
          WornEquipmentDrawer::DrawImmediate(*armor, viewProj, bodyMat,
                                             boneLookup, Shader.get());
        }
        if ((mainItemId && !mainItemId->empty()) ||
            (offhandItemId && !offhandItemId->empty()))
        {
          WornEquipmentDrawer::SubmitWieldedPreview(
              nullptr, Shader.get(),
              mainItemId ? *mainItemId : std::string(),
              offhandItemId ? *offhandItemId : std::string(),
              WornEquipmentDrawer::ItemDefinitions(), viewProj, bodyMat,
              boneLookup, true);
        }
      }
    }
    return true;
  }

  const ResolvedCreatureAppearance appearance =
      ResolveCreatureAppearance(*Species, *Skins, speciesId, skinId);

  const glm::vec4 bg = def->visual.wireframeColor * 0.25f;
  glClearColor(bg.r, bg.g, bg.b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  const CreaturePreviewFit fit = FitCreaturePreview(appearance.Parts);
  const glm::mat4 projection =
      glm::perspective(glm::radians(kFovDeg), 1.0f, 0.1f, 30.0f);
  const glm::mat4 view = OrbitView(yawDeg, pitchDeg, kOrbitDistance);

  Shader->Use();
  Shader->SetInt("texture0", 0);
  Shader->SetInt("uAnimFrame", 0);
  Shader->SetInt("uAnimFrameCount", 1);
  for (const ResolvedCreaturePart &part : appearance.Parts)
  {
    const GLuint tex = Textures->GetTexture(part.textureAssetKey);
    if (tex == 0)
    {
      continue;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    GLuint partVao = CubeVao;
    if (ParseCreatureTextureLayout(appearance.textureLayout) ==
        CreatureTextureLayout::PlayerSkinAtlas)
    {
      if (part.partId == "head")
      {
        partVao = HeadCubeVao;
      }
      else if (part.partId == "torso")
      {
        partVao = BodyCubeVao;
      }
    }
    else if (UsesRigidFaceTexture(part.textureAssetKey))
    {
      partVao = RigidHeadCubeVao;
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

  UGlStateScope glState(kGlMaskIconFbo);
  glBindFramebuffer(GL_FRAMEBUFFER, Fbo);
  glViewport(0, 0, FboSize, FboSize);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDisable(GL_CULL_FACE);

  if (!DrawSpeciesParts(speciesId, skinId, FboSize, yawDeg, pitchDeg))
  {
    return 0;
  }
  return ColorTex;
}

GLuint UCreaturePreviewRenderer::RenderToUniqueTexture(
    const std::string &speciesId, const std::string &skinId, int size,
    float yawDeg, float pitchDeg, float animTimeSec, bool animateWalk,
    const std::array<WornArmorPreviewSlot, 6> *armor,
    const std::string *mainItemId, const std::string *offhandItemId)
{
  if (speciesId.empty() || !Shader)
  {
    return 0;
  }
  const int clamped = std::max(64, std::min(size, 512));

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, clamped, clamped, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  if (Fbo == 0 && !EnsureFboSize(clamped))
  {
    glDeleteTextures(1, &tex);
    return 0;
  }

  UGlStateScope glState(kGlMaskIconFbo);
  glBindFramebuffer(GL_FRAMEBUFFER, Fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         tex, 0);
  if (DepthRbo != 0 && FboSize != clamped)
  {
    glBindRenderbuffer(GL_RENDERBUFFER, DepthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, clamped,
                          clamped);
  }
  glViewport(0, 0, clamped, clamped);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDisable(GL_CULL_FACE);

  if (!DrawSpeciesParts(speciesId, skinId, clamped, yawDeg, pitchDeg,
                        animTimeSec, animateWalk, armor, mainItemId,
                        offhandItemId))
  {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteTextures(1, &tex);
    return 0;
  }

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         ColorTex, 0);
  if (DepthRbo != 0 && FboSize != clamped)
  {
    glBindRenderbuffer(GL_RENDERBUFFER, DepthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, FboSize,
                          FboSize);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return tex;
}

} // namespace cutum

#include "Gui/Preview/CreaturePreviewRenderer.h"

#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include "Creatures/Visual/CreatureAppearance.h"
#include "Creatures/Visual/CreatureMeshGpuCache.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/CreatureRootTransform.h"
#include "Creatures/Visual/CreatureTextureResolver.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Creatures/Visual/Gltf/CreatureGltfBonePalette.h"
#include "Creatures/Visual/Gltf/CreatureGltfCache.h"
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonHierarchy.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonCache.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonModelSpace.h"
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

GLuint GetOrCreatePreviewSkeletalVao(const cutum::BoneSkeletonCubeMeshCpu &mesh,
                                     std::unordered_map<size_t, GLuint> &cache)
{
  (void)cache;
  return cutum::CreatureMeshGpuCache::Instance().GetOrCreateSkeletalMeshVao(
      mesh);
}

GLuint GetOrCreatePreviewSkinnedVao(const cutum::GltfPrimitiveCpu &mesh,
                                    std::unordered_map<size_t, GLuint> &cache)
{
  (void)cache;
  size_t indexCount = 0;
  return cutum::CreatureMeshGpuCache::Instance().GetOrCreateGltfSkinnedMeshVao(
      mesh, indexCount);
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
  // Flat icon.png is only valid for rigid_voxels; bedrock_geo and gltf_skeleton
  // must render icons from the same 3D backend as world/preview.
  if (ParseCreatureVisualBackend(def->visual.backend) !=
      CreatureVisualBackend::RigidVoxels)
  {
    return 0;
  }
  const GLuint iconTex = Textures->GetTexture(speciesId + "/icon");
  if (iconTex == 0)
  {
    return 0;
  }
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

bool UCreaturePreviewRenderer::DrawSpeciesParts(const std::string &speciesId,
                                                const std::string &skinId,
                                                int viewportSize, float yawDeg,
                                                float pitchDeg)
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

  if (ParseCreatureVisualBackend(def->visual.backend) ==
      CreatureVisualBackend::GltfSkeleton)
  {
    const std::string modelFile = def->visual.gltf.modelPath.empty()
                                      ? "model.gltf"
                                      : def->visual.gltf.modelPath;
    const auto meshAsset =
        CreatureGltfCache::Instance().Load(speciesId, modelFile);
    const std::string defaultTex = def->visual.defaultTextureKey.empty()
                                       ? "body"
                                       : def->visual.defaultTextureKey;

    const glm::vec4 bg = def->visual.wireframeColor * 0.25f;
    glClearColor(bg.r, bg.g, bg.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!meshAsset)
    {
      const std::string key = speciesId + "|" + modelFile;
      if (gPreviewGltfMissingLogged.insert(key).second)
      {
        std::cerr << "CreaturePreviewRenderer: missing glTF mesh for species="
                  << speciesId << " model=" << modelFile << std::endl;
      }
      return false;
    }

    const glm::mat4 projection =
        glm::perspective(glm::radians(kFovDeg), 1.0f, 0.1f, 30.0f);
    const glm::mat4 view = OrbitView(yawDeg, pitchDeg, kOrbitDistance);
    const float feetOffsetY =
        def->visual.gltf.modelOffsetY - meshAsset->bindMinY;
    glm::mat4 bodyMat = GltfPreviewRootMatrix(*meshAsset, 1.2f, feetOffsetY);
    const float modelScale = def->visual.gltf.modelScale;
    if (modelScale != 1.f)
    {
      bodyMat = bodyMat * glm::scale(glm::mat4(1.f), glm::vec3(modelScale));
    }

    static std::unordered_map<size_t, GLuint> previewGltfVaoCache;
    static std::unordered_map<size_t, GLuint> previewGltfSkinnedVaoCache;
    const glm::mat4 mvp = projection * view * bodyMat;
    const std::vector<glm::mat4> bindPoseBones =
        meshAsset->hasSkin
            ? ComputeGltfSkinMatrices(*meshAsset, nullptr, 0.f, true)
            : std::vector<glm::mat4>{};

    bool drewAny = false;
    std::vector<std::string> missingTextureStems;
    for (const GltfPrimitiveCpu &prim : meshAsset->primitives)
    {
      const GLuint tex = cutum::ResolveCreatureSpeciesTexture(
          *Textures, speciesId, prim.textureStem, defaultTex);
      if (tex == 0)
      {
        missingTextureStems.push_back(prim.textureStem);
        continue;
      }

      const BoneSkeletonCubeMeshCpu &cube = prim.mesh;
      if (cube.interleavedPosUv.empty() || cube.indices.empty())
      {
        continue;
      }

      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, tex);

      if (prim.skinned && SkinnedShader && SkinnedShader->IsValid() &&
          !bindPoseBones.empty())
      {
        SkinnedShader->Use();
        SkinnedShader->SetInt("texture0", 0);
        SkinnedShader->SetMat4("mvp_matrix", mvp);
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
        constexpr int kMaxSkinnedBoneUniforms = 48;
#else
        constexpr int kMaxSkinnedBoneUniforms = 64;
#endif
        const int boneCount = static_cast<int>(
            std::min<size_t>(bindPoseBones.size(), kMaxSkinnedBoneUniforms));
        for (int i = 0; i < boneCount; ++i)
        {
          SkinnedShader->SetMat4("uBones[" + std::to_string(i) + "]",
                                 bindPoseBones[static_cast<size_t>(i)]);
        }
        const GLuint vao =
            GetOrCreatePreviewSkinnedVao(prim, previewGltfSkinnedVaoCache);
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(cube.indices.size()),
                       GL_UNSIGNED_INT, nullptr);
        SkinnedShader->Unuse();
      }
      else if (prim.skinned && !bindPoseBones.empty())
      {
        Shader->Use();
        Shader->SetInt("texture0", 0);
        Shader->SetInt("uAnimFrame", 0);
        Shader->SetInt("uAnimFrameCount", 1);
        const GLuint vao =
            GetOrCreatePreviewSkeletalVao(cube, previewGltfVaoCache);
        glBindVertexArray(vao);
        Shader->SetMat4("mvp_matrix", mvp);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(cube.indices.size()),
                       GL_UNSIGNED_INT, nullptr);
        Shader->Unuse();
      }
      else
      {
        Shader->Use();
        Shader->SetInt("texture0", 0);
        Shader->SetInt("uAnimFrame", 0);
        Shader->SetInt("uAnimFrameCount", 1);
        const GLuint vao =
            GetOrCreatePreviewSkeletalVao(cube, previewGltfVaoCache);
        glBindVertexArray(vao);
        Shader->SetMat4("mvp_matrix", mvp);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(cube.indices.size()),
                       GL_UNSIGNED_INT, nullptr);
        Shader->Unuse();
      }
      drewAny = true;
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!drewAny)
    {
      const std::string key = speciesId + "|textures";
      if (gPreviewGltfMissingLogged.insert(key).second)
      {
        std::cerr
            << "CreaturePreviewRenderer: missing glTF textures for species="
            << speciesId;
        if (!missingTextureStems.empty())
        {
          std::cerr << " stems:";
          for (const std::string &stem : missingTextureStems)
          {
            std::cerr << " " << stem;
          }
        }
        std::cerr << std::endl;
      }
      return false;
    }
    return true;
  }

  if (ParseCreatureVisualBackend(def->visual.backend) ==
      CreatureVisualBackend::BoneSkeleton)
  {
    const std::string geoFile = def->visual.boneSkeleton.geometryFile.empty()
                                    ? "geometry.geo.json"
                                    : def->visual.boneSkeleton.geometryFile;
    const auto meshAsset = CreatureBoneSkeletonCache::Instance().Load(
        speciesId, geoFile, def->visual.boneSkeleton.geometryId);
    const std::string texStem = def->visual.boneSkeleton.textureStem.empty()
                                    ? "diffuse"
                                    : def->visual.boneSkeleton.textureStem;
    const std::string defaultTex = def->visual.defaultTextureKey.empty()
                                       ? "body"
                                       : def->visual.defaultTextureKey;
    GLuint tex = cutum::ResolveCreatureSpeciesTexture(
        *Textures, speciesId, texStem, defaultTex, skinId);
    if (!meshAsset || tex == 0)
    {
      const std::string key = speciesId + "|" + geoFile + "|" +
                              def->visual.boneSkeleton.geometryId + "|" + texStem;
      if (gPreviewSkeletalMissingLogged.insert(key).second)
      {
        std::cerr << "CreaturePreviewRenderer: missing bone_skeleton "
                  << (!meshAsset ? "mesh" : "texture")
                  << " for species=" << speciesId << " geoFile=" << geoFile
                  << " geometryId=" << def->visual.boneSkeleton.geometryId
                  << " texStem=" << texStem << " skin=" << skinId << std::endl;
      }
      return false;
    }

    const glm::vec4 bg = def->visual.wireframeColor * 0.25f;
    glClearColor(bg.r, bg.g, bg.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const glm::mat4 projection =
        glm::perspective(glm::radians(kFovDeg), 1.0f, 0.1f, 30.0f);
    const glm::mat4 view = OrbitView(yawDeg, pitchDeg, kOrbitDistance);
    const glm::mat4 bodyMat =
        BoneSkeletonPreviewRootMatrix(meshAsset->geometry, 1.2f);

    CreatureLocomotionFacts facts;
    facts.state = LocomotionState::Idle;
    facts.animPhase = 0.f;
    const BoneSkeletonPose pose =
        BoneSkeletonPoseEngine::Compute(facts, *def, 0.f);
    BoneSkeletonHierarchy hierarchy(meshAsset->geometry);

    static std::unordered_map<size_t, GLuint> previewSkeletalVaoCache;
    Shader->Use();
    Shader->SetInt("texture0", 0);
    Shader->SetInt("uAnimFrame", 0);
    Shader->SetInt("uAnimFrameCount", 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    for (size_t boneIdx = 0; boneIdx < meshAsset->boneMeshes.size(); ++boneIdx)
    {
      const glm::mat4 boneMat =
          bodyMat * hierarchy.ComputeBoneMatrix(boneIdx, pose);
      for (const BoneSkeletonCubeMeshCpu &cube :
           meshAsset->boneMeshes[boneIdx].cubes)
      {
        if (cube.interleavedPosUv.empty() || cube.indices.empty())
        {
          continue;
        }
        const GLuint vao =
            GetOrCreatePreviewSkeletalVao(cube, previewSkeletalVaoCache);
        glBindVertexArray(vao);
        Shader->SetMat4("mvp_matrix",
                        projection * view * boneMat * cube.restLocalMatrix);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(cube.indices.size()),
                       GL_UNSIGNED_INT, nullptr);
      }
    }
    glBindVertexArray(0);
    Shader->Unuse();
    glBindTexture(GL_TEXTURE_2D, 0);
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
    float yawDeg, float pitchDeg)
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

  if (!DrawSpeciesParts(speciesId, skinId, clamped, yawDeg, pitchDeg))
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

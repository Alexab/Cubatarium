#include "Render/Engine/CreatureDrawPass.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include "Creatures/Stats/CreatureVitals.h"
#include "Creatures/Visual/CreatureBonePaletteGpu.h"
#include "Creatures/Visual/CreatureMeshGpuCache.h"
#include "Creatures/Visual/CreatureVisibility.h"
#include "Creatures/Visual/CreatureDrawContext.h"
#include "Creatures/Visual/CreatureEngineDrawSink.h"
#include "Creatures/Visual/CreatureVisual.h"
#include "Pose/CreaturePoseParams.h"
#include "Pose/IUCreaturePosePresenter.h"
#include "Render/Camera/Camera.h"
#include "Render/Camera/CameraPerspective.h"
#include "Render/Camera/Frustum.h"
#include "Core/StallTimer.h"
#include "Render/Engine/EnvironmentLight.h"
#include "Render/Engine/GeometryEngine.h"
#include "Render/Engine/TextRenderer.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Lighting/LightUtil.h"
#include "Render/GlIncludes.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/World.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace cutum
{

namespace
{

bool UploadCreaturePartMesh(GLuint &vao, GLuint &vbo, GLuint &ebo,
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

  if (vao == 0)
  {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
  }
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kCreaturePartIndices),
               kCreaturePartIndices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glBindVertexArray(0);
  return vao != 0;
}

} // namespace

bool UCreatureDrawPass::InitBuffers(UShaderManager &shader_manager)
{
  creatureShader = shader_manager.CreateShader(
      "creature", "shaders/vshader.glsl", "shaders/fshader_creature.glsl");
  if (!creatureShader || !creatureShader->IsValid())
  {
    std::cerr << "Failed to create creature shader" << std::endl;
    return false;
  }

  creatureSkinnedShader = shader_manager.CreateShader(
      "creature_skinned", "shaders/vshader_creature_skinned.glsl",
      "shaders/fshader_creature_skinned.glsl");
  if (!creatureSkinnedShader || !creatureSkinnedShader->IsValid())
  {
    std::cerr << "Failed to create creature skinned shader" << std::endl;
    return false;
  }

  DestroyBuffers();
  InitCreatureBonePaletteGpu();
  BindCreatureBonePaletteBlock(creatureSkinnedShader->GetProgramID(),
                               kCreatureBonePaletteBindingPoint);
  return InitPartBuffers() && InitHeadPartBuffers() && InitBodyPartBuffers();
}

void UCreatureDrawPass::DestroyBuffers()
{
  if (creatureRigidHeadPartEBO)
  {
    glDeleteBuffers(1, &creatureRigidHeadPartEBO);
    creatureRigidHeadPartEBO = 0;
  }
  if (creatureRigidHeadPartVBO)
  {
    glDeleteBuffers(1, &creatureRigidHeadPartVBO);
    creatureRigidHeadPartVBO = 0;
  }
  if (creatureRigidHeadPartVAO)
  {
    glDeleteVertexArrays(1, &creatureRigidHeadPartVAO);
    creatureRigidHeadPartVAO = 0;
  }
  if (creatureBodyPartEBO)
  {
    glDeleteBuffers(1, &creatureBodyPartEBO);
    creatureBodyPartEBO = 0;
  }
  if (creatureBodyPartVBO)
  {
    glDeleteBuffers(1, &creatureBodyPartVBO);
    creatureBodyPartVBO = 0;
  }
  if (creatureBodyPartVAO)
  {
    glDeleteVertexArrays(1, &creatureBodyPartVAO);
    creatureBodyPartVAO = 0;
  }
  if (creatureHeadPartEBO)
  {
    glDeleteBuffers(1, &creatureHeadPartEBO);
    creatureHeadPartEBO = 0;
  }
  if (creatureHeadPartVBO)
  {
    glDeleteBuffers(1, &creatureHeadPartVBO);
    creatureHeadPartVBO = 0;
  }
  if (creatureHeadPartVAO)
  {
    glDeleteVertexArrays(1, &creatureHeadPartVAO);
    creatureHeadPartVAO = 0;
  }
  if (creaturePartEBO)
  {
    glDeleteBuffers(1, &creaturePartEBO);
    creaturePartEBO = 0;
  }
  if (creaturePartVBO)
  {
    glDeleteBuffers(1, &creaturePartVBO);
    creaturePartVBO = 0;
  }
  if (creaturePartVAO)
  {
    glDeleteVertexArrays(1, &creaturePartVAO);
    creaturePartVAO = 0;
  }
  if (creatureBillboardEBO)
  {
    glDeleteBuffers(1, &creatureBillboardEBO);
    creatureBillboardEBO = 0;
  }
  if (creatureBillboardVBO)
  {
    glDeleteBuffers(1, &creatureBillboardVBO);
    creatureBillboardVBO = 0;
  }
  if (creatureBillboardVAO)
  {
    glDeleteVertexArrays(1, &creatureBillboardVAO);
    creatureBillboardVAO = 0;
  }
  DestroyCreatureBonePaletteGpu();
}

bool UCreatureDrawPass::InitPartBuffers()
{
  if (creaturePartVAO != 0)
  {
    return true;
  }
  float texCoords[48];
  BuildCreatureBoxTexCoords(texCoords);
  return UploadCreaturePartMesh(creaturePartVAO, creaturePartVBO,
                                creaturePartEBO, texCoords);
}

bool UCreatureDrawPass::InitHeadPartBuffers()
{
  if (creatureHeadPartVAO != 0)
  {
    return true;
  }
  float texCoords[48];
  BuildCreatureHeadTexCoords(texCoords);
  return UploadCreaturePartMesh(creatureHeadPartVAO, creatureHeadPartVBO,
                                creatureHeadPartEBO, texCoords);
}

bool UCreatureDrawPass::InitBodyPartBuffers()
{
  if (creatureBodyPartVAO != 0)
  {
    return true;
  }
  float texCoords[48];
  BuildCreatureBodyTexCoords(texCoords);
  return UploadCreaturePartMesh(creatureBodyPartVAO, creatureBodyPartVBO,
                                creatureBodyPartEBO, texCoords);
}

bool UCreatureDrawPass::InitRigidHeadPartBuffers()
{
  if (creatureRigidHeadPartVAO != 0)
  {
    return true;
  }
  float texCoords[48];
  BuildCreatureRigidHeadTexCoords(texCoords);
  return UploadCreaturePartMesh(creatureRigidHeadPartVAO,
                                creatureRigidHeadPartVBO,
                                creatureRigidHeadPartEBO, texCoords);
}

bool UCreatureDrawPass::InitBillboardBuffers()
{
  if (creatureBillboardVAO != 0)
  {
    return true;
  }
  const float vertices[] = {
      -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f,  0.0f, 0.0f, 1.0f, 0.0f,
      0.5f,  1.0f, 0.0f, 1.0f, 1.0f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f,
  };
  const unsigned int indices[] = {0, 1, 2, 0, 2, 3};
  glGenVertexArrays(1, &creatureBillboardVAO);
  glGenBuffers(1, &creatureBillboardVBO);
  glGenBuffers(1, &creatureBillboardEBO);
  glBindVertexArray(creatureBillboardVAO);
  glBindBuffer(GL_ARRAY_BUFFER, creatureBillboardVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, creatureBillboardEBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
               GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glBindVertexArray(0);
  return creatureBillboardVAO != 0;
}

void UCreatureDrawPass::DrawTexturedPart(const glm::mat4 &mvp, GLuint texture,
                                         CreaturePartMesh mesh)
{
  if (texture == 0 || !creatureShader || !creatureShader->IsValid())
  {
    return;
  }
  GLuint vao = 0;
  switch (mesh)
  {
  case CreaturePartMesh::Head:
    if (creatureHeadPartVAO == 0 && !InitHeadPartBuffers())
    {
      return;
    }
    vao = creatureHeadPartVAO;
    break;
  case CreaturePartMesh::Body:
    if (creatureBodyPartVAO == 0 && !InitBodyPartBuffers())
    {
      return;
    }
    vao = creatureBodyPartVAO;
    break;
  case CreaturePartMesh::RigidHead:
    if (creatureRigidHeadPartVAO == 0 && !InitRigidHeadPartBuffers())
    {
      return;
    }
    vao = creatureRigidHeadPartVAO;
    break;
  case CreaturePartMesh::Box:
  default:
    if (creaturePartVAO == 0 && !InitPartBuffers())
    {
      return;
    }
    vao = creaturePartVAO;
    break;
  }

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);

  glBindTexture(GL_TEXTURE_2D, texture);
  creatureShader->Use();
  creatureShader->SetInt("texture0", 0);
  creatureShader->SetInt("uAnimFrame", 0);
  creatureShader->SetInt("uAnimFrameCount", 1);
  creatureShader->SetVec4(
      "uTint", glm::vec4(1.0f + ActiveHitFlash01, 1.0f + ActiveHitFlash01,
                         1.0f + ActiveHitFlash01, 1.0f));
  creatureShader->SetMat4("mvp_matrix", mvp);
  creatureShader->SetVec2("uCreatureLight", ActiveLight);

  glBindVertexArray(vao);
  glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);

  creatureShader->Unuse();
  glBindTexture(GL_TEXTURE_2D, 0);
  ++Stats.CreatureDrawCalls;
}

void UCreatureDrawPass::DrawBillboard(const glm::mat4 &mvp, GLuint texture,
                                      const glm::vec4 &tint)
{
  if (texture == 0 || !creatureShader || !creatureShader->IsValid())
  {
    return;
  }
  if (creatureBillboardVAO == 0 && !InitBillboardBuffers())
  {
    return;
  }

  GLboolean blendEnabled;
  glGetBooleanv(GL_BLEND, &blendEnabled);
  GLint blendSrcRgb, blendDstRgb, blendSrcAlpha, blendDstAlpha;
  glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);

  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);

  glBindTexture(GL_TEXTURE_2D, texture);
  creatureShader->Use();
  creatureShader->SetInt("texture0", 0);
  creatureShader->SetInt("uAnimFrame", 0);
  creatureShader->SetInt("uAnimFrameCount", 1);
  creatureShader->SetVec4("uTint", tint);
  creatureShader->SetMat4("mvp_matrix", mvp);
  // Glow/effect billboards are not lit by the world.
  creatureShader->SetFloat("uCreatureLitEnabled", 0.0f);
  glBindVertexArray(creatureBillboardVAO);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);
  creatureShader->SetFloat("uCreatureLitEnabled", LightingActive ? 1.0f : 0.0f);
  creatureShader->Unuse();
  glBindTexture(GL_TEXTURE_2D, 0);

  glBlendFunc(blendSrcRgb, blendDstRgb);
  glBlendFuncSeparate(blendSrcRgb, blendDstRgb, blendSrcAlpha, blendDstAlpha);
  if (!blendEnabled)
  {
    glDisable(GL_BLEND);
  }
  ++Stats.CreatureDrawCalls;
}

void UCreatureDrawPass::DrawBoneSkeletonMesh(
    const glm::mat4 &mvp, GLuint texture, const BoneSkeletonCubeMeshCpu &mesh)
{
  if (texture == 0 || mesh.interleavedPosUv.empty() || mesh.indices.empty() ||
      !creatureShader || !creatureShader->IsValid())
  {
    return;
  }

  const GLuint vao =
      CreatureMeshGpuCache::Instance().GetOrCreateSkeletalMeshVao(mesh);
  if (vao == 0)
  {
    return;
  }

  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);

  glBindTexture(GL_TEXTURE_2D, texture);
  creatureShader->Use();
  creatureShader->SetInt("texture0", 0);
  creatureShader->SetInt("uAnimFrame", 0);
  creatureShader->SetInt("uAnimFrameCount", 1);
  creatureShader->SetVec4(
      "uTint", glm::vec4(1.0f + ActiveHitFlash01, 1.0f + ActiveHitFlash01,
                         1.0f + ActiveHitFlash01, 1.0f));
  creatureShader->SetMat4("mvp_matrix", mvp);
  creatureShader->SetVec2("uCreatureLight", ActiveLight);

  glBindVertexArray(vao);
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()),
                 GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);

  creatureShader->Unuse();
  glBindTexture(GL_TEXTURE_2D, 0);
  ++Stats.CreatureDrawCalls;
}

void UCreatureDrawPass::DrawSkinnedMesh(
    const glm::mat4 &mvp, GLuint texture, const GltfPrimitiveCpu &mesh,
    const std::shared_ptr<const std::vector<glm::mat4>> &boneMatrices)
{
  if (texture == 0 || mesh.mesh.interleavedPosUv.empty() ||
      mesh.mesh.indices.empty() || !creatureSkinnedShader ||
      !creatureSkinnedShader->IsValid() || !boneMatrices ||
      boneMatrices->empty())
  {
    return;
  }

  size_t indexCount = 0;
  const GLuint vao =
      CreatureMeshGpuCache::Instance().GetOrCreateGltfSkinnedMeshVao(
          mesh, indexCount);
  if (vao == 0)
  {
    return;
  }

  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);

  glBindTexture(GL_TEXTURE_2D, texture);
  creatureSkinnedShader->Use();
  creatureSkinnedShader->SetInt("texture0", 0);
  creatureSkinnedShader->SetVec4(
      "uTint", glm::vec4(1.0f + ActiveHitFlash01, 1.0f + ActiveHitFlash01,
                         1.0f + ActiveHitFlash01, 1.0f));
  creatureSkinnedShader->SetMat4("mvp_matrix", mvp);
  creatureSkinnedShader->SetVec2("uCreatureLight", ActiveLight);
  BindCreatureBonePaletteBlock(creatureSkinnedShader->GetProgramID(),
                               kCreatureBonePaletteBindingPoint);
  // All primitives of one creature share the palette pointer; upload it once
  // per creature per frame instead of once per primitive (audit V7).
  if (boneMatrices != FrameBonePalette)
  {
    const size_t boneCount = UploadCreatureBonePaletteGpu(*boneMatrices);
    if (boneCount > 0)
    {
      ++Stats.CreatureBoneMatrixUploads;
    }
    FrameBonePalette = boneMatrices;
  }

  glBindVertexArray(vao);
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount),
                 GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);

  creatureSkinnedShader->Unuse();
  glBindTexture(GL_TEXTURE_2D, 0);
  ++Stats.CreatureDrawCalls;
}

void UCreatureDrawPass::Render(UWorld &world, UGeometryEngine &engine,
                               const RenderSettings &render)
{
  auto camera = world.GetCurrentUserCamera();
  if (!camera)
  {
    return;
  }
  const glm::mat4 viewProj = camera->GetProjection() * camera->GetViewMatrix();
  const float dt = static_cast<float>(camera->GetDeltaTime());
  const CreatureId controlledId = world.GetControlledCreatureId();
  Stats.Reset();
  Queue.Clear();
  FrameBonePalette.reset();

  const Frustum frustum = Frustum::FromViewProjection(viewProj);
  const glm::vec3 cameraPos = camera->GetPosition();

  // Narrow draw context for the visual backends: queue, textures, settings
  // and camera position (audit V13/V9). The engine pointer is the
  // transitional worn-equipment path.
  CreatureDrawContext drawContext;
  drawContext.Queue = &Queue;
  drawContext.Textures = engine.GetCreatureTextureStorage().get();
  drawContext.Settings = &engine.GetRenderSettings();
  drawContext.CameraPosition = cameraPos;
  drawContext.Engine = &engine;

  const float maxDistBlocks =
      static_cast<float>(world.GetEffectiveRenderDistance()) *
      static_cast<float>(CHUNK_SIZE);

  ++FrameCounter;
  LightingActive = true;
  ApplyCreatureLightUniforms(world, LightingActive);
  // Env overrides for QA runs (CUBA_CREATURE_LABELS=1, CUBA_CREATURE_LOCAL_LIGHT=0).
  static const bool envLabels = std::getenv("CUBA_CREATURE_LABELS") != nullptr;
  static const char *envLocal = std::getenv("CUBA_CREATURE_LOCAL_LIGHT");
  const bool localLight =
      envLocal ? std::atoi(envLocal) != 0 : render.CreatureLocalLight;
  const bool labels = envLabels || render.CreatureDebugLabels;
  PendingLabels.clear();
  static const bool lightBench =
      std::getenv("CUBA_CREATURE_LIGHT_BENCH") != nullptr;
  double lightSampleMs = 0.0;
  int lightSamples = 0;

  world.ForEachCreature(
      [&](UCreature &creature)
      {
        ++Stats.CreaturesConsidered;
        const bool controlled_first_person =
            creature.GetId() == controlledId &&
            camera->GetPerspective() == CameraPerspective::FirstPerson &&
            !camera->IsIsometricProjection();

        auto draw_debug_bounds = [&]()
        {
          DrawCreatureDebugBounds(creature, world, viewProj, engine,
                                  render.CreatureDebugBounds);
        };

        if (controlled_first_person)
        {
          draw_debug_bounds();
          ++Stats.CreaturesCulled;
          return;
        }

        const glm::vec3 sizeBlocks = creature.GetBounds().profile.maxSizeBlocks;
        if (!CreatureBoundsIntersectsFrustum(
                frustum, creature.GetBodyOrigin(), sizeBlocks, cameraPos,
                maxDistBlocks, render.FrustumCulling))
        {
          ++Stats.CreaturesCulled;
          return;
        }
        ++Stats.CreaturesDrawn;

        const std::string animType = world.ResolveAnimationTypeId(creature);
        const CreatureDefinition *def = world.GetCreatureDefinition(animType);
        CreatureDefinition fallback;
        if (!def)
        {
          fallback.Id = animType;
          def = &fallback;
        }
        glm::vec2 light(1.0f, 0.0f);
        {
          const auto lightBegin =
              lightBench ? std::chrono::steady_clock::now()
                         : std::chrono::steady_clock::time_point{};
          light = SampleCreatureLight(world, creature, dt, localLight);
          if (lightBench)
          {
            lightSampleMs += std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - lightBegin)
                                 .count();
            ++lightSamples;
          }
        }
        const size_t firstRequest = Queue.Size();
        SubmitCreatureVisual(world, creature, *def, dt, drawContext, viewProj);
        Queue.StampLight(firstRequest, light);

        if (labels && !creature.IsPlayerCharacter())
        {
          const glm::vec3 size = creature.GetBounds().currentSizeBlocks;
          const CreatureLocomotionFacts &facts = creature.GetLocomotionFacts();
          char text[160];
          std::snprintf(text, sizeof(text),
                        "%s #%llu %s v=%.1f box %.2fx%.2fx%.2f",
                        creature.GetTypeId().c_str(),
                        static_cast<unsigned long long>(creature.GetId()),
                        ToString(facts.state), facts.horizontalSpeed, size.x,
                        size.y, size.z);
          PendingLabel label;
          label.World = creature.GetBodyOrigin() +
                        glm::vec3(0.0f, size.y + 0.6f, 0.0f);
          label.Text = text;
          PendingLabels.push_back(std::move(label));
        }

        DrawCreatureHealthBar(creature, viewProj, engine);

        draw_debug_bounds();
      });
  struct LitSink final : ICreatureDrawSink
  {
    LitSink(UCreatureDrawPass &pass, UGeometryEngine &engine)
        : Pass(pass), Engine(engine)
    {
    }
    void DrawTexturedPart(const glm::mat4 &mvp, GLuint texture,
                          CreaturePartMesh mesh) override
    {
      Pass.DrawTexturedPart(mvp, texture, mesh);
    }
    void DrawBoneSkeletonMesh(const glm::mat4 &mvp, GLuint texture,
                              const BoneSkeletonCubeMeshCpu &mesh) override
    {
      Pass.DrawBoneSkeletonMesh(mvp, texture, mesh);
    }
    void DrawSkinnedMesh(const glm::mat4 &mvp, GLuint texture,
                         const GltfPrimitiveCpu &mesh,
                         const std::shared_ptr<const std::vector<glm::mat4>>
                             &boneMatrices) override
    {
      Pass.DrawSkinnedMesh(mvp, texture, mesh, boneMatrices);
    }
    void DrawBoxWireframe(const glm::mat4 &mvp,
                          const glm::vec4 &color) override
    {
      Engine.DrawBoxWireframe(mvp, color);
    }
    void DrawBillboard(const glm::mat4 &mvp, GLuint texture,
                       const glm::vec4 &tint) override
    {
      Pass.DrawBillboard(mvp, texture, tint);
    }
    void SetCreatureLight(const glm::vec2 &skyBlock) override
    {
      Pass.SetActiveLight(skyBlock);
    }
    UCreatureDrawPass &Pass;
    UGeometryEngine &Engine;
  } sink(*this, engine);
  Queue.Flush(sink);

  // Other users of the creature programs (GUI previews) draw unlit.
  LightingActive = false;
  ApplyCreatureLightUniforms(world, false);
  ActiveLight = glm::vec2(1.0f, 0.0f);

  if (lightBench && lightSamples > 0)
  {
    static int benchFrames = 0;
    static double benchTotalMs = 0.0;
    static int benchSamples = 0;
    benchTotalMs += lightSampleMs;
    benchSamples += lightSamples;
    if (++benchFrames % 300 == 0)
    {
      char line[200];
      std::snprintf(line, sizeof(line),
                    "local=%d avg %.4f ms/frame, %.2f us/creature "
                    "(%d creatures last frame)",
                    localLight ? 1 : 0, benchTotalMs / 300.0,
                    benchSamples > 0 ? benchTotalMs * 1000.0 / benchSamples
                                     : 0.0,
                    lightSamples);
      StallLogLine("CreatureLightBench", line, 0.0);
      benchTotalMs = 0.0;
      benchSamples = 0;
    }
  }

  if (labels)
  {
    DrawCreatureLabels(world, viewProj, engine);
  }
}

void UCreatureDrawPass::ApplyCreatureLightUniforms(const UWorld &world,
                                                   bool enabled)
{
  const UWorld::EnvironmentState &env = world.GetEnvironmentState();
  const UWorld::LightingSettings &lighting = world.GetLightingSettings();
  const float skyScale = SkyLightScaleFromWeather(env.WeatherSkyAttenuation,
                                                  env.PrecipitationIntensity);
  for (const std::shared_ptr<UShaderProgram> &shader :
       {creatureShader, creatureSkinnedShader})
  {
    if (!shader || !shader->IsValid())
    {
      continue;
    }
    shader->Use();
    shader->SetFloat("uCreatureLitEnabled", enabled ? 1.0f : 0.0f);
    shader->SetFloat("uEnvMinAmbient", lighting.MinAmbient);
    shader->SetFloat("uEnvDayFactor", env.DayNightFactor);
    shader->SetFloat("uEnvNightFactor", env.MoonNightFactor);
    shader->SetFloat("uEnvSkyLightScale", skyScale);
    shader->Unuse();
  }
}

glm::vec2 UCreatureDrawPass::SampleCreatureLight(UWorld &world,
                                                 const UCreature &creature,
                                                 float dt, bool localLight)
{
  if (!localLight)
  {
    return glm::vec2(1.0f, 0.0f);
  }
  LightState &state = LightStates[creature.GetId()];
  state.LastSeenFrame = FrameCounter;
  const glm::vec3 size = creature.GetBounds().currentSizeBlocks;
  const glm::vec3 center =
      creature.GetBodyOrigin() + glm::vec3(0.0f, size.y * 0.5f, 0.0f);
  const glm::ivec3 cell(static_cast<int>(std::floor(center.x)),
                        static_cast<int>(std::floor(center.y)),
                        static_cast<int>(std::floor(center.z)));
  state.SecondsToResample -= dt;
  if (cell != state.Cell || state.SecondsToResample <= 0.0f)
  {
    state.Cell = cell;
    state.SecondsToResample = 0.25f;
    const glm::ivec3 chunkCoord = UChunkManager::WorldToChunk(cell);
    if (const UChunk *chunk =
            world.GetBlockWorld().GetChunkManager().GetChunk(chunkCoord))
    {
      const glm::ivec3 local = cell - chunkCoord * CHUNK_SIZE;
      const uint8_t packed = chunk->GetLightPackedLocal(local);
      state.Target = glm::vec2(
          static_cast<float>(UnpackSky(packed)) / kMaxLightLevel,
          static_cast<float>(UnpackBlock(packed)) / kMaxLightLevel);
    }
    // Not loaded or not lit yet: keep the previous target.
  }
  // Ease toward the target (~0.15 s) so crossing cell borders never pops.
  const float blend = std::clamp(dt * 7.0f, 0.0f, 1.0f);
  state.Current += (state.Target - state.Current) * blend;

  // Drop state of creatures not drawn for a while (cheap, amortized).
  if ((FrameCounter & 255u) == 0)
  {
    for (auto it = LightStates.begin(); it != LightStates.end();)
    {
      it = (FrameCounter - it->second.LastSeenFrame > 600)
               ? LightStates.erase(it)
               : std::next(it);
    }
  }
  return state.Current;
}

void UCreatureDrawPass::DrawCreatureLabels(UWorld &world,
                                           const glm::mat4 &viewProj,
                                           UGeometryEngine &engine)
{
  (void)world;
  UTextRenderer *text = engine.GetTextRenderer();
  if (!text || PendingLabels.empty())
  {
    return;
  }
  GLint viewport[4] = {0, 0, 1, 1};
  glGetIntegerv(GL_VIEWPORT, viewport);
  const GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST);
  glDisable(GL_DEPTH_TEST);
  for (const PendingLabel &label : PendingLabels)
  {
    const glm::vec4 clip = viewProj * glm::vec4(label.World, 1.0f);
    if (clip.w <= 0.05f)
    {
      continue;
    }
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (std::abs(ndc.x) > 1.0f || std::abs(ndc.y) > 1.0f)
    {
      continue;
    }
    const float x = (ndc.x * 0.5f + 0.5f) * static_cast<float>(viewport[2]);
    const float y = (ndc.y * 0.5f + 0.5f) * static_cast<float>(viewport[3]);
    const float scale = 0.5f;
    const glm::vec2 extent = text->GetTextSize(label.Text, scale);
    text->RenderText(label.Text, x - extent.x * 0.5f, y, scale,
                     glm::vec3(1.0f, 0.95f, 0.45f));
  }
  if (depthWas)
  {
    glEnable(GL_DEPTH_TEST);
  }
}

void UCreatureDrawPass::SubmitCreatureVisual(
    UWorld &world, UCreature &creature, const CreatureDefinition &def,
    float dt, const CreatureDrawContext &ctx, const glm::mat4 &viewProj)
{
  IUCreatureVisual *visual = creature.GetVisual();
  if (!visual)
  {
    return;
  }
  visual->SetAppearance(world.GetResolvedAppearance(creature));
  const CreatureLocomotionFacts &facts = creature.GetLocomotionFacts();
  CreaturePoseParams pose;
  // Procedural pose parameters are only consumed by the rigid backend;
  // skeleton/gltf backends derive pose from locomotion facts themselves.
  const LocomotionArchetype poseArchetype =
      ResolveCreaturePoseArchetype(def.visual.rig.templateId,
                                   facts.archetype);
  if (ParseCreatureVisualBackend(def.visual.backend) ==
      CreatureVisualBackend::RigidVoxels)
  {
    if (IUCreaturePosePresenter *presenter =
            world.GetPosePresenterRegistry().Get(poseArchetype))
    {
      pose = presenter->Compute(facts, def, dt);
    }
  }
  visual->UpdatePose(creature, facts, pose, def, dt);
  ActiveHitFlash01 = creature.GetHitFlash01();
  visual->SubmitDraw(ctx, viewProj);
  ActiveHitFlash01 = 0.f;
}

void UCreatureDrawPass::DrawCreatureHealthBar(const UCreature &creature,
                                              const glm::mat4 &viewProj,
                                              UGeometryEngine &engine)
{
  // World-space mob health bar (fade ~2s after damage).
  if (creature.IsPlayerCharacter() || creature.GetHealthBarVisibleSec() <= 0.f)
  {
    return;
  }
  const CreatureVitals &vitals = creature.GetVitals();
  const float ratio = vitals.maxHealth > 0.f
                          ? std::clamp(vitals.health / vitals.maxHealth, 0.f, 1.f)
                          : 0.f;
  const glm::vec3 bodyOrigin = creature.GetBodyOrigin();
  const glm::vec3 sizeBlocks = creature.GetBounds().currentSizeBlocks;
  const float topY =
      BoundsFeetY(bodyOrigin) + std::max(0.6f, sizeBlocks.y) + 0.25f;
  const glm::vec3 barCenter(bodyOrigin.x, topY, bodyOrigin.z);
  const float barW = std::max(0.6f, sizeBlocks.x * 0.9f);
  glm::mat4 bg = glm::translate(glm::mat4(1.0f), barCenter);
  bg = glm::scale(bg, glm::vec3(barW, 0.08f, 0.08f));
  engine.DrawBoxWireframe(viewProj * bg, glm::vec4(0.15f, 0.15f, 0.15f, 0.9f));
  if (ratio > 0.01f)
  {
    const float fillW = barW * ratio;
    const glm::vec3 fillCenter =
        barCenter + glm::vec3(-(barW - fillW) * 0.5f, 0.f, 0.f);
    glm::mat4 fill = glm::translate(glm::mat4(1.0f), fillCenter);
    fill = glm::scale(fill, glm::vec3(fillW, 0.06f, 0.06f));
    glm::vec4 color{0.2f, 0.85f, 0.25f, 1.f};
    if (ratio < 0.35f)
    {
      color = glm::vec4(0.9f, 0.2f, 0.15f, 1.f);
    }
    else if (ratio < 0.65f)
    {
      color = glm::vec4(0.9f, 0.85f, 0.15f, 1.f);
    }
    engine.DrawBoxWireframe(viewProj * fill, color);
  }
}

void UCreatureDrawPass::DrawCreatureDebugBounds(const UCreature &creature,
                                                const UWorld &world,
                                                const glm::mat4 &viewProj,
                                                UGeometryEngine &engine,
                                                bool enabled)
{
  if (!enabled)
  {
    return;
  }
  const glm::vec3 bodyOrigin = creature.GetBodyOrigin();
  const glm::vec3 sizeBlocks = creature.GetBounds().currentSizeBlocks;
  const glm::vec3 center = BoundsCollisionCenter(bodyOrigin, sizeBlocks);
  glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
  model = glm::scale(model, sizeBlocks);
  engine.DrawBoxWireframe(viewProj * model,
                          glm::vec4(0.2f, 0.85f, 1.0f, 1.0f));

  // Ground-contact marker under the feet (green when grounded).
  const int gx = static_cast<int>(std::floor(bodyOrigin.x));
  const int gz = static_cast<int>(std::floor(bodyOrigin.z));
  const float feetY = BoundsFeetY(bodyOrigin);
  float groundY = feetY;
  float delta = 0.0f;
  if (const std::optional<float> queryY =
          world.QueryGroundFeetYUnder(gx, gz, feetY))
  {
    groundY = *queryY;
    delta = feetY - groundY;
  }
  const glm::vec3 groundCenter(static_cast<float>(gx) + 0.5f, groundY,
                               static_cast<float>(gz) + 0.5f);
  glm::mat4 groundModel = glm::translate(glm::mat4(1.0f), groundCenter);
  groundModel = glm::scale(groundModel, glm::vec3(1.02f, 0.02f, 1.02f));
  const float groundColor = std::abs(delta) < 0.05f ? 0.2f : 1.0f;
  engine.DrawBoxWireframe(
      viewProj * groundModel,
      glm::vec4(groundColor, 1.0f - groundColor * 0.5f, 0.15f, 1.0f));
}

} // namespace cutum

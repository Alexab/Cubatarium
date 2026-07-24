#include "Render/Engine/CreatureDrawPass.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include "Creatures/Visual/CreatureBonePaletteGpu.h"
#include "Creatures/Visual/CreatureMeshGpuCache.h"
#include "Creatures/Visual/CreatureVisibility.h"
#include "Creatures/Visual/CreatureVisual.h"
#include "Pose/CreaturePoseParams.h"
#include "Pose/IUCreaturePosePresenter.h"
#include "Render/Camera/Camera.h"
#include "Render/Camera/CameraPerspective.h"
#include "Render/Camera/Frustum.h"
#include "Render/Engine/GeometryEngine.h"
#include "Render/GlIncludes.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/World.h"
#include <chrono>
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
  creatureShader->SetVec4("uTint", glm::vec4(1.0f));
  creatureShader->SetMat4("mvp_matrix", mvp);

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
  glBindVertexArray(creatureBillboardVAO);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);
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
  creatureShader->SetVec4("uTint", glm::vec4(1.0f));
  creatureShader->SetMat4("mvp_matrix", mvp);

  glBindVertexArray(vao);
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()),
                 GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);

  creatureShader->Unuse();
  glBindTexture(GL_TEXTURE_2D, 0);
  ++Stats.CreatureDrawCalls;
}

void UCreatureDrawPass::DrawGltfMesh(const glm::mat4 &mvp, GLuint texture,
                                     const BoneSkeletonCubeMeshCpu &mesh)
{
  DrawBoneSkeletonMesh(mvp, texture, mesh);
}

void UCreatureDrawPass::DrawSkinnedMesh(
    const glm::mat4 &mvp, GLuint texture, const GltfPrimitiveCpu &mesh,
    const std::vector<glm::mat4> &boneMatrices)
{
  if (texture == 0 || mesh.mesh.interleavedPosUv.empty() ||
      mesh.mesh.indices.empty() || !creatureSkinnedShader ||
      !creatureSkinnedShader->IsValid())
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
  creatureSkinnedShader->SetMat4("mvp_matrix", mvp);
  BindCreatureBonePaletteBlock(creatureSkinnedShader->GetProgramID(),
                               kCreatureBonePaletteBindingPoint);
  const size_t boneCount = UploadCreatureBonePaletteGpu(boneMatrices);
  if (boneCount > 0)
  {
    ++Stats.CreatureBoneMatrixUploads;
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

  const Frustum frustum = Frustum::FromViewProjection(viewProj);
  const glm::vec3 cameraPos = camera->GetPosition();
  const float maxDistBlocks =
      static_cast<float>(world.GetEffectiveRenderDistance()) *
      static_cast<float>(CHUNK_SIZE);

  world.ForEachCreature(
      [&](UCreature &creature)
      {
        ++Stats.CreaturesConsidered;
        const bool controlled_first_person =
            creature.GetId() == controlledId &&
            camera->GetPerspective() == CameraPerspective::FirstPerson;

        auto draw_debug_bounds = [&]()
        {
          if (!render.CreatureDebugBounds)
          {
            return;
          }
          const glm::vec3 bodyOrigin = creature.GetBodyOrigin();
          const glm::vec3 sizeBlocks = creature.GetBounds().currentSizeBlocks;
          const glm::vec3 center =
              BoundsCollisionCenter(bodyOrigin, sizeBlocks);
          glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
          model = glm::scale(model, sizeBlocks);
          engine.DrawBoxWireframe(viewProj * model,
                                  glm::vec4(0.2f, 0.85f, 1.0f, 1.0f));

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
        if (IUCreatureVisual *visual = creature.GetVisual())
        {
          visual->SetAppearance(world.GetResolvedAppearance(creature));
          const CreatureLocomotionFacts &facts = creature.GetLocomotionFacts();
          CreaturePoseParams pose;
          const LocomotionArchetype poseArchetype =
              ResolveCreaturePoseArchetype(def->visual.rig.templateId,
                                           facts.archetype);
          if (ParseCreatureVisualBackend(def->visual.backend) ==
              CreatureVisualBackend::RigidVoxels)
          {
            if (IUCreaturePosePresenter *presenter =
                    world.GetPosePresenterRegistry().Get(poseArchetype))
            {
              pose = presenter->Compute(facts, *def, dt);
            }
          }
          visual->UpdatePose(creature, facts, pose, *def, dt);
          visual->SubmitDraw(engine, viewProj);
        }

        draw_debug_bounds();
      });
  Queue.Flush(engine);
}

} // namespace cutum

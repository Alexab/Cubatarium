#include "Render/Blocks/BlockBreakFxPass.h"
#include "Render/Engine/TerrainGpuBuffers.h"
#include "Render/Pipeline/GlVertexBufferScope.h"

#include "Render/Engine/ShaderManager.h"
#include "Render/GlIncludes.h"
#include "Render/Pipeline/GlStateScope.h"
#include "World/Core/World.h"
#include "World/Math/GridMath.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <optional>
#include <vector>

namespace cutum
{

namespace
{

constexpr GlStateMask kGlMaskBreakDebris =
    GlStateBit::DepthTest | GlStateBit::DepthMask | GlStateBit::Blend |
    GlStateBit::CullFace;

constexpr float kHitProgressStep = 0.1f;

struct DebrisInstanceGpu
{
  glm::vec3 WorldPos;
  float Size;
  glm::vec4 Color;
};

} // namespace

bool UBlockBreakFxPass::InitShaders(
    const std::shared_ptr<UShaderManager> &shader_manager)
{
  if (!shader_manager)
  {
    return false;
  }
  Shader = shader_manager->CreateShader("block_debris",
                                        "shaders/vshader_block_debris.glsl",
                                        "shaders/fshader_block_debris.glsl");
  if (!Shader || !Shader->IsValid())
  {
    std::cerr << "Failed to create block debris shader" << std::endl;
    return false;
  }
  if (!SinkRegistered)
  {
    InfluenceEvents::AddSink(this);
    SinkRegistered = true;
  }
  return true;
}

void UBlockBreakFxPass::DestroyGpuResources()
{
  if (SinkRegistered)
  {
    InfluenceEvents::RemoveSink(this);
    SinkRegistered = false;
  }
  Particles.Reset();
  DestroyBuffers();
  HadSession = false;
  LastProgress = 0.0f;
  HitProgressCursor = 0.0f;
}

void UBlockBreakFxPass::DestroyBuffers()
{
  RetireVertexBuffers(Vao, {&CornerVbo, &InstanceVbo, &Ebo});
  InstanceCapacity = 0;
  IndexCount = 0;
}

void UBlockBreakFxPass::InspectGpuBuffers(TerrainGpuBufferCensus &out) const
{
  out.Add(TerrainGpuBufferRole::BlockDebris, CornerVbo);
  out.Add(TerrainGpuBufferRole::BlockDebris, InstanceVbo);
  out.Add(TerrainGpuBufferRole::BlockDebris, Ebo);
}

void UBlockBreakFxPass::ApplyDigProgress(glm::ivec3 block_pos, float progress)
{
  if (!HadSession || block_pos != LastBlockPos)
  {
    LastBlockPos = block_pos;
    HitProgressCursor = 0.0f;
    LastProgress = 0.0f;
  }
  const glm::vec3 center = BlockCenter(block_pos);
  while (progress >= HitProgressCursor + kHitProgressStep)
  {
    HitProgressCursor += kHitProgressStep;
    Particles.SpawnHitDebris(center,
                             UBlockBreakParticleSystem::kHitDebrisPerStep);
  }
  LastProgress = progress;
  HadSession = true;
}

void UBlockBreakFxPass::NotifyDigEnded(bool completed)
{
  if (HadSession && completed && LastProgress >= 0.85f)
  {
    Particles.SpawnBreakBurst(BlockCenter(LastBlockPos),
                              UBlockBreakParticleSystem::kBurstDebris);
  }
  HadSession = false;
  LastProgress = 0.0f;
  HitProgressCursor = 0.0f;
}

void UBlockBreakFxPass::OnInfluenceEvent(const InfluenceEvent &event)
{
  if (event.Channel != InfluenceChannel::Dig)
  {
    return;
  }
  if (event.Kind == InfluenceEventKind::DigProgress)
  {
    ApplyDigProgress(event.DigBlockPos, event.DigProgress);
    return;
  }
  if (event.Kind == InfluenceEventKind::Applied)
  {
    ApplyDigProgress(event.DigBlockPos, 1.f);
    NotifyDigEnded(true);
    return;
  }
  if (event.Kind == InfluenceEventKind::Cancelled)
  {
    NotifyDigEnded(false);
  }
}

void UBlockBreakFxPass::SyncFromWorld(UWorld &world)
{
  // DigSessionState remains SoT for crack overlay; debris also gets Dig
  // Influence DigProgress events. Poll keeps FX correct if events are missed.
  const bool has_session = world.HasBreakSession();
  if (has_session)
  {
    if (const std::optional<glm::ivec3> pos = world.GetBreakSessionBlockPos())
    {
      ApplyDigProgress(*pos, world.GetBreakProgress());
    }
    return;
  }
  if (HadSession)
  {
    NotifyDigEnded(LastProgress >= 0.85f);
  }
}

bool UBlockBreakFxPass::EnsureGpu()
{
  if (Vao != 0)
  {
    return true;
  }

  const float corners[] = {-1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f};
  const unsigned int indices[] = {0, 1, 2, 0, 2, 3};
  const GlVertexBufferScope restore;

  glGenVertexArrays(1, &Vao);
  glGenBuffers(1, &CornerVbo);
  glGenBuffers(1, &InstanceVbo);
  glGenBuffers(1, &Ebo);
  if (Vao == 0 || CornerVbo == 0 || InstanceVbo == 0 || Ebo == 0)
  {
    DestroyBuffers();
    return false;
  }

  glBindVertexArray(Vao);
  glBindBuffer(GL_ARRAY_BUFFER, CornerVbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
  glEnableVertexAttribArray(0);
  glVertexAttribDivisor(0, 0);

  glBindBuffer(GL_ARRAY_BUFFER, InstanceVbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(DebrisInstanceGpu) * 64, nullptr,
               GL_DYNAMIC_DRAW);
  const GLsizei stride = static_cast<GLsizei>(sizeof(DebrisInstanceGpu));
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<void *>(offsetof(DebrisInstanceGpu, WorldPos)));
  glEnableVertexAttribArray(1);
  glVertexAttribDivisor(1, 1);
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<void *>(offsetof(DebrisInstanceGpu, Size)));
  glEnableVertexAttribArray(2);
  glVertexAttribDivisor(2, 1);
  glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<void *>(offsetof(DebrisInstanceGpu, Color)));
  glEnableVertexAttribArray(3);
  glVertexAttribDivisor(3, 1);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, Ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
  if (glGetError() != GL_NO_ERROR)
  {
    DestroyBuffers();
    return false;
  }
  IndexCount = 6;
  InstanceCapacity = 64;
  return true;
}

void UBlockBreakFxPass::DrawInstances(const glm::mat4 &view_proj,
                                      const glm::vec3 &camera_right,
                                      const glm::vec3 &camera_up)
{
  const auto &instances = Particles.GetInstances();
  if (instances.empty() || !Shader || !Shader->IsValid() || !EnsureGpu())
  {
    return;
  }

  std::vector<DebrisInstanceGpu> upload;
  upload.reserve(instances.size());
  for (const BlockBreakParticleGpuInstance &src : instances)
  {
    DebrisInstanceGpu dst;
    dst.WorldPos = src.WorldPos;
    dst.Size = src.Size;
    dst.Color = src.Color;
    upload.push_back(dst);
  }

  const GlVertexBufferScope restore;
  glBindBuffer(GL_ARRAY_BUFFER, InstanceVbo);
  if (upload.size() > InstanceCapacity)
  {
    const auto capacity = upload.size() + upload.size() / 2 + 16;
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(capacity *
                                         sizeof(DebrisInstanceGpu)),
                 nullptr, GL_DYNAMIC_DRAW);
    if (glGetError() != GL_NO_ERROR)
      return;
    InstanceCapacity = capacity;
  }
  glBufferSubData(GL_ARRAY_BUFFER, 0,
                  static_cast<GLsizeiptr>(upload.size() *
                                         sizeof(DebrisInstanceGpu)),
                  upload.data());
  if (glGetError() != GL_NO_ERROR)
    return;

  UGlStateScope gl_guard(kGlMaskBreakDebris);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);

  Shader->Use();
  Shader->SetMat4("uViewProj", view_proj);
  Shader->SetVec3("uCameraRight", camera_right);
  Shader->SetVec3("uCameraUp", camera_up);
  glBindVertexArray(Vao);
  glDrawElementsInstanced(GL_TRIANGLES, IndexCount, GL_UNSIGNED_INT, nullptr,
                          static_cast<GLsizei>(upload.size()));
  Shader->Unuse();
}

void UBlockBreakFxPass::UpdateAndRender(UWorld &world, float dt_seconds,
                                        const glm::mat4 &view_proj,
                                        const glm::vec3 &camera_right,
                                        const glm::vec3 &camera_up)
{
  SyncFromWorld(world);
  Particles.Update(dt_seconds);
  DrawInstances(view_proj, camera_right, camera_up);
}

} // namespace cutum

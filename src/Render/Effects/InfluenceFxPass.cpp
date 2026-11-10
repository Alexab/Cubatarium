#include "Render/Effects/InfluenceFxPass.h"
#include "Render/Effects/InfluenceFxSystem.h"
#include "Render/GlIncludes.h"
#include "Render/Engine/ShaderManager.h"
#include <algorithm>
#include <iostream>
#include <vector>

namespace cutum
{

bool UInfluenceFxPass::InitShaders(
    const std::shared_ptr<UShaderManager> &shader_manager)
{
  if (!shader_manager)
  {
    return false;
  }
  LineShader = shader_manager->CreateShader(
      "influence_fx_line", "shaders/vshader.glsl", "shaders/fshader_2d.glsl");
  if (!LineShader || !LineShader->IsValid())
  {
    std::cerr << "Failed to create influence_fx_line shader" << std::endl;
    return false;
  }
  return EnsureLineBuffers();
}

void UInfluenceFxPass::DestroyGpuResources()
{
  if (LineVbo != 0)
  {
    glDeleteBuffers(1, &LineVbo);
    LineVbo = 0;
  }
  if (LineVao != 0)
  {
    glDeleteVertexArrays(1, &LineVao);
    LineVao = 0;
  }
  LineShader.reset();
}

bool UInfluenceFxPass::EnsureLineBuffers()
{
  if (LineVao != 0)
  {
    return true;
  }
  glGenVertexArrays(1, &LineVao);
  glGenBuffers(1, &LineVbo);
  glBindVertexArray(LineVao);
  glBindBuffer(GL_ARRAY_BUFFER, LineVbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 64, nullptr, GL_DYNAMIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                        reinterpret_cast<void *>(0));
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);
  return LineVao != 0;
}

void UInfluenceFxPass::UpdateAndRender(const glm::mat4 &view_proj, float dt)
{
  UInfluenceFxSystem &fx = UInfluenceFxSystem::Get();
  fx.Update(dt);
  const auto &beams = fx.GetBeams();
  const auto &bursts = fx.GetBursts();
  if ((beams.empty() && bursts.empty()) || !LineShader ||
      !LineShader->IsValid() || !EnsureLineBuffers())
  {
    return;
  }

  std::vector<float> verts;
  verts.reserve((beams.size() + bursts.size() * 4) * 6);
  auto push_line = [&](const glm::vec3 &a, const glm::vec3 &b)
  {
    verts.push_back(a.x);
    verts.push_back(a.y);
    verts.push_back(a.z);
    verts.push_back(b.x);
    verts.push_back(b.y);
    verts.push_back(b.z);
  };

  for (const InfluenceFxBeam &b : beams)
  {
    push_line(b.From, b.To);
  }
  for (const InfluenceFxBurst &p : bursts)
  {
    const float s = 0.15f + 0.25f * (1.f - p.Life / p.LifeMax);
    push_line(p.Pos + glm::vec3(-s, 0.f, 0.f), p.Pos + glm::vec3(s, 0.f, 0.f));
    push_line(p.Pos + glm::vec3(0.f, -s, 0.f), p.Pos + glm::vec3(0.f, s, 0.f));
  }
  if (verts.empty())
  {
    return;
  }

  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glLineWidth(2.5f);

  glBindVertexArray(LineVao);
  glBindBuffer(GL_ARRAY_BUFFER, LineVbo);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
               verts.data(), GL_DYNAMIC_DRAW);

  LineShader->Use();
  LineShader->SetMat4("mvp_matrix", view_proj);
  // fshader.glsl may use `color` uniform like outline path.
  LineShader->SetVec4("color", glm::vec4(1.f, 0.75f, 0.3f, 0.85f));

  glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(verts.size() / 3));
  LineShader->Unuse();
  glBindVertexArray(0);
  glDisable(GL_BLEND);
}

} // namespace cutum

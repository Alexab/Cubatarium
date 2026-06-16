#include "Gui/Batch/UiQuadBatch.h"
#include "Render/Engine/ShaderManager.h"

#include "Render/GlIncludes.h"
#include <algorithm>
#include <iostream>

namespace cutum
{

UGuiQuadBatch::UGuiQuadBatch() = default;

UGuiQuadBatch::~UGuiQuadBatch() { Shutdown(); }

bool UGuiQuadBatch::Initialize(std::shared_ptr<UShaderProgram> shader)
{
  if (!shader || !shader->IsValid())
  {
    return false;
  }
  Shader = std::move(shader);

  glGenVertexArrays(1, &Vao);
  glGenBuffers(1, &Vbo);
  glBindVertexArray(Vao);
  glBindBuffer(GL_ARRAY_BUFFER, Vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * 6 * 256, nullptr,
               GL_DYNAMIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);

  Initialized = true;
  return true;
}

void UGuiQuadBatch::Shutdown()
{
  if (Vbo != 0)
  {
    glDeleteBuffers(1, &Vbo);
    Vbo = 0;
  }
  if (Vao != 0)
  {
    glDeleteVertexArrays(1, &Vao);
    Vao = 0;
  }
  Shader.reset();
  Initialized = false;
}

void UGuiQuadBatch::Begin(int window_width, int window_height)
{
  WindowWidth = window_width;
  WindowHeight = window_height;
  Vertices.clear();

  GLboolean depthTest;
  glGetBooleanv(GL_DEPTH_TEST, &depthTest);
  DepthTestWasEnabled = depthTest == GL_TRUE;
  glDisable(GL_DEPTH_TEST);

  GLboolean blend;
  glGetBooleanv(GL_BLEND, &blend);
  BlendWasEnabled = blend == GL_TRUE;
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void UGuiQuadBatch::End()
{
  Flush();
  if (DepthTestWasEnabled)
  {
    glEnable(GL_DEPTH_TEST);
  }
  else
  {
    glDisable(GL_DEPTH_TEST);
  }
  if (!BlendWasEnabled)
  {
    glDisable(GL_BLEND);
  }
}

void UGuiQuadBatch::GuiRectToShaderCoords(const GuiRect &rect, float &x0,
                                          float &y0, float &x1, float &y1) const
{
  x0 = static_cast<float>(rect.X);
  x1 = static_cast<float>(rect.X + rect.W);
  const float h = static_cast<float>(WindowHeight);
  y0 = h - static_cast<float>(rect.Y + rect.H);
  y1 = h - static_cast<float>(rect.Y);
}

void UGuiQuadBatch::AddQuad(float x0, float y0, float x1, float y1,
                            const glm::vec4 &color)
{
  if (color != CurrentColor && !Vertices.empty())
  {
    Flush();
  }
  CurrentColor = color;
  Vertices.push_back({x0, y0});
  Vertices.push_back({x1, y0});
  Vertices.push_back({x0, y1});
  Vertices.push_back({x0, y1});
  Vertices.push_back({x1, y0});
  Vertices.push_back({x1, y1});
}

void UGuiQuadBatch::Flush()
{
  if (!Initialized || Vertices.empty() || !Shader)
  {
    Vertices.clear();
    return;
  }

  Shader->Use();
  Shader->SetVec2("screenSize", glm::vec2(WindowWidth, WindowHeight));
  Shader->SetVec4("color", CurrentColor);

  glBindVertexArray(Vao);
  glBindBuffer(GL_ARRAY_BUFFER, Vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0,
                  static_cast<GLsizeiptr>(Vertices.size() * sizeof(Vertex)),
                  Vertices.data());
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(Vertices.size()));
  glBindVertexArray(0);
  Shader->Unuse();
  Vertices.clear();
}

void UGuiQuadBatch::DrawFilledRect(const GuiRect &rect, const glm::vec4 &color)
{
  if (rect.W <= 0 || rect.H <= 0)
  {
    return;
  }
  float x0 = 0.0f;
  float y0 = 0.0f;
  float x1 = 0.0f;
  float y1 = 0.0f;
  GuiRectToShaderCoords(rect, x0, y0, x1, y1);
  AddQuad(x0, y0, x1, y1, color);
}

void UGuiQuadBatch::DrawBorderRect(const GuiRect &rect, const glm::vec4 &color,
                                   int thicknessPx)
{
  const int t = std::max(1, thicknessPx);
  DrawFilledRect({rect.X, rect.Y, rect.W, t}, color);
  DrawFilledRect({rect.X, rect.Y + rect.H - t, rect.W, t}, color);
  DrawFilledRect({rect.X, rect.Y, t, rect.H}, color);
  DrawFilledRect({rect.X + rect.W - t, rect.Y, t, rect.H}, color);
}

} // namespace cutum

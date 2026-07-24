#include "Gui/Batch/UiTexturedQuadBatch.h"
#include "Render/Engine/ShaderManager.h"

#include "Render/GlIncludes.h"

namespace cutum
{

UGuiTexturedQuadBatch::UGuiTexturedQuadBatch() = default;

UGuiTexturedQuadBatch::~UGuiTexturedQuadBatch() { Shutdown(); }

bool UGuiTexturedQuadBatch::Initialize(std::shared_ptr<UShaderProgram> shader)
{
  if (!shader || !shader->IsValid())
  {
    return false;
  }
  Shader = std::move(shader);

  struct Vertex
  {
    float x;
    float y;
    float u;
    float v;
  };

  glGenVertexArrays(1, &Vao);
  glGenBuffers(1, &Vbo);
  glBindVertexArray(Vao);
  glBindBuffer(GL_ARRAY_BUFFER, Vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * 6, nullptr, GL_DYNAMIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void *>(sizeof(float) * 2));
  glEnableVertexAttribArray(1);
  glBindVertexArray(0);

  Initialized = true;
  return true;
}

void UGuiTexturedQuadBatch::Shutdown()
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

void UGuiTexturedQuadBatch::Begin(int window_width, int window_height)
{
  WindowWidth = window_width;
  WindowHeight = window_height;
  BoundTexture = 0;

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

void UGuiTexturedQuadBatch::GuiRectToShaderCoords(const GuiRect &rect,
                                                  float &x0, float &y0,
                                                  float &x1, float &y1) const
{
  x0 = static_cast<float>(rect.X);
  x1 = static_cast<float>(rect.X + rect.W);
  const float top = static_cast<float>(rect.Y);
  const float bottom = static_cast<float>(rect.Y + rect.H);
  y0 = static_cast<float>(WindowHeight) - bottom;
  y1 = static_cast<float>(WindowHeight) - top;
}

void UGuiTexturedQuadBatch::DrawTexturedRect(const GuiRect &rect,
                                             GLuint texture,
                                             const glm::vec4 &tint)
{
  if (!Initialized || !Shader || texture == 0)
  {
    return;
  }

  float x0 = 0.0f;
  float y0 = 0.0f;
  float x1 = 0.0f;
  float y1 = 0.0f;
  GuiRectToShaderCoords(rect, x0, y0, x1, y1);

  const float vertices[] = {
      x0, y0, 0.0f, 0.0f, x1, y0, 1.0f, 0.0f, x0, y1, 0.0f, 1.0f,
      x0, y1, 0.0f, 1.0f, x1, y0, 1.0f, 0.0f, x1, y1, 1.0f, 1.0f,
  };

  Shader->Use();
  Shader->SetVec2("screenSize", glm::vec2(static_cast<float>(WindowWidth),
                                          static_cast<float>(WindowHeight)));
  Shader->SetVec4("tint", tint);
  Shader->SetInt("texture0", 0);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);

  glBindVertexArray(Vao);
  glBindBuffer(GL_ARRAY_BUFFER, Vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBindVertexArray(0);
  Shader->Unuse();
}

void UGuiTexturedQuadBatch::End()
{
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

} // namespace cutum

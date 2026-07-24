#include "Gui/Core/GuiRenderer.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/Engine/TextRenderer.h"

#include "Render/GlIncludes.h"
#include <algorithm>

namespace cutum
{

UGuiRenderer::UGuiRenderer() = default;

UGuiRenderer::~UGuiRenderer() { Shutdown(); }

bool UGuiRenderer::Initialize(std::shared_ptr<UShaderManager> shaderManager,
                              std::shared_ptr<UTextRenderer> textRenderer)
{
  TextRenderer = std::move(textRenderer);
  if (!shaderManager)
  {
    return false;
  }
  auto uiShader = shaderManager->CreateShader(
      "gui_ui", "shaders/vshader_2d.glsl", "shaders/fshader_2d.glsl");
  if (!uiShader || !uiShader->IsValid())
  {
    return false;
  }
  if (!QuadBatch.Initialize(uiShader))
  {
    return false;
  }
  auto texShader =
      shaderManager->CreateShader("gui_textured", "shaders/gui_textured_v.glsl",
                                  "shaders/gui_textured_f.glsl");
  if (!texShader || !texShader->IsValid())
  {
    return false;
  }
  return TexturedQuadBatch.Initialize(texShader);
}

void UGuiRenderer::Shutdown()
{
  TexturedQuadBatch.Shutdown();
  QuadBatch.Shutdown();
  TextRenderer.reset();
  ClipStack.clear();
}

void UGuiRenderer::BeginFrame(int window_width, int window_height)
{
  WindowWidth = window_width;
  WindowHeight = window_height;
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  if (TextRenderer)
  {
    TextRenderer->SetWindowSize(WindowWidth, WindowHeight);
  }
  QuadBatch.Begin(window_width, window_height);
  TexturedQuadBatch.Begin(window_width, window_height);
  ClipStack.clear();
}

void UGuiRenderer::EndFrame()
{
  QuadBatch.End();
  TexturedQuadBatch.End();
  glDisable(GL_SCISSOR_TEST);
}

void UGuiRenderer::ApplyClipStack()
{
  if (ClipStack.empty())
  {
    glDisable(GL_SCISSOR_TEST);
    return;
  }
  GuiRect clip = ClipStack.back();
  for (int i = static_cast<int>(ClipStack.size()) - 2; i >= 0; --i)
  {
    const GuiRect &r = ClipStack[static_cast<size_t>(i)];
    const int x1 = std::max(clip.X, r.X);
    const int y1 = std::max(clip.Y, r.Y);
    const int x2 = std::min(clip.X + clip.W, r.X + r.W);
    const int y2 = std::min(clip.Y + clip.H, r.Y + r.H);
    clip.X = x1;
    clip.Y = y1;
    clip.W = std::max(0, x2 - x1);
    clip.H = std::max(0, y2 - y1);
  }
  glEnable(GL_SCISSOR_TEST);
  const int glY = WindowHeight - (clip.Y + clip.H);
  glScissor(clip.X, glY, clip.W, clip.H);
}

void UGuiRenderer::PushClipRect(const GuiRect &rect)
{
  ClipStack.push_back(rect);
  ApplyClipStack();
}

void UGuiRenderer::PopClipRect()
{
  if (!ClipStack.empty())
  {
    ClipStack.pop_back();
  }
  ApplyClipStack();
}

void UGuiRenderer::DrawFilledRect(const GuiRect &rect, const glm::vec4 &color)
{
  QuadBatch.DrawFilledRect(rect, color);
}

void UGuiRenderer::DrawBorderRect(const GuiRect &rect, const glm::vec4 &color,
                                  int thicknessPx)
{
  QuadBatch.DrawBorderRect(rect, color, thicknessPx);
}

void UGuiRenderer::DrawTexturedRect(const GuiRect &rect, GLuint texture,
                                    const glm::vec4 &tint)
{
  if (texture == 0)
  {
    return;
  }
  QuadBatch.Flush();
  TexturedQuadBatch.DrawTexturedRect(rect, texture, tint);
}

void UGuiRenderer::DrawTextCenteredInRect(const GuiRect &rect,
                                          const std::string &text,
                                          const glm::vec3 &color)
{
  if (!TextRenderer || text.empty())
  {
    return;
  }
  constexpr float kScale = 1.0f;
  const float textScale = TextScale > 0.f ? TextScale : kScale;
  const glm::vec2 size = TextRenderer->GetTextSize(text, textScale);
  const int x = rect.X + (rect.W - static_cast<int>(size.x)) / 2;
  const int yTop = rect.Y + (rect.H - static_cast<int>(size.y)) / 2;
  DrawText(text, x, yTop, color);
}

int UGuiRenderer::MeasureTextWidth(const std::string &text) const
{
  if (!TextRenderer || text.empty())
  {
    return 0;
  }
  constexpr float kScale = 1.0f;
  const float textScale = TextScale > 0.f ? TextScale : kScale;
  return static_cast<int>(TextRenderer->GetTextSize(text, textScale).x);
}

int UGuiRenderer::MeasureTextHeight(const std::string &text) const
{
  if (!TextRenderer || text.empty())
  {
    return 0;
  }
  constexpr float kScale = 1.0f;
  const float textScale = TextScale > 0.f ? TextScale : kScale;
  return static_cast<int>(TextRenderer->GetTextSize(text, textScale).y);
}

void UGuiRenderer::DrawText(const std::string &text, int x, int yTop,
                            const glm::vec3 &color)
{
  if (!TextRenderer || text.empty() || WindowHeight <= 0)
  {
    return;
  }
  QuadBatch.Flush();

  // FreeType atlas is built at TextRenderer init size; scale is a glyph
  // multiplier (0.7–1.0), not px size.
  constexpr float kScale = 1.0f;
  const float textScale = TextScale > 0.f ? TextScale : kScale;
  const glm::vec2 size = TextRenderer->GetTextSize(text, textScale);
  const float baselineY =
      static_cast<float>(WindowHeight) - static_cast<float>(yTop) - size.y;

  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  TextRenderer->RenderText(text, static_cast<float>(x), baselineY, textScale,
                           color);
}

} // namespace cutum

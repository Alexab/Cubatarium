#include "Gui/Core/GuiRenderer.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/Engine/TextRenderer.h"

#include <GL/glew.h>
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
  if (!quadBatch_.Initialize(uiShader))
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
  return texturedQuadBatch_.Initialize(texShader);
}

void UGuiRenderer::Shutdown()
{
  texturedQuadBatch_.Shutdown();
  quadBatch_.Shutdown();
  TextRenderer.reset();
  clipStack_.clear();
}

void UGuiRenderer::BeginFrame(int WindowWidth, int WindowHeight)
{
  windowWidth_ = WindowWidth;
  windowHeight_ = WindowHeight;
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  if (TextRenderer)
  {
    TextRenderer->SetWindowSize(WindowWidth, WindowHeight);
  }
  quadBatch_.Begin(WindowWidth, WindowHeight);
  texturedQuadBatch_.Begin(WindowWidth, WindowHeight);
  clipStack_.clear();
}

void UGuiRenderer::EndFrame()
{
  quadBatch_.End();
  texturedQuadBatch_.End();
  glDisable(GL_SCISSOR_TEST);
}

void UGuiRenderer::ApplyClipStack()
{
  if (clipStack_.empty())
  {
    glDisable(GL_SCISSOR_TEST);
    return;
  }
  GuiRect clip = clipStack_.back();
  for (int i = static_cast<int>(clipStack_.size()) - 2; i >= 0; --i)
  {
    const GuiRect &r = clipStack_[static_cast<size_t>(i)];
    const int x1 = std::max(clip.x, r.x);
    const int y1 = std::max(clip.y, r.y);
    const int x2 = std::min(clip.x + clip.w, r.x + r.w);
    const int y2 = std::min(clip.y + clip.h, r.y + r.h);
    clip.x = x1;
    clip.y = y1;
    clip.w = std::max(0, x2 - x1);
    clip.h = std::max(0, y2 - y1);
  }
  glEnable(GL_SCISSOR_TEST);
  const int glY = windowHeight_ - (clip.y + clip.h);
  glScissor(clip.x, glY, clip.w, clip.h);
}

void UGuiRenderer::PushClipRect(const GuiRect &rect)
{
  clipStack_.push_back(rect);
  ApplyClipStack();
}

void UGuiRenderer::PopClipRect()
{
  if (!clipStack_.empty())
  {
    clipStack_.pop_back();
  }
  ApplyClipStack();
}

void UGuiRenderer::DrawFilledRect(const GuiRect &rect, const glm::vec4 &color)
{
  quadBatch_.DrawFilledRect(rect, color);
}

void UGuiRenderer::DrawBorderRect(const GuiRect &rect, const glm::vec4 &color,
                                  int thicknessPx)
{
  quadBatch_.DrawBorderRect(rect, color, thicknessPx);
}

void UGuiRenderer::DrawTexturedRect(const GuiRect &rect, GLuint texture,
                                    const glm::vec4 &tint)
{
  if (texture == 0)
  {
    return;
  }
  quadBatch_.Flush();
  texturedQuadBatch_.DrawTexturedRect(rect, texture, tint);
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
  const glm::vec2 size = TextRenderer->GetTextSize(text, kScale);
  const int x = rect.x + (rect.w - static_cast<int>(size.x)) / 2;
  const int yTop = rect.y + (rect.h - static_cast<int>(size.y)) / 2;
  DrawText(text, x, yTop, color);
}

int UGuiRenderer::MeasureTextWidth(const std::string &text) const
{
  if (!TextRenderer || text.empty())
  {
    return 0;
  }
  constexpr float kScale = 1.0f;
  return static_cast<int>(TextRenderer->GetTextSize(text, kScale).x);
}

void UGuiRenderer::DrawText(const std::string &text, int x, int yTop,
                            const glm::vec3 &color)
{
  if (!TextRenderer || text.empty() || windowHeight_ <= 0)
  {
    return;
  }
  quadBatch_.Flush();

  // FreeType atlas is built at TextRenderer init size; scale is a glyph
  // multiplier (0.7–1.0), not px size.
  constexpr float kScale = 1.0f;
  const glm::vec2 size = TextRenderer->GetTextSize(text, kScale);
  const float baselineY =
      static_cast<float>(windowHeight_) - static_cast<float>(yTop) - size.y;

  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  TextRenderer->RenderText(text, static_cast<float>(x), baselineY, kScale,
                           color);
}

} // namespace cutum

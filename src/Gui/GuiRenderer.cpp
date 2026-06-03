#include "GuiRenderer.h"
#include "ShaderManager.h"
#include "TextRenderer.h"

#include <GL/glew.h>
#include <algorithm>

namespace cutum {

GuiRenderer::GuiRenderer() = default;

GuiRenderer::~GuiRenderer()
{
    Shutdown();
}

bool GuiRenderer::Initialize(std::shared_ptr<ShaderManager> shaderManager,
                             std::shared_ptr<TextRenderer> textRenderer)
{
    textRenderer_ = std::move(textRenderer);
    if (!shaderManager) {
        return false;
    }
    auto uiShader = shaderManager->CreateShader("gui_ui", "shaders/vshader_2d.glsl",
                                                "shaders/fshader_2d.glsl");
    if (!uiShader || !uiShader->IsValid()) {
        return false;
    }
    if (!quadBatch_.Initialize(uiShader)) {
        return false;
    }
    auto texShader = shaderManager->CreateShader("gui_textured", "shaders/gui_textured_v.glsl",
                                                 "shaders/gui_textured_f.glsl");
    if (!texShader || !texShader->IsValid()) {
        return false;
    }
    return texturedQuadBatch_.Initialize(texShader);
}

void GuiRenderer::Shutdown()
{
    texturedQuadBatch_.Shutdown();
    quadBatch_.Shutdown();
    textRenderer_.reset();
    clipStack_.clear();
}

void GuiRenderer::BeginFrame(int windowWidth, int windowHeight)
{
    windowWidth_ = windowWidth;
    windowHeight_ = windowHeight;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (textRenderer_) {
        textRenderer_->SetWindowSize(windowWidth, windowHeight);
    }
    quadBatch_.Begin(windowWidth, windowHeight);
    texturedQuadBatch_.Begin(windowWidth, windowHeight);
    clipStack_.clear();
}

void GuiRenderer::EndFrame()
{
    quadBatch_.End();
    texturedQuadBatch_.End();
    glDisable(GL_SCISSOR_TEST);
}

void GuiRenderer::ApplyClipStack()
{
    if (clipStack_.empty()) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    GuiRect clip = clipStack_.back();
    for (int i = static_cast<int>(clipStack_.size()) - 2; i >= 0; --i) {
        const GuiRect& r = clipStack_[static_cast<size_t>(i)];
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

void GuiRenderer::PushClipRect(const GuiRect& rect)
{
    clipStack_.push_back(rect);
    ApplyClipStack();
}

void GuiRenderer::PopClipRect()
{
    if (!clipStack_.empty()) {
        clipStack_.pop_back();
    }
    ApplyClipStack();
}

void GuiRenderer::DrawFilledRect(const GuiRect& rect, const glm::vec4& color)
{
    quadBatch_.DrawFilledRect(rect, color);
}

void GuiRenderer::DrawBorderRect(const GuiRect& rect, const glm::vec4& color, int thicknessPx)
{
    quadBatch_.DrawBorderRect(rect, color, thicknessPx);
}

void GuiRenderer::DrawTexturedRect(const GuiRect& rect, GLuint texture, const glm::vec4& tint)
{
    if (texture == 0) {
        return;
    }
    quadBatch_.Flush();
    texturedQuadBatch_.DrawTexturedRect(rect, texture, tint);
}

void GuiRenderer::DrawTextCenteredInRect(const GuiRect& rect, const std::string& text,
                                         const glm::vec3& color)
{
    if (!textRenderer_ || text.empty()) {
        return;
    }
    constexpr float kScale = 1.0f;
    const glm::vec2 size = textRenderer_->GetTextSize(text, kScale);
    const int x = rect.x + (rect.w - static_cast<int>(size.x)) / 2;
    const int yTop = rect.y + (rect.h - static_cast<int>(size.y)) / 2;
    DrawText(text, x, yTop, color);
}

int GuiRenderer::MeasureTextWidth(const std::string& text) const
{
    if (!textRenderer_ || text.empty()) {
        return 0;
    }
    constexpr float kScale = 1.0f;
    return static_cast<int>(textRenderer_->GetTextSize(text, kScale).x);
}

void GuiRenderer::DrawText(const std::string& text, int x, int yTop, const glm::vec3& color)
{
    if (!textRenderer_ || text.empty() || windowHeight_ <= 0) {
        return;
    }
    quadBatch_.Flush();

    // FreeType atlas is built at TextRenderer init size; scale is a glyph multiplier (0.7–1.0), not px size.
    constexpr float kScale = 1.0f;
    const glm::vec2 size = textRenderer_->GetTextSize(text, kScale);
    const float baselineY =
        static_cast<float>(windowHeight_) - static_cast<float>(yTop) - size.y;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    textRenderer_->RenderText(text, static_cast<float>(x), baselineY, kScale, color);
}

} // namespace cutum

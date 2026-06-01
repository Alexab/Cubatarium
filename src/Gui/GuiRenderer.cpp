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
    return quadBatch_.Initialize(uiShader);
}

void GuiRenderer::Shutdown()
{
    quadBatch_.Shutdown();
    textRenderer_.reset();
    clipStack_.clear();
}

void GuiRenderer::BeginFrame(int windowWidth, int windowHeight)
{
    windowWidth_ = windowWidth;
    windowHeight_ = windowHeight;
    if (textRenderer_) {
        textRenderer_->SetWindowSize(windowWidth, windowHeight);
    }
    quadBatch_.Begin(windowWidth, windowHeight);
    clipStack_.clear();
}

void GuiRenderer::EndFrame()
{
    quadBatch_.End();
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

void GuiRenderer::DrawText(const std::string& text, int x, int y, float scale,
                           const glm::vec3& color)
{
    if (!textRenderer_ || text.empty()) {
        return;
    }
    quadBatch_.Flush();
    textRenderer_->RenderText(text, static_cast<float>(x), static_cast<float>(y), scale, color);
}

} // namespace cutum

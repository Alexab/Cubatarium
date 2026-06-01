#include "UiQuadBatch.h"
#include "ShaderManager.h"

#include <GL/glew.h>
#include <algorithm>
#include <iostream>

namespace cutum {

UiQuadBatch::UiQuadBatch() = default;

UiQuadBatch::~UiQuadBatch()
{
    Shutdown();
}

bool UiQuadBatch::Initialize(std::shared_ptr<ShaderProgram> shader)
{
    if (!shader || !shader->IsValid()) {
        return false;
    }
    shader_ = std::move(shader);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * 6 * 256, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    initialized_ = true;
    return true;
}

void UiQuadBatch::Shutdown()
{
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    shader_.reset();
    initialized_ = false;
}

void UiQuadBatch::Begin(int windowWidth, int windowHeight)
{
    windowWidth_ = windowWidth;
    windowHeight_ = windowHeight;
    vertices_.clear();

    GLboolean depthTest;
    glGetBooleanv(GL_DEPTH_TEST, &depthTest);
    depthTestWasEnabled_ = depthTest == GL_TRUE;
    glDisable(GL_DEPTH_TEST);

    GLboolean blend;
    glGetBooleanv(GL_BLEND, &blend);
    blendWasEnabled_ = blend == GL_TRUE;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void UiQuadBatch::End()
{
    Flush();
    if (depthTestWasEnabled_) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    if (!blendWasEnabled_) {
        glDisable(GL_BLEND);
    }
}

void UiQuadBatch::GuiRectToShaderCoords(const GuiRect& rect, float& x0, float& y0, float& x1,
                                        float& y1) const
{
    x0 = static_cast<float>(rect.x);
    x1 = static_cast<float>(rect.x + rect.w);
    const float h = static_cast<float>(windowHeight_);
    y0 = h - static_cast<float>(rect.y + rect.h);
    y1 = h - static_cast<float>(rect.y);
}

void UiQuadBatch::AddQuad(float x0, float y0, float x1, float y1, const glm::vec4& color)
{
    if (color != currentColor_ && !vertices_.empty()) {
        Flush();
    }
    currentColor_ = color;
    vertices_.push_back({x0, y0});
    vertices_.push_back({x1, y0});
    vertices_.push_back({x0, y1});
    vertices_.push_back({x0, y1});
    vertices_.push_back({x1, y0});
    vertices_.push_back({x1, y1});
}

void UiQuadBatch::Flush()
{
    if (!initialized_ || vertices_.empty() || !shader_) {
        vertices_.clear();
        return;
    }

    shader_->Use();
    shader_->SetVec2("screenSize", glm::vec2(windowWidth_, windowHeight_));
    shader_->SetVec3("color", glm::vec3(currentColor_.r, currentColor_.g, currentColor_.b));

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(vertices_.size() * sizeof(Vertex)),
                    vertices_.data());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
    glBindVertexArray(0);
    shader_->Unuse();
    vertices_.clear();
}

void UiQuadBatch::DrawFilledRect(const GuiRect& rect, const glm::vec4& color)
{
    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    GuiRectToShaderCoords(rect, x0, y0, x1, y1);
    AddQuad(x0, y0, x1, y1, color);
}

void UiQuadBatch::DrawBorderRect(const GuiRect& rect, const glm::vec4& color, int thicknessPx)
{
    const int t = std::max(1, thicknessPx);
    DrawFilledRect({rect.x, rect.y, rect.w, t}, color);
    DrawFilledRect({rect.x, rect.y + rect.h - t, rect.w, t}, color);
    DrawFilledRect({rect.x, rect.y, t, rect.h}, color);
    DrawFilledRect({rect.x + rect.w - t, rect.y, t, rect.h}, color);
}

} // namespace cutum

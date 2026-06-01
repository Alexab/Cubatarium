#include "UiTexturedQuadBatch.h"
#include "ShaderManager.h"

#include <GL/glew.h>

namespace cutum {

UiTexturedQuadBatch::UiTexturedQuadBatch() = default;

UiTexturedQuadBatch::~UiTexturedQuadBatch()
{
    Shutdown();
}

bool UiTexturedQuadBatch::Initialize(std::shared_ptr<ShaderProgram> shader)
{
    if (!shader || !shader->IsValid()) {
        return false;
    }
    shader_ = std::move(shader);

    struct Vertex {
        float x;
        float y;
        float u;
        float v;
    };

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * 6, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(sizeof(float) * 2));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    initialized_ = true;
    return true;
}

void UiTexturedQuadBatch::Shutdown()
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

void UiTexturedQuadBatch::Begin(int windowWidth, int windowHeight)
{
    windowWidth_ = windowWidth;
    windowHeight_ = windowHeight;
    boundTexture_ = 0;

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

void UiTexturedQuadBatch::GuiRectToShaderCoords(const GuiRect& rect, float& x0, float& y0,
                                                float& x1, float& y1) const
{
    x0 = static_cast<float>(rect.x);
    x1 = static_cast<float>(rect.x + rect.w);
    const float top = static_cast<float>(rect.y);
    const float bottom = static_cast<float>(rect.y + rect.h);
    y0 = static_cast<float>(windowHeight_) - bottom;
    y1 = static_cast<float>(windowHeight_) - top;
}

void UiTexturedQuadBatch::DrawTexturedRect(const GuiRect& rect, GLuint texture,
                                           const glm::vec4& tint)
{
    if (!initialized_ || !shader_ || texture == 0) {
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

    shader_->Use();
    shader_->SetVec2("screenSize", glm::vec2(static_cast<float>(windowWidth_),
                                             static_cast<float>(windowHeight_)));
    shader_->SetVec4("tint", tint);
    shader_->SetInt("texture0", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    shader_->Unuse();
}

void UiTexturedQuadBatch::End()
{
    if (depthTestWasEnabled_) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    if (!blendWasEnabled_) {
        glDisable(GL_BLEND);
    }
}

} // namespace cutum

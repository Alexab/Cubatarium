#ifndef UI_TEXTURED_QUAD_BATCH_H
#define UI_TEXTURED_QUAD_BATCH_H

#include "GuiTypes.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

typedef unsigned int GLuint;

namespace cutum {

class ShaderProgram;

class UiTexturedQuadBatch {
public:
    UiTexturedQuadBatch();
    ~UiTexturedQuadBatch();

    bool Initialize(std::shared_ptr<ShaderProgram> shader);
    void Shutdown();

    void Begin(int windowWidth, int windowHeight);
    void DrawTexturedRect(const GuiRect& rect, GLuint texture, const glm::vec4& tint);
    void End();

private:
    void GuiRectToShaderCoords(const GuiRect& rect, float& x0, float& y0, float& x1, float& y1) const;

    std::shared_ptr<ShaderProgram> shader_;
    GLuint vao_{0};
    GLuint vbo_{0};
    int windowWidth_{0};
    int windowHeight_{0};
    bool initialized_{false};
    bool depthTestWasEnabled_{false};
    bool blendWasEnabled_{false};
    GLuint boundTexture_{0};
};

} // namespace cutum

#endif

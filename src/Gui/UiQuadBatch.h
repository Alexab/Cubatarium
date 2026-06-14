#ifndef UI_QUAD_BATCH_H
#define UI_QUAD_BATCH_H

#include "GuiTypes.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

typedef unsigned int GLuint;

namespace cutum {

class UShaderProgram;

class UiQuadBatch {
public:
    UiQuadBatch();
    ~UiQuadBatch();

    bool Initialize(std::shared_ptr<UShaderProgram> shader);
    void Shutdown();

    void Begin(int WindowWidth, int WindowHeight);
    void DrawFilledRect(const GuiRect& rect, const glm::vec4& color);
    void DrawBorderRect(const GuiRect& rect, const glm::vec4& color, int thicknessPx);
    void Flush();
    void End();

    bool IsReady() const { return initialized_; }

private:
    void AddQuad(float x0, float y0, float x1, float y1, const glm::vec4& color);
    /// GuiRect — top-left origin; shader expects bottom-left pixel Y.
    void GuiRectToShaderCoords(const GuiRect& rect, float& x0, float& y0, float& x1, float& y1) const;

    std::shared_ptr<UShaderProgram> shader_;
    GLuint vao_{0};
    GLuint vbo_{0};
    int windowWidth_{0};
    int windowHeight_{0};
    bool initialized_{false};
    bool depthTestWasEnabled_{false};
    bool blendWasEnabled_{false};

    struct Vertex {
        float x;
        float y;
    };
    std::vector<Vertex> vertices_;
    glm::vec4 currentColor_{1.0f};
};

} // namespace cutum

#endif

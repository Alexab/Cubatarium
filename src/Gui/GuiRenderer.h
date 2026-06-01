#ifndef GUI_RENDERER_H
#define GUI_RENDERER_H

#include "GuiTypes.h"
#include "UiQuadBatch.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace cutum {

class TextRenderer;
class ShaderManager;

class GuiRenderer {
public:
    GuiRenderer();
    ~GuiRenderer();

    bool Initialize(std::shared_ptr<ShaderManager> shaderManager,
                    std::shared_ptr<TextRenderer> textRenderer);
    void Shutdown();

    void BeginFrame(int windowWidth, int windowHeight);
    void EndFrame();

    void DrawFilledRect(const GuiRect& rect, const glm::vec4& color);
    void DrawBorderRect(const GuiRect& rect, const glm::vec4& color, int thicknessPx);
    /// @p yTop — отступ сверху (как в GUI), не baseline FreeType.
    void DrawText(const std::string& text, int x, int yTop, const glm::vec3& color);
    void DrawTextCenteredInRect(const GuiRect& rect, const std::string& text, const glm::vec3& color);

    void PushClipRect(const GuiRect& rect);
    void PopClipRect();

    int GetWindowWidth() const { return windowWidth_; }
    int GetWindowHeight() const { return windowHeight_; }

private:
    void ApplyClipStack();

    std::shared_ptr<TextRenderer> textRenderer_;
    UiQuadBatch quadBatch_;
    int windowWidth_{0};
    int windowHeight_{0};
    std::vector<GuiRect> clipStack_;
};

} // namespace cutum

#endif

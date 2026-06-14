#ifndef GUI_RENDERER_H
#define GUI_RENDERER_H

#include "Gui/Core/GuiTypes.h"
#include "Gui/Batch/UiQuadBatch.h"
#include "Gui/Batch/UiTexturedQuadBatch.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace cutum
{

class UTextRenderer;
class UShaderManager;

class UGuiRenderer
{
public:
  UGuiRenderer();
  ~UGuiRenderer();

  bool Initialize(std::shared_ptr<UShaderManager> shaderManager,
                  std::shared_ptr<UTextRenderer> textRenderer);
  void Shutdown();

  void BeginFrame(int WindowWidth, int WindowHeight);
  void EndFrame();

  void DrawFilledRect(const GuiRect &rect, const glm::vec4 &color);
  void DrawBorderRect(const GuiRect &rect, const glm::vec4 &color,
                      int thicknessPx);
  void DrawTexturedRect(const GuiRect &rect, GLuint texture,
                        const glm::vec4 &tint = glm::vec4(1.0f));
  /// @p yTop — отступ сверху (как в GUI), не baseline FreeType.
  void DrawText(const std::string &text, int x, int yTop,
                const glm::vec3 &color);
  void DrawTextCenteredInRect(const GuiRect &rect, const std::string &text,
                              const glm::vec3 &color);
  int MeasureTextWidth(const std::string &text) const;

  void PushClipRect(const GuiRect &rect);
  void PopClipRect();

  int GetWindowWidth() const { return windowWidth_; }
  int GetWindowHeight() const { return windowHeight_; }

private:
  void ApplyClipStack();

  std::shared_ptr<UTextRenderer> TextRenderer;
  UiQuadBatch quadBatch_;
  UiTexturedQuadBatch texturedQuadBatch_;
  int windowWidth_{0};
  int windowHeight_{0};
  std::vector<GuiRect> clipStack_;
};

} // namespace cutum

#endif

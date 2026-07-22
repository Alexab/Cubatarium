#ifndef GUI_RENDERER_H
#define GUI_RENDERER_H

#include "Gui/Batch/UiQuadBatch.h"
#include "Gui/Batch/UiTexturedQuadBatch.h"
#include "Gui/Core/GuiTypes.h"
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

  void BeginFrame(int window_width, int window_height);
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
  int MeasureTextHeight(const std::string &text = "Ag") const;

  void PushClipRect(const GuiRect &rect);
  void PopClipRect();

  int GetWindowWidth() const { return WindowWidth; }
  int GetWindowHeight() const { return WindowHeight; }

  void SetTextScale(float scale) { TextScale = scale; }
  float GetTextScale() const { return TextScale; }

private:
  void ApplyClipStack();

  std::shared_ptr<UTextRenderer> TextRenderer;
  UGuiQuadBatch QuadBatch;
  UGuiTexturedQuadBatch TexturedQuadBatch;
  int WindowWidth{0};
  int WindowHeight{0};
  float TextScale{1.f};
  std::vector<GuiRect> ClipStack;
};

} // namespace cutum

#endif

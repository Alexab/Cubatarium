#ifndef UI_TEXTURED_QUAD_BATCH_H
#define UI_TEXTURED_QUAD_BATCH_H

#include "Gui/Core/GuiTypes.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

typedef unsigned int GLuint;

namespace cutum
{

class UShaderProgram;

class UGuiTexturedQuadBatch
{
public:
  UGuiTexturedQuadBatch();
  ~UGuiTexturedQuadBatch();

  bool Initialize(std::shared_ptr<UShaderProgram> shader);
  void Shutdown();

  void Begin(int window_width, int window_height);
  void DrawTexturedRect(const GuiRect &rect, GLuint texture,
                        const glm::vec4 &tint);
  void End();

private:
  void GuiRectToShaderCoords(const GuiRect &rect, float &x0, float &y0,
                             float &x1, float &y1) const;

  std::shared_ptr<UShaderProgram> Shader;
  GLuint Vao{0};
  GLuint Vbo{0};
  int WindowWidth{0};
  int WindowHeight{0};
  bool Initialized{false};
  bool DepthTestWasEnabled{false};
  bool BlendWasEnabled{false};
  GLuint BoundTexture{0};
};

} // namespace cutum

#endif

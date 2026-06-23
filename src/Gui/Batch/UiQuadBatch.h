#ifndef UI_QUAD_BATCH_H
#define UI_QUAD_BATCH_H

#include "Gui/Core/GuiTypes.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

typedef unsigned int GLuint;

namespace cutum
{

class UShaderProgram;

class UGuiQuadBatch
{
public:
  UGuiQuadBatch();
  ~UGuiQuadBatch();

  bool Initialize(std::shared_ptr<UShaderProgram> shader);
  void Shutdown();

  void Begin(int window_width, int window_height);
  void DrawFilledRect(const GuiRect &rect, const glm::vec4 &color);
  void DrawBorderRect(const GuiRect &rect, const glm::vec4 &color,
                      int thicknessPx);
  void Flush();
  void End();

  bool IsReady() const { return Initialized; }

private:
  void AddQuad(float x0, float y0, float x1, float y1, const glm::vec4 &color);
  /// GuiRect — top-left origin; shader expects bottom-left pixel Y.
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

  struct Vertex
  {
    float x;
    float y;
  };
  std::vector<Vertex> Vertices;
  glm::vec4 CurrentColor{1.0f};
};

} // namespace cutum

#endif

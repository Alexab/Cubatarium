#pragma once
#include "Items/ItemLabDiagnostics.h"
#include <memory>

namespace cutum
{
class UShaderManager;
class UShaderProgram;
class UItemLabOverlayRenderer
{
public:
  explicit UItemLabOverlayRenderer(UShaderManager &shaders);
  ~UItemLabOverlayRenderer();
  size_t DrawContacts(const ItemRenderAsset &asset,
                      const ItemVisualState &state, glm::vec3 physicalSize,
                      const glm::mat4 &mvp);
  size_t DrawUvTiles(const ItemRenderAsset &asset);

private:
  struct Vertex
  {
    glm::vec3 Position{0};
    glm::vec4 Color{1};
    glm::vec2 Uv{0};
  };
  void Draw(const std::vector<Vertex> &vertices, unsigned mode,
            const glm::mat4 &matrix, unsigned texture = 0);
  void Lines(const std::vector<ItemLabLineVertex> &lines,
             const glm::mat4 &matrix);
  std::shared_ptr<UShaderProgram> Shader;
  unsigned Vao{0}, Vbo{0};
};
} // namespace cutum

#ifndef ITEM_PREVIEW_RENDERER_H
#define ITEM_PREVIEW_RENDERER_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>

typedef unsigned int GLuint;

namespace cutum
{

class UItemDefinitionStorage;
class UShaderManager;

/// Lightweight 3D preview renderer for item tools.
/// - Primary goal (UX): show rotated "3D-ish" tool model in UI.
/// - Optional: when item `model` json exists, try parsing simple `parts[]`
///   schema (offset/size/texture) and render it as multiple cubes.
class UItemPreviewRenderer
{
public:
  UItemPreviewRenderer(std::shared_ptr<UItemDefinitionStorage> items,
                        std::shared_ptr<UShaderManager> shaderManager);
  ~UItemPreviewRenderer();

  bool Initialize();
  void Shutdown();
  void Invalidate();

  /// Renders to an owned texture (caller must keep/delete it).
  GLuint RenderToUniqueTexture(const std::string &itemId, int size,
                                float yawDeg, float pitchDeg);

private:
  struct Part
  {
    std::string textureStem;
    float ox{0.f};
    float oy{0.f};
    float oz{0.f};
    float sx{1.f};
    float sy{1.f};
    float sz{1.f};
  };

  bool InitCubeMesh();
  bool EnsureFboSize(int size);
  GLuint GetOrCreateColorTexture(const std::string &itemId);

  bool TryLoadPartsFromModelJson(const std::string &itemId,
                                  std::string &outModelRelPath,
                                  std::vector<Part> &outParts) const;

  std::vector<Part> FallbackParts(const std::string &itemId) const;

  glm::mat4 OrbitView(float yawDeg, float pitchDeg, float distance) const;

  std::shared_ptr<UItemDefinitionStorage> Items;
  std::shared_ptr<UShaderManager> ShaderManager;

  std::shared_ptr<class UShaderProgram> Shader;

  GLuint CubeVao{0};
  GLuint CubeVbo{0};
  GLuint CubeEbo{0};

  GLuint Fbo{0};
  GLuint ColorTex{0};
  GLuint DepthRbo{0};
  int FboSize{0};

  std::unordered_map<std::string, GLuint> ColorTexCache;
};

} // namespace cutum

#endif


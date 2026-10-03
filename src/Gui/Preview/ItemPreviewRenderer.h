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

/// Lightweight 3D preview renderer for item tools / armor props.
/// Supports (in order): static glTF via CreatureGltfLoader, parts[] JSON cubes,
/// then procedural FallbackParts.
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

  /// Returns true if a static glTF mesh was drawn into the bound FBO.
  bool TryDrawGltfModel(const std::string &itemId, const std::string &modelRel,
                        const glm::mat4 &projection, const glm::mat4 &view);

  std::vector<Part> FallbackParts(const std::string &itemId) const;

  glm::mat4 OrbitView(float yawDeg, float pitchDeg, float distance) const;

  std::shared_ptr<UItemDefinitionStorage> Items;
  std::shared_ptr<UShaderManager> ShaderManager;

  std::shared_ptr<class UShaderProgram> Shader;

  GLuint CubeVao{0};
  GLuint CubeVbo{0};
  GLuint CubeEbo{0};
  GLuint ScratchMeshVao{0};
  GLuint ScratchMeshVbo{0};
  GLuint ScratchMeshEbo{0};

  GLuint Fbo{0};
  GLuint ColorTex{0};
  GLuint DepthRbo{0};
  int FboSize{0};

  std::unordered_map<std::string, GLuint> ColorTexCache;
};

} // namespace cutum

#endif


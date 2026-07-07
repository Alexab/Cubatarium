#ifndef CONTENT_PREVIEW_RENDERER_H
#define CONTENT_PREVIEW_RENDERER_H

#include "Gui/Interfaces/IUContentCatalog.h"
#include "World/Math/BlockTypes.h"
#include <glm/mat4x4.hpp>
#include <memory>
#include <string>

typedef unsigned int GLuint;

namespace cutum
{

class UBlockDefinitionStorage;
class UObjectLibrary;
class UShaderManager;
class UTextureCubeStorage;
class UCreaturePreviewRenderer;

class UContentPreviewRenderer
{
public:
  UContentPreviewRenderer(std::shared_ptr<UObjectLibrary> objects,
                          std::shared_ptr<UTextureCubeStorage> textures,
                          std::shared_ptr<UBlockDefinitionStorage> blockDefs,
                          std::shared_ptr<UShaderManager> shaderManager,
                          std::shared_ptr<UCreaturePreviewRenderer> creatures =
                              nullptr);
  ~UContentPreviewRenderer();

  bool Initialize();
  void Shutdown();

  bool SupportsKind(ContentKind kind) const;
  GLuint Render(ContentKind kind, const std::string &id, int size,
                float yawDeg, float pitchDeg);
  /// Stable texture for dock preview (not shared with icon-warmup FBO).
  GLuint RenderUnique(ContentKind kind, const std::string &id, int size,
                      float yawDeg, float pitchDeg);

private:
  bool EnsureFboSize(int size);
  bool InitCubeMesh();
  GLuint GetBlockTexture(BlockId blockId) const;
  GLuint RenderObject(const std::string &objectName, int size, float yawDeg,
                      float pitchDeg);
  GLuint RenderBlock(const std::string &blockName, int size, float yawDeg,
                     float pitchDeg);
  glm::mat4 OrbitView(float yawDeg, float pitchDeg, float distance) const;

  std::shared_ptr<UObjectLibrary> Objects;
  std::shared_ptr<UTextureCubeStorage> Textures;
  std::shared_ptr<UBlockDefinitionStorage> BlockDefs;
  std::shared_ptr<UShaderManager> ShaderManager;
  std::shared_ptr<UCreaturePreviewRenderer> Creatures;
  std::shared_ptr<class UShaderProgram> Shader;

  GLuint CubeVao{0};
  GLuint CubeVbo{0};
  GLuint CubeEbo{0};
  GLuint Fbo{0};
  GLuint ColorTex{0};
  GLuint DepthRbo{0};
  int FboSize{0};
};

} // namespace cutum

#endif

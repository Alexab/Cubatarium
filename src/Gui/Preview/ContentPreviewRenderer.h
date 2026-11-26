#ifndef CONTENT_PREVIEW_RENDERER_H
#define CONTENT_PREVIEW_RENDERER_H

#include "Gui/Interfaces/IUContentCatalog.h"
#include "World/Math/BlockTypes.h"
#include <cstddef>
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
class UItemPreviewRenderer;

struct ContentPreviewRendererStorage
{
  std::size_t rendererBytes{}, vertexBytes{}, indexBytes{}, colorTexels{}, depthSamples{};
  int targetSize{};
  GLuint vertexArray{}, vertexBuffer{}, indexBuffer{}, framebuffer{}, colorTexture{}, depthBuffer{};
  bool present{}, bindingsMatch{}, supported{}, shaderPresent{}, creatureSourcePresent{}, itemSourcePresent{};
};

class UContentPreviewRenderer
{
public:
  UContentPreviewRenderer(std::shared_ptr<UObjectLibrary> objects,
                          std::shared_ptr<UTextureCubeStorage> textures,
                          std::shared_ptr<UBlockDefinitionStorage> blockDefs,
                          std::shared_ptr<UShaderManager> shaderManager,
                          std::shared_ptr<UCreaturePreviewRenderer> creatures =
                              nullptr,
                          std::shared_ptr<UItemPreviewRenderer> itemPreview =
                              nullptr);
  ~UContentPreviewRenderer();
  UContentPreviewRenderer(const UContentPreviewRenderer &) = delete;
  UContentPreviewRenderer &operator=(const UContentPreviewRenderer &) = delete;

  bool Initialize();
  void Shutdown();
  ContentPreviewRendererStorage InspectOwnedStorage(
      const UObjectLibrary *objects, const UTextureCubeStorage *textures,
      const UBlockDefinitionStorage *blocks, const UShaderManager *shaders,
      const UCreaturePreviewRenderer *creatures) const noexcept;

  bool SupportsKind(ContentKind kind) const;
  // Block and object outputs borrow the current target until resize or shutdown.
  GLuint Render(ContentKind kind, const std::string &id, int size,
                float yawDeg, float pitchDeg);
  /// A successful call transfers the output texture to its caller.
  GLuint RenderUnique(ContentKind kind, const std::string &id, int size,
                      float yawDeg, float pitchDeg, float animTimeSec = 0.f,
                      bool animateWalk = false);

public:
  template<class Inspector> void InspectCatalogDomain(Inspector &out) const
  {out.Library(Objects);out.TextureCubes(Textures);out.BlockStorage(BlockDefs,"content-preview");}
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
  std::shared_ptr<UItemPreviewRenderer> ItemPreview;
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

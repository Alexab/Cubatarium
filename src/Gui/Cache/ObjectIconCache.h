#ifndef OBJECT_ICON_CACHE_H
#define OBJECT_ICON_CACHE_H

#include "Gui/Cache/IconWarmupQueue.h"
#include "Gui/Preview/PreviewRenderResources.h"
#include "Render/Textures/GlTextureOwner.h"
#include "World/Math/BlockTypes.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

typedef unsigned int GLuint;

namespace cutum
{

class UBlockDefinitionStorage;
class UObjectLibrary;
class UShaderManager;
class UTextureCubeStorage;
class UInventoryIconService;

struct ObjectIconStorage
{
  size_t sourceBytes{}, cacheBytes{}, cpuBytes{};
  size_t objectTableBytes{}, blockTableBytes{}, objectQueueBytes{}, blockQueueBytes{};
  size_t objectIcons{}, blockIcons{}, pendingObjects{}, pendingBlocks{};
  size_t vertexBytes{}, indexBytes{}, targetTexels{}, depthSamples{}, cachedTexels{};
  GLuint vertexArray{}, vertexBuffer{}, indexBuffer{}, framebuffer{}, colorTexture{}, depthBuffer{};
  bool present{}, bindingsMatch{}, supported{}, atlasFallback{}, shaderPresent{}, servicePresent{};
};

class UObjectIconCache
{
public:
  UObjectIconCache(std::shared_ptr<UObjectLibrary> objects,
                   std::shared_ptr<UTextureCubeStorage> textures,
                   std::shared_ptr<UBlockDefinitionStorage> blockDefs,
                   std::shared_ptr<UShaderManager> shaderManager,
                   std::shared_ptr<UInventoryIconService> iconService = nullptr);
  ~UObjectIconCache();
  UObjectIconCache(const UObjectIconCache &) = delete;
  UObjectIconCache &operator=(const UObjectIconCache &) = delete;

  bool Initialize();
  void Shutdown();

  GLuint GetIcon(const std::string &objectName);
  GLuint GetIconIfCached(const std::string &objectName) const;
  void WarmupNext(size_t count);

  GLuint GetBlockIconTexture(const std::string &blockName);
  /// UX U4: cached-only lookup (0 when not ready) for placeholder grids.
  GLuint GetBlockIconTextureIfCached(const std::string &blockName) const;
  void QueueBlockIcon(const std::string &blockName);
  void WarmupBlockIcons(size_t count);
  size_t PendingBlockWarmupCount() const { return BlockWarmup.Size(); }

  void ClearBlockIconCache();
  ObjectIconStorage InspectOwnedStorage(const UObjectLibrary *objects,
      const UTextureCubeStorage *textures, const UBlockDefinitionStorage *blocks,
      const UShaderManager *shaders) const noexcept;

public:
  template<class Inspector> void InspectCatalogDomain(Inspector &out) const
  {out.Library(Objects);out.TextureCubes(Textures);out.BlockStorage(BlockDefs,"object-icons");}
private:
  friend class UGuiIconSource;
  std::string BuildObjectFingerprint(const std::string &objectName) const;
  std::string BuildBlockFingerprint(const std::string &blockName) const;
  GLuint RenderObjectIcon(const std::string &objectName);
  GLuint RenderBlockIcon(BlockId blockId);
  GLuint GetBlockTexture(BlockId blockId) const;

  std::shared_ptr<UObjectLibrary> Objects;
  std::shared_ptr<UTextureCubeStorage> Textures;
  std::shared_ptr<UBlockDefinitionStorage> BlockDefs;
  std::shared_ptr<UShaderManager> ShaderManager;
  std::shared_ptr<UInventoryIconService> IconService;
  std::shared_ptr<class UShaderProgram> Shader;

  std::unordered_map<std::string, UGlTextureOwner> Cache;
  std::unordered_map<BlockId, UGlTextureOwner> BlockCache;
  UGuiIconWarmupQueue BlockWarmup;
  UGuiIconWarmupQueue Warmup;

  PreviewCubeMesh Cube;
  PreviewRenderTarget Target;
  static constexpr int kIconSize = 64;
};

} // namespace cutum

#endif

#ifndef OBJECT_ICON_CACHE_H
#define OBJECT_ICON_CACHE_H

#include "Gui/Cache/IconWarmupQueue.h"
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

class UObjectIconCache
{
public:
  UObjectIconCache(std::shared_ptr<UObjectLibrary> objects,
                   std::shared_ptr<UTextureCubeStorage> textures,
                   std::shared_ptr<UBlockDefinitionStorage> blockDefs,
                   std::shared_ptr<UShaderManager> shaderManager,
                   std::shared_ptr<UInventoryIconService> iconService = nullptr);
  ~UObjectIconCache();

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

private:
  std::string BuildObjectFingerprint(const std::string &objectName) const;
  std::string BuildBlockFingerprint(const std::string &blockName) const;
  GLuint RenderObjectIcon(const std::string &objectName);
  GLuint RenderBlockIcon(BlockId blockId);
  GLuint GetBlockTexture(BlockId blockId) const;
  bool InitCubeMesh();

  std::shared_ptr<UObjectLibrary> Objects;
  std::shared_ptr<UTextureCubeStorage> Textures;
  std::shared_ptr<UBlockDefinitionStorage> BlockDefs;
  std::shared_ptr<UShaderManager> ShaderManager;
  std::shared_ptr<UInventoryIconService> IconService;
  std::shared_ptr<class UShaderProgram> Shader;

  std::unordered_map<std::string, GLuint> Cache;
  std::unordered_map<BlockId, GLuint> BlockCache;
  UGuiIconWarmupQueue BlockWarmup;
  UGuiIconWarmupQueue Warmup;

  GLuint CubeVao{0};
  GLuint CubeVbo{0};
  GLuint CubeEbo{0};
  GLuint Fbo{0};
  GLuint ColorTex{0};
  GLuint DepthRbo{0};
  static constexpr int kIconSize = 64;
};

} // namespace cutum

#endif

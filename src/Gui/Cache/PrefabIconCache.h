#ifndef PREFAB_ICON_CACHE_H
#define PREFAB_ICON_CACHE_H

#include "World/Math/BlockTypes.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

typedef unsigned int GLuint;

namespace cutum
{

class UBlockDefinitionStorage;
class UPrefabLibrary;
class UShaderManager;
class UTextureCubeStorage;

class UPrefabIconCache
{
public:
  UPrefabIconCache(std::shared_ptr<UPrefabLibrary> prefabs,
                   std::shared_ptr<UTextureCubeStorage> textures,
                   std::shared_ptr<UBlockDefinitionStorage> blockDefs,
                   std::shared_ptr<UShaderManager> shaderManager);
  ~UPrefabIconCache();

  bool Initialize();
  void Shutdown();

  GLuint GetIcon(const std::string &prefabName);
  GLuint GetIconIfCached(const std::string &prefabName) const;
  void WarmupNext(size_t count);

  GLuint GetBlockIconTexture(const std::string &blockName);

private:
  GLuint RenderPrefabIcon(const std::string &prefabName);
  GLuint RenderBlockIcon(BlockId blockId);
  GLuint GetBlockTexture(BlockId blockId) const;
  bool InitCubeMesh();

  std::shared_ptr<UPrefabLibrary> Prefabs;
  std::shared_ptr<UTextureCubeStorage> Textures;
  std::shared_ptr<UBlockDefinitionStorage> BlockDefs;
  std::shared_ptr<UShaderManager> ShaderManager;
  std::shared_ptr<class UShaderProgram> Shader;

  std::unordered_map<std::string, GLuint> Cache;
  std::unordered_map<BlockId, GLuint> BlockCache;
  std::vector<std::string> WarmupQueue;
  size_t WarmupIndex{0};

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

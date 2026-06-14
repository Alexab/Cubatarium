#ifndef CREATURE_ICON_CACHE_H
#define CREATURE_ICON_CACHE_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

typedef unsigned int GLuint;

namespace cutum
{

class UCreatureDefinitionStorage;
class UCreatureTextureStorage;
class UShaderManager;
class USkinDefinitionStorage;

class UCreatureIconCache
{
public:
  UCreatureIconCache(std::shared_ptr<UCreatureDefinitionStorage> species,
                     std::shared_ptr<USkinDefinitionStorage> skins,
                     std::shared_ptr<UCreatureTextureStorage> textures,
                     std::shared_ptr<UShaderManager> shaderManager);
  ~UCreatureIconCache();

  bool Initialize();
  void Shutdown();

  GLuint GetSpeciesIcon(const std::string &speciesId);
  GLuint GetSkinIcon(const std::string &skinId);
  void WarmupNext(size_t count);

private:
  bool InitCubeMesh();
  GLuint RenderSolidColorIcon(float r, float g, float b, float a);
  GLuint RenderSpeciesPartsIcon(const std::string &speciesId);
  GLuint GetOrCreateSpeciesIcon(const std::string &speciesId);
  GLuint GetOrCreateSkinIcon(const std::string &skinId);

  std::shared_ptr<UCreatureDefinitionStorage> species_;
  std::shared_ptr<USkinDefinitionStorage> skins_;
  std::shared_ptr<UCreatureTextureStorage> textures_;
  std::shared_ptr<UShaderManager> ShaderManager;
  std::shared_ptr<class UShaderProgram> shader_;

  std::unordered_map<std::string, GLuint> speciesCache_;
  std::unordered_map<std::string, GLuint> skinCache_;
  std::vector<std::string> warmupQueue_;
  size_t warmupIndex_{0};

  GLuint fbo_{0};
  GLuint colorTex_{0};
  GLuint depthRbo_{0};
  GLuint cubeVao_{0};
  GLuint cubeVbo_{0};
  GLuint cubeEbo_{0};
  GLuint headCubeVao_{0};
  GLuint headCubeVbo_{0};
  GLuint headCubeEbo_{0};
  GLuint bodyCubeVao_{0};
  GLuint bodyCubeVbo_{0};
  GLuint bodyCubeEbo_{0};
  static constexpr int kIconSize = 64;
};

} // namespace cutum

#endif

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
  void ClearRenderedIcons();

  GLuint GetSpeciesIcon(const std::string &speciesId);
  GLuint GetSkinIcon(const std::string &skinId);
  void WarmupNext(size_t count);

private:
  bool InitCubeMesh();
  GLuint RenderSolidColorIcon(float r, float g, float b, float a);
  GLuint RenderSpeciesPartsIcon(const std::string &speciesId);
  GLuint GetOrCreateSpeciesIcon(const std::string &speciesId);
  GLuint GetOrCreateSkinIcon(const std::string &skinId);

  std::shared_ptr<UCreatureDefinitionStorage> Species;
  std::shared_ptr<USkinDefinitionStorage> Skins;
  std::shared_ptr<UCreatureTextureStorage> Textures;
  std::shared_ptr<UShaderManager> ShaderManager;
  std::shared_ptr<class UShaderProgram> Shader;

  std::unordered_map<std::string, GLuint> SpeciesCache;
  std::unordered_map<std::string, GLuint> SkinCache;
  std::vector<std::string> WarmupQueue;
  size_t WarmupIndex{0};

  GLuint Fbo{0};
  GLuint ColorTex{0};
  GLuint DepthRbo{0};
  GLuint CubeVao{0};
  GLuint CubeVbo{0};
  GLuint CubeEbo{0};
  GLuint HeadCubeVao{0};
  GLuint HeadCubeVbo{0};
  GLuint HeadCubeEbo{0};
  GLuint BodyCubeVao{0};
  GLuint BodyCubeVbo{0};
  GLuint BodyCubeEbo{0};
  static constexpr int kIconSize = 64;
};

} // namespace cutum

#endif

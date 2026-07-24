#ifndef TEXTURECUBE_H
#define TEXTURECUBE_H

#include <map>
#include <memory>
#include <string>
#include <vector>
typedef unsigned int GLuint;
#include "Render/Textures/TextureBase.h"
#include "World/Math/BlockTypes.h"

namespace cutum
{

class UBlockDefinitionStorage;
struct MergedCubeDesc;

class UTextureCube
{
public:
  UTextureCube();
  UTextureCube(const std::string &Name, size_t type_id,
               const std::vector<std::string> &texture_names);
  UTextureCube(const UTextureCube &) = default;

  UTextureCube &operator=(const UTextureCube &) = default;

  const std::string &GetName() const;
  size_t GetTypeId() const;

  GLuint GetTextureId() const;

  const std::vector<std::string> &GetTextureNames() const;
  size_t GetNumTextureFrames() const;
  void SetNumTextureFrames(size_t count);

  GLuint GetTexture() const;
  void SetTexture(GLuint value);

private:
  std::string Name;
  size_t TypeId;
  GLuint TextureId;
  std::vector<std::string> TextureNames;
  size_t NumTextureFrames;

  GLuint Texture;
};

class UTextureCubeStorage
{
public:
  UTextureCubeStorage(std::shared_ptr<UTextureBaseStorage> base_textures);

  void
  SetBlockDefinitions(std::shared_ptr<UBlockDefinitionStorage> definitions);
  void GenerateCubeTextures();
  void Load(const std::string &textures_path);
  void Clear();
  void BuildFromDescriptors(const std::vector<MergedCubeDesc> &descriptors);
  void PatchDescriptors(const std::vector<MergedCubeDesc> &descriptors);
  void RemoveCubeDescriptors(const std::vector<BlockId> &blockIds);

  const std::map<size_t, UTextureCube> &GetTextures() const;
  size_t GetTypeIdByName(const std::string &Name) const;

private:
  struct CubeAtlasPixels
  {
    int Width{0};
    int Height{0};
    std::vector<unsigned char> Rgba;
  };

  CubeAtlasPixels
  BuildCubeAtlasPixels(const std::string &cube_type_name,
                       const std::vector<std::string> &texture_names,
                       int stripFrameCount) const;
  UTextureCube CreateCubeTexture(const std::string &cube_type_name,
                                 size_t cube_type_id,
                                 const std::vector<std::string> &texture_names,
                                 int stripFrameCount);
  GLuint LoadTexture(const std::string &image_path);

  bool LoadJson(const std::string &file_name, std::string &Name, size_t &Id,
                std::vector<std::string> &textures);

  std::shared_ptr<UTextureBaseStorage> TextureBaseStorageInstance;
  std::shared_ptr<UBlockDefinitionStorage> BlockDefinitions;

  std::map<size_t, UTextureCube> Textures;
  std::map<std::string, size_t> TexturesNames;
};

} // namespace cutum

#endif // TEXTURECUBE_H

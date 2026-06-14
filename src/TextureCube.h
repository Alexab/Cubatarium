#ifndef TEXTURECUBE_H
#define TEXTURECUBE_H

#include <string>
#include <vector>
#include <map>
#include <memory>
typedef unsigned int GLuint;
#include "TextureBase.h"

namespace cutum {

class UBlockDefinitionStorage;

class UTextureCube
{
public:
 UTextureCube();
 UTextureCube(const std::string& name, size_t type_id, const std::vector<std::string>& texture_names);
 UTextureCube(const UTextureCube&) = default;

 UTextureCube& operator = (const UTextureCube&) = default;

 const std::string& GetName() const;
 size_t GetTypeId() const;

 GLuint GetTextureId() const;

 const std::vector<std::string>& GetTextureNames() const;
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

 void SetBlockDefinitions(std::shared_ptr<UBlockDefinitionStorage> definitions);
 void GenerateCubeTextures();
 void Load(const std::string &textures_path);

 const std::map<size_t, UTextureCube>& GetTextures() const;
 size_t GetTypeIdByName(const std::string& name) const;

private:
 UTextureCube CreateCubeTexture(const std::string &cube_type_name, size_t cube_type_id,
                               const std::vector<std::string>& texture_names, int stripFrameCount);
 GLuint LoadTexture(const std::string &image_path);

 bool LoadJson(const std::string& file_name, std::string &name, size_t &id, std::vector<std::string> &textures);

 std::shared_ptr<UTextureBaseStorage> TextureBaseStorageInstance;
 std::shared_ptr<UBlockDefinitionStorage> BlockDefinitions;

 std::map<size_t, UTextureCube> Textures;
 std::map<std::string, size_t> TexturesNames;
};

}

#endif // TEXTURECUBE_H

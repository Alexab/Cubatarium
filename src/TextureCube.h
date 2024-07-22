#ifndef TEXTURECUBE_H
#define TEXTURECUBE_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include "TextureBase.h"
#include <QOpenGLTexture>

namespace cutum {

class TextureCube
{
public:
 TextureCube();
 TextureCube(const std::string& name, size_t type_id, const std::vector<std::string>& texture_names);
 TextureCube(const TextureCube&) = default;

 TextureCube& operator = (const TextureCube&) = default;

 const std::string& GetName() const;
 size_t GetTypeId() const;

 GLuint GetTextureId() const;

 const std::vector<std::string>& GetTextureNames() const;
 size_t GetNumTextureFrames() const;

 std::shared_ptr<QOpenGLTexture> GetTexture() const;
 void SetTexture(std::shared_ptr<QOpenGLTexture> value);

private:
 std::string Name;
 size_t TypeId;
 GLuint TextureId;
 std::vector<std::string> TextureNames;
 size_t NumTextureFrames;

 std::shared_ptr<QOpenGLTexture> Texture;
};

class TextureCubeStorage
{
public:
 TextureCubeStorage(std::shared_ptr<TextureBaseStorage> base_textures);

 void GenerateCubeTextures();
 void Load(const std::string &textures_path);
// void CreateCubeTextures();

 const std::map<size_t, TextureCube>& GetTextures() const;
 size_t GetTypeIdByName(const std::string& name) const;

private:
 TextureCube CreateCubeTexture(const std::string &cube_type_name, size_t cube_type_id, const std::vector<std::string>& texture_names);
 std::shared_ptr<QOpenGLTexture> LoadTexture(const QImage &image);
 std::shared_ptr<QOpenGLTexture> LoadTexture(const std::string &texture_res_name);

 bool LoadJson(const std::string& file_name, std::string &name, size_t &id, std::vector<std::string> &textures);

private:
 std::shared_ptr<TextureBaseStorage> TextureBaseStorageInstance;

 std::map<size_t, TextureCube> Textures;
 std::map<std::string, size_t> TexturesNames;
};

}

#endif // TEXTURECUBE_H

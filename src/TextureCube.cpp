//#include <QPainter>
//#include <QJsonDocument>
//#include <QJsonObject>
//#include <QJsonValue>
//#include <QJsonArray>
//#include <QFile>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
#include <GL/glew.h>
#include "stb_image.h"
#include "TextureCube.h"

using json = nlohmann::json;

namespace cutum {

namespace fs = std::filesystem;

TextureCube::TextureCube()
 : TypeId(0)
 , NumTextureFrames(0)
 , TextureId(0)
 , Texture(0)
{

}

TextureCube::TextureCube(const std::string& name, size_t type_id, const std::vector<std::string>& texture_names)
 : Name(name)
 , TypeId(type_id)
 , TextureNames(texture_names)
 , NumTextureFrames((TextureNames.size()/6)<1?1:TextureNames.size()/6)
 , TextureId(0)
 , Texture(0)
{

}

const std::string& TextureCube::GetName() const
{
 return Name;
}

size_t TextureCube::GetTypeId() const
{
 return TypeId;
}

GLuint TextureCube::GetTextureId() const
{
 return TextureId;
}

const std::vector<std::string>& TextureCube::GetTextureNames() const
{
 return TextureNames;
}

size_t TextureCube::GetNumTextureFrames() const
{
 return NumTextureFrames;
}

GLuint TextureCube::GetTexture() const
{
 return Texture;
}

void TextureCube::SetTexture(GLuint value)
{
 Texture = value;
 TextureId = value;
}

TextureCubeStorage::TextureCubeStorage(std::shared_ptr<TextureBaseStorage> base_textures)
 : TextureBaseStorageInstance(base_textures)
{

}

void TextureCubeStorage::GenerateCubeTextures()
{
}

void TextureCubeStorage::Load(const std::string &textures_path)
{
 try
 {
  for (const auto & entry : fs::directory_iterator(textures_path))
  {
   auto ext = entry.path().extension();
   if(ext.string() == ".json")
   {
    std::string name;
    size_t id;
    std::vector<std::string> textures;
    if(LoadJson(entry.path().string(), name, id, textures))
    {
     TextureCube descr=CreateCubeTexture(name, id, textures);
     Textures[descr.GetTypeId()] = descr;
    }
   }
  }
 }
 catch(std::filesystem::filesystem_error &ex)
 {
  std::cerr << ex.what();
 }
}

const std::map<size_t, TextureCube>& TextureCubeStorage::GetTextures() const
{
 return Textures;
}

size_t TextureCubeStorage::GetTypeIdByName(const std::string& name) const
{
 return TexturesNames.at(name);
}


TextureCube TextureCubeStorage::CreateCubeTexture(const std::string &cube_type_name, size_t cube_type_id, const std::vector<std::string>& texture_names)
{
 const auto & base_texture_descriptions = TextureBaseStorageInstance->GetBaseTextures();
 TextureCube result(cube_type_name, cube_type_id, texture_names);
 int num_texture_frames = int(result.GetNumTextureFrames());
 
 // Создаем OpenGL текстуру
 GLuint textureId;
 glGenTextures(1, &textureId);
 glBindTexture(GL_TEXTURE_2D, textureId);
 
 // Загружаем первую текстуру для получения размеров
 std::string first_texture_path = base_texture_descriptions.at(texture_names[0]).GetFileName();
 int width, height, channels;
 unsigned char* data = stbi_load(first_texture_path.c_str(), &width, &height, &channels, 4);
 if (!data) {
     std::cerr << "Failed to load texture: " << first_texture_path << std::endl;
     return result;
 }
 
 // Создаем буфер для объединенной текстуры
 int total_width = width * 6;
 int total_height = height * num_texture_frames;
 std::vector<unsigned char> combined_data(total_width * total_height * 4);
 
 // Загружаем и объединяем все текстуры
 size_t k = 0;
 for(size_t j = 0; j < num_texture_frames; j++)
 {
  for(size_t i = 0; i < 6; i++)
  {
   if (k < texture_names.size()) {
       std::string texture_path = base_texture_descriptions.at(texture_names[k]).GetFileName();
       int tex_width, tex_height, tex_channels;
       unsigned char* tex_data = stbi_load(texture_path.c_str(), &tex_width, &tex_height, &tex_channels, 4);
       if (tex_data) {
           // Копируем данные текстуры в нужную позицию
           for (int y = 0; y < height; y++) {
               for (int x = 0; x < width; x++) {
                   int src_idx = (y * width + x) * 4;
                   int dst_idx = ((j * height + y) * total_width + (i * width + x)) * 4;
                   combined_data[dst_idx] = tex_data[src_idx];
                   combined_data[dst_idx + 1] = tex_data[src_idx + 1];
                   combined_data[dst_idx + 2] = tex_data[src_idx + 2];
                   combined_data[dst_idx + 3] = tex_data[src_idx + 3];
               }
           }
           stbi_image_free(tex_data);
       }
   }
   ++k;
  }
 }
 
 // Загружаем объединенную текстуру в OpenGL
 glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, total_width, total_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, combined_data.data());
 
 // Устанавливаем параметры текстуры
 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
 
 glBindTexture(GL_TEXTURE_2D, 0);
 
 result.SetTexture(textureId);
 TexturesNames[cube_type_name] = cube_type_id;
 
 stbi_image_free(data);
 return result;
}

GLuint TextureCubeStorage::LoadTexture(const std::string &image_path)
{
 GLuint textureId;
 glGenTextures(1, &textureId);
 glBindTexture(GL_TEXTURE_2D, textureId);
 
 int width, height, channels;
 unsigned char* data = stbi_load(image_path.c_str(), &width, &height, &channels, 4);
 if (!data) {
     std::cerr << "Failed to load texture: " << image_path << std::endl;
     return 0;
 }
 
 glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
 
 glBindTexture(GL_TEXTURE_2D, 0);
 
 stbi_image_free(data);
 return textureId;
}



bool TextureCubeStorage::LoadJson(const std::string& file_name, std::string &name, size_t &id, std::vector<std::string> &textures)
{
 std::string val;
 std::ifstream file(file_name);
 if (file.is_open()) {
     std::stringstream buffer;
     buffer << file.rdbuf();
     val = buffer.str();
     file.close();
     std::cout << val << std::endl;
 } else {
     std::cerr << "Failed to open JSON file: " << file_name << std::endl;
     return false;
 }

 try {
     json d = json::parse(val);
     std::string name_value = d.value("name", "");
     size_t id_value = d.value("id", 0);
     json textures_value = d.value("textures", json::array());

     if(name_value.empty() || id_value == 0 || textures_value.empty())
      return false;

     name = name_value;
     id = id_value;

     if(!textures_value.is_array())
      return false;

     textures.clear();
     for(const auto& texture : textures_value) {
         textures.push_back(texture.get<std::string>());
     }

     return true;
 } catch (const json::exception& e) {
     std::cerr << "JSON parsing error in LoadJson: " << e.what() << std::endl;
     return false;
 }
}

}

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
#include "BlockDefinitionStorage.h"

using json = nlohmann::json;

namespace cutum {

namespace fs = std::filesystem;

UTextureCube::UTextureCube()
 : TypeId(0)
 , NumTextureFrames(0)
 , TextureId(0)
 , Texture(0)
{

}

UTextureCube::UTextureCube(const std::string& name, size_t type_id, const std::vector<std::string>& texture_names)
 : Name(name)
 , TypeId(type_id)
 , TextureNames(texture_names)
 , NumTextureFrames((TextureNames.size()/6)<1?1:TextureNames.size()/6)
 , TextureId(0)
 , Texture(0)
{

}

const std::string& UTextureCube::GetName() const
{
 return Name;
}

size_t UTextureCube::GetTypeId() const
{
 return TypeId;
}

GLuint UTextureCube::GetTextureId() const
{
 return TextureId;
}

const std::vector<std::string>& UTextureCube::GetTextureNames() const
{
 return TextureNames;
}

size_t UTextureCube::GetNumTextureFrames() const
{
 return NumTextureFrames;
}

void UTextureCube::SetNumTextureFrames(size_t count)
{
 NumTextureFrames = count < 1 ? 1 : count;
}

GLuint UTextureCube::GetTexture() const
{
 return Texture;
}

void UTextureCube::SetTexture(GLuint value)
{
 Texture = value;
 TextureId = value;
}

UTextureCubeStorage::UTextureCubeStorage(std::shared_ptr<UTextureBaseStorage> base_textures)
 : TextureBaseStorageInstance(base_textures)
{
}

void UTextureCubeStorage::SetBlockDefinitions(std::shared_ptr<UBlockDefinitionStorage> definitions)
{
 BlockDefinitions = std::move(definitions);
}

void UTextureCubeStorage::GenerateCubeTextures()
{
}

void UTextureCubeStorage::Load(const std::string &textures_path)
{
#ifdef CUBATARIUM_DEBUG
 std::cout << "UTextureCubeStorage::Load: Loading from " << textures_path << std::endl;
#endif
 
 try
 {
  int loaded_count = 0;
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
      int stripFrames = 0;
      if (BlockDefinitions) {
       if (const BlockDefinition* def = BlockDefinitions->GetByName(name)) {
        if (textures.size() == 6 && def->animation.frameCount > 1) {
         stripFrames = def->animation.frameCount;
        }
       }
      }
      UTextureCube descr = CreateCubeTexture(name, id, textures, stripFrames);
      Textures[descr.GetTypeId()] = descr;
      loaded_count++;
#ifdef CUBATARIUM_DEBUG
      std::cout << "UTextureCubeStorage::Load: Added texture '" << name << "'" << std::endl;
#endif
     }
   }
  }

#ifdef CUBATARIUM_DEBUG
  std::cout << "UTextureCubeStorage::Load: Total loaded textures: " << loaded_count << std::endl;
#endif
  }
  catch(std::filesystem::filesystem_error &ex)
  {
   std::cerr << ex.what();
  }
 }

const std::map<size_t, UTextureCube>& UTextureCubeStorage::GetTextures() const
{
 return Textures;
}

size_t UTextureCubeStorage::GetTypeIdByName(const std::string& name) const
{
 const auto it = TexturesNames.find(name);
 if (it != TexturesNames.end()) {
  return it->second;
 }
 return 0;
}


namespace {

void CopyRegion(unsigned char* dst, int dstX, int dstY, int dstStride, int dstTotalW,
                const unsigned char* src, int srcX, int srcY, int srcW, int srcH, int frameW, int frameH)
{
 for (int y = 0; y < frameH; ++y) {
  for (int x = 0; x < frameW; ++x) {
   const int srcIdx = ((srcY + y) * srcW + (srcX + x)) * 4;
   const int dstIdx = ((dstY + y) * dstTotalW + (dstX + x)) * 4;
   dst[dstIdx] = src[srcIdx];
   dst[dstIdx + 1] = src[srcIdx + 1];
   dst[dstIdx + 2] = src[srcIdx + 2];
   dst[dstIdx + 3] = src[srcIdx + 3];
  }
 }
 (void)dstStride;
}

} // namespace

UTextureCube UTextureCubeStorage::CreateCubeTexture(const std::string &cube_type_name, size_t cube_type_id,
                                                    const std::vector<std::string>& texture_names,
                                                    int stripFrameCount)
{
 const auto & base_texture_descriptions = TextureBaseStorageInstance->GetBaseTextures();
 UTextureCube result(cube_type_name, cube_type_id, texture_names);
 const bool useVerticalStrip = stripFrameCount > 1 && texture_names.size() == 6;
 int num_texture_frames = useVerticalStrip ? stripFrameCount : int(result.GetNumTextureFrames());
 if (useVerticalStrip) {
  result.SetNumTextureFrames(static_cast<size_t>(num_texture_frames));
 }
 
     // Create OpenGL texture
 GLuint textureId;
 glGenTextures(1, &textureId);
 glBindTexture(GL_TEXTURE_2D, textureId);
 
     // Load first texture to get dimensions
 const auto firstTexIt = base_texture_descriptions.find(texture_names[0]);
 if (firstTexIt == base_texture_descriptions.end()) {
  std::cerr << "UTextureCubeStorage::CreateCubeTexture: unknown base texture '"
            << texture_names[0] << "' for " << cube_type_name << std::endl;
  return result;
 }
 std::string first_texture_path = firstTexIt->second.GetFileName();
 int width, height, channels;
 int fullWidth = 0;
 int fullHeight = 0;
 unsigned char* data =
     stbi_load(first_texture_path.c_str(), &fullWidth, &fullHeight, &channels, 4);
 if (!data) {
  std::cerr << "Failed to load texture: " << first_texture_path << std::endl;
  return result;
 }
 if (useVerticalStrip) {
  width = fullWidth;
  height = fullHeight / num_texture_frames;
  if (height <= 0) {
   height = fullWidth;
  }
 } else {
  width = fullWidth;
  height = fullHeight;
 }

 int total_width = width * 6;
 int total_height = height * num_texture_frames;
 std::vector<unsigned char> combined_data(total_width * total_height * 4);
 
 if (useVerticalStrip) {
  for (int j = 0; j < num_texture_frames; ++j) {
   for (size_t i = 0; i < 6; ++i) {
    const auto texIt = base_texture_descriptions.find(texture_names[i]);
    if (texIt == base_texture_descriptions.end()) {
     continue;
    }
    const std::string& texture_path = texIt->second.GetFileName();
    int tex_width = 0, tex_height = 0, tex_channels = 0;
    unsigned char* tex_data =
        stbi_load(texture_path.c_str(), &tex_width, &tex_height, &tex_channels, 4);
    if (!tex_data) {
     continue;
    }
    const int frameH = height;
    CopyRegion(combined_data.data(), static_cast<int>(i) * width, j * frameH, total_width,
               total_width, tex_data, 0, j * frameH, tex_width, tex_height, width, frameH);
    stbi_image_free(tex_data);
   }
  }
 } else {
  size_t k = 0;
  for (size_t j = 0; j < static_cast<size_t>(num_texture_frames); j++) {
   for (size_t i = 0; i < 6; i++) {
    if (k < texture_names.size()) {
     const auto texIt = base_texture_descriptions.find(texture_names[k]);
     if (texIt == base_texture_descriptions.end()) {
      std::cerr << "UTextureCubeStorage::CreateCubeTexture: unknown base texture '"
                << texture_names[k] << "'" << std::endl;
      ++k;
      continue;
     }
     std::string texture_path = texIt->second.GetFileName();
     int tex_width = 0, tex_height = 0, tex_channels = 0;
     unsigned char* tex_data =
         stbi_load(texture_path.c_str(), &tex_width, &tex_height, &tex_channels, 4);
     if (tex_data) {
      for (int y = 0; y < height; y++) {
       for (int x = 0; x < width; x++) {
        int src_idx = (y * width + x) * 4;
        int dst_idx = ((static_cast<int>(j) * height + y) * total_width + (static_cast<int>(i) * width + x)) * 4;
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
 }
 
     // Load combined texture into OpenGL
 glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, total_width, total_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, combined_data.data());
 
     // Set texture parameters
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

GLuint UTextureCubeStorage::LoadTexture(const std::string &image_path)
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



bool UTextureCubeStorage::LoadJson(const std::string& file_name, std::string &name, size_t &id, std::vector<std::string> &textures)
{
 std::string val;
 std::ifstream file(file_name);
 if (file.is_open()) {
     std::stringstream buffer;
     buffer << file.rdbuf();
     val = buffer.str();
     file.close();
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

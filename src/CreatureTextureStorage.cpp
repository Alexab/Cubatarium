#include "CreatureTextureStorage.h"

#include <filesystem>
#include <iostream>

#include <stb_image.h>

namespace cutum {

namespace {

GLuint LoadPngFile(const std::string& imagePath)
{
 GLuint textureId = 0;
 glGenTextures(1, &textureId);
 glBindTexture(GL_TEXTURE_2D, textureId);

 int width = 0;
 int height = 0;
 int channels = 0;
 unsigned char* data = stbi_load(imagePath.c_str(), &width, &height, &channels, 4);
 if (!data) {
  std::cerr << "CreatureTextureStorage: failed to load " << imagePath << std::endl;
  glDeleteTextures(1, &textureId);
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

void IndexPngTextures(const std::filesystem::path& texturesDir,
                      const std::string& keyPrefix,
                      std::unordered_map<std::string, GLuint>& out)
{
 if (!std::filesystem::exists(texturesDir)) {
  return;
 }
 for (const auto& entry : std::filesystem::directory_iterator(texturesDir)) {
  if (!entry.is_regular_file() || entry.path().extension() != ".png") {
   continue;
  }
  const std::string stem = entry.path().stem().string();
  const std::string key = keyPrefix + "/" + stem;
  const GLuint tex = LoadPngFile(entry.path().string());
  if (tex != 0) {
   out[key] = tex;
  }
 }
}

} // namespace

void CreatureTextureStorage::LoadFromCreatureAndSkinRoots(const std::string& creaturesRoot,
                                                          const std::string& skinsRoot)
{
 textures_.clear();
 if (!std::filesystem::exists(creaturesRoot)) {
  return;
 }
 for (const auto& entry : std::filesystem::directory_iterator(creaturesRoot)) {
  if (!entry.is_directory()) {
   continue;
  }
  const std::string speciesId = entry.path().filename().string();
  IndexPngTextures(entry.path() / "textures", speciesId, textures_);
 }

 if (std::filesystem::exists(skinsRoot)) {
  for (const auto& entry : std::filesystem::directory_iterator(skinsRoot)) {
   if (!entry.is_directory()) {
    continue;
   }
   const std::string skinId = entry.path().filename().string();
   IndexPngTextures(entry.path() / "textures", "skin/" + skinId, textures_);
  }
 }

 std::cout << "CreatureTextureStorage: loaded " << textures_.size() << " textures" << std::endl;
}

GLuint CreatureTextureStorage::GetTexture(const std::string& assetKey) const
{
 const auto it = textures_.find(assetKey);
 if (it == textures_.end()) {
  return 0;
 }
 return it->second;
}

} // namespace cutum

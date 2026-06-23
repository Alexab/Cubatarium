#ifndef TEXTUREOVERRIDES_H
#define TEXTUREOVERRIDES_H

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

struct TextureFaceOverride
{
  std::vector<int> Faces;
  std::string Stem;
};

using TextureOverrideMap =
    std::unordered_map<std::string, std::vector<TextureFaceOverride>>;

TextureOverrideMap LoadTextureOverrides(const std::filesystem::path &packRoot);

} // namespace cutum

#endif

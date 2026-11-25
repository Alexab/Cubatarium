#ifndef TEXTUREOVERRIDES_H
#define TEXTUREOVERRIDES_H

#include <filesystem>
#include <iosfwd>
#include <nlohmann/json_fwd.hpp>
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
TextureOverrideMap CompileTextureOverridesJson(const nlohmann::json &root);
// Existing supported YAML subset; same streaming/partial-result policy.
TextureOverrideMap ParseTextureOverridesYaml(std::istream &in);

} // namespace cutum

#endif

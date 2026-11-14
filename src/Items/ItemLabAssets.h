#ifndef ITEM_LAB_ASSETS_H
#define ITEM_LAB_ASSETS_H
#include <filesystem>
#include <stdexcept>
#include <string>

namespace cutum
{
struct ItemLabAssets
{
  std::filesystem::path Published, Reference, Candidate;
  bool Archived{false}, Staged{false};
};

inline ItemLabAssets
ResolveItemLabAssets(const std::filesystem::path &assetRoot,
                     const std::filesystem::path &model, const std::string &id)
{
  if (id.empty() ||
      id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") !=
          std::string::npos)
    throw std::invalid_argument("Invalid Item Lab item ID");
  const auto root = std::filesystem::weakly_canonical(assetRoot);
  ItemLabAssets result;
  result.Published = std::filesystem::weakly_canonical(root / model);
  const auto relative = result.Published.lexically_relative(root);
  if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
    throw std::invalid_argument(
        "Item Lab published resource escapes asset root");
  const auto reference =
      result.Published.parent_path() / "_itemgen_reference/model.gltf";
  result.Archived = std::filesystem::is_regular_file(reference);
  result.Reference = result.Archived ? reference : result.Published;
  const auto candidate =
      root.parent_path() / "assetgen_output/items/redesign" / id / "model.gltf";
  result.Staged = std::filesystem::is_regular_file(candidate);
  result.Candidate = result.Staged     ? candidate
                     : result.Archived ? result.Published
                                       : candidate;
  return result;
}
} // namespace cutum
#endif

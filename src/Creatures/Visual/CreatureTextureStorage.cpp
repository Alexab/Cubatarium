#include "Creatures/Visual/CreatureTextureStorage.h"

#include "Core/Memory/VisualAssetStorage.h"
#include "Render/Textures/VisualTextureUpload.h"
#include <iostream>
#include <stdexcept>

namespace cutum
{

UCreatureTextureStorage::~UCreatureTextureStorage()
{
  // Clear while the owning context is current. An escaped storage object may
  // outlive that context, which has already reclaimed its texture names.
  for (auto &entry : Textures)
    entry.second.Name.Release();
}

std::shared_ptr<UCreatureTextureStorage> UCreatureTextureStorage::Create()
{
  auto textures = std::make_shared<UCreatureTextureStorage>();
  textures->FactoryOwner = true;
  return textures;
}

bool UCreatureTextureStorage::IndexRoot(const std::filesystem::path &root, bool skins, TextureTable &out)
{
  if (!std::filesystem::exists(root))
    return true;
  for (const auto &entry : std::filesystem::directory_iterator(root))
  {
    if (!entry.is_directory())
      continue;
    const auto directory = entry.path() / "textures";
    if (!std::filesystem::exists(directory))
      continue;
    auto prefix = entry.path().filename().string();
    if (skins)
      prefix.insert(0, "skin/");
    for (const auto &file : std::filesystem::directory_iterator(directory))
    {
      if (!file.is_regular_file() || file.path().extension() != ".png")
        continue;
      auto image = DecodeVisualTextureFile(file.path(), VisualTextureSampling::Nearest);
      if (image.Retry)
        return false;
      if (image.Image.Name.Get())
        out.insert_or_assign(prefix + "/" + file.path().stem().string(), std::move(image.Image));
      else
        std::cerr << "UCreatureTextureStorage: failed to load " << file.path().string() << '\n';
    }
  }
  return true;
}

bool UCreatureTextureStorage::LoadFromCreatureAndSkinRoots(
    const std::string &creaturesRoot, const std::string &skinsRoot,
    const std::vector<std::filesystem::path> &overlays)
{
  const auto basePresent = std::filesystem::exists(creaturesRoot);
  if (!basePresent && overlays.empty())
  {
    Clear();
    return true;
  }
  TextureTable candidate;
  if (basePresent && (!IndexRoot(creaturesRoot, false, candidate) || !IndexRoot(skinsRoot, true, candidate)))
    return false;
  for (const auto &root : overlays)
    if (!IndexRoot(root / "creatures", false, candidate) || !IndexRoot(root / "skins", true, candidate))
      return false;
  Textures.swap(candidate);
  std::cout << "UCreatureTextureStorage: loaded " << Textures.size() << " textures" << std::endl;
  return true;
}

bool UCreatureTextureStorage::MergeRoot(const std::string &root, bool skins)
{
  if (!std::filesystem::exists(root))
    return true;
  TextureTable candidate;
  if (!IndexRoot(root, skins, candidate))
    return false;

  size_t additions = 0;
  for (const auto &entry : candidate)
    additions += Textures.find(entry.first) == Textures.end();
  if (additions > Textures.max_size() - Textures.size())
    throw std::length_error("Creature texture table capacity");
  if (additions)
    Textures.reserve(Textures.size() + additions);
  while (!candidate.empty())
  {
    auto entry = candidate.extract(candidate.begin());
    const auto previous = Textures.find(entry.key());
    if (previous != Textures.end())
      previous->second = std::move(entry.mapped());
    else
      Textures.insert(std::move(entry));
  }
  return true;
}

bool UCreatureTextureStorage::MergeCreatureRoot(const std::string &creaturesRoot)
{
  return MergeRoot(creaturesRoot, false);
}

bool UCreatureTextureStorage::MergeSkinRoot(const std::string &skinsRoot)
{
  return MergeRoot(skinsRoot, true);
}

void UCreatureTextureStorage::Clear() noexcept
{
  Textures.clear();
}

GLuint UCreatureTextureStorage::GetTexture(const std::string &assetKey) const
{
  const auto it = Textures.find(assetKey);
  return it == Textures.end() ? 0 : it->second.Name.Get();
}

VisualTextureStorage UCreatureTextureStorage::InspectOwnedStorage() const noexcept
{
  VisualTextureStorage out;
  out.present = true;
  out.inlineBytes = sizeof(*this);
  StlSourceStorage table, control, images;
  if (FactoryOwner)
    AddVisualAssetControl<UCreatureTextureStorage>(control);
  table.Hash(Textures);
  for (const auto &[key, image] : Textures)
  {
    table.Text(key);
    images.Add(image.RgbaBytes);
    images.bytes.supported &= image.Name.IsOwned() && image.RgbaBytes > 0;
  }
  out.controlBytes = control.bytes.requested;
  out.tableBytes = table.bytes.requested;
  out.images = Textures.size();
  out.rgbaBytes = images.bytes.requested;
  out.supported = table.bytes.supported && control.bytes.supported && images.bytes.supported;
  return out;
}

} // namespace cutum

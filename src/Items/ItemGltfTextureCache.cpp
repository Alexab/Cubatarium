#include "Items/ItemGltfTextureCache.h"

#include "Core/Memory/StlSourceStorage.h"
#include "Render/Textures/VisualTextureUpload.h"
#include "WorldGen/Core/CatalogJsonSource.h"
#include <atomic>
#include <fstream>

namespace cutum
{
namespace
{

std::atomic<ItemGltfTextureCache *> PublishedCache{nullptr};
constexpr size_t MissingImage = SIZE_MAX;

unsigned char ColorComponent(const nlohmann::json &value)
{
  return static_cast<unsigned char>(std::clamp(value.get<float>(), 0.f, 1.f) * 255.f);
}

} // namespace

ItemGltfTextureCache::ItemGltfTextureCache()
{
  PublishedCache.store(this, std::memory_order_release);
}

ItemGltfTextureCache::~ItemGltfTextureCache()
{
  PublishedCache.store(nullptr, std::memory_order_release);
  // A platform context can already be gone at process-static destruction.
  // The context shutdown path explicitly clears the images while it is current.
  for (auto &model : ByModel)
    for (auto &image : model.second.Images)
      image.Name.Release();
  for (auto &entry : LinearAlbedo)
    entry.second.Name.Release();
}

ItemGltfTextureCache &ItemGltfTextureCache::Instance()
{
  static ItemGltfTextureCache cache;
  return cache;
}

void ItemGltfTextureCache::ClearForContextShutdown()
{
  if (auto *cache = PublishedCache.load(std::memory_order_acquire))
    cache->Clear();
}

bool ItemGltfTextureCache::IndexImageDirectory(const std::filesystem::path &dir, ModelTextures &out)
{
  if (!std::filesystem::is_directory(dir))
    return true;
  for (const auto &entry : std::filesystem::directory_iterator(dir))
  {
    if (!entry.is_regular_file())
      continue;
    const auto ext = entry.path().extension();
    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg")
      continue;
    auto stem = entry.path().stem().string();
    if (out.Aliases.find(stem) != out.Aliases.end())
      continue;
    auto image = DecodeVisualTextureFile(entry.path(), VisualTextureSampling::Linear);
    if (image.Retry)
      return false;
    if (image.Image.Name.Get())
    {
      const auto slot = out.Images.size();
      out.Images.push_back(std::move(image.Image));
      out.Aliases.emplace(std::move(stem), slot);
    }
  }
  return true;
}

bool ItemGltfTextureCache::ReadModelImages(const std::filesystem::path &gltfPath, ModelTextures &out)
{
  std::ifstream in(gltfPath);
  if (!in)
    return true;
  CatalogJsonDocument gltf;
  try
  {
    gltf = ParseAndCloseCatalogFileJson(in, CatalogJsonKind::Definitions);
  }
  catch (const nlohmann::json::exception &)
  {
    return true;
  }
  const auto modelDir = gltfPath.parent_path();

  std::vector<size_t> images;
  if (gltf.contains("images") && gltf["images"].is_array())
  {
    const auto &sources = gltf["images"];
    images.assign(sources.size(), MissingImage);
    for (size_t i = 0; i < sources.size(); ++i)
    {
      const auto &source = sources[i];
      VisualTextureResult loaded;
      if (source.contains("uri") && source["uri"].is_string())
      {
        const auto &uri = source["uri"].get_ref<const std::string &>();
        if (!uri.empty() && uri.rfind("data:", 0) != 0)
          loaded = DecodeVisualTextureFile(modelDir / uri, VisualTextureSampling::Linear);
      }
      else if (source.contains("bufferView") && gltf.contains("bufferViews") && gltf.contains("buffers"))
      {
        const int viewIndex = source["bufferView"].get<int>();
        const auto &views = gltf["bufferViews"];
        const auto &buffers = gltf["buffers"];
        if (viewIndex < 0 || size_t(viewIndex) >= views.size())
          continue;
        const auto &view = views[viewIndex];
        const int bufferIndex = view.value("buffer", 0);
        const int offset = view.value("byteOffset", 0);
        const int length = view.value("byteLength", 0);
        if (bufferIndex < 0 || size_t(bufferIndex) >= buffers.size() || offset < 0 || length <= 0)
          continue;
        const auto uri = buffers[bufferIndex].value("uri", std::string());
        if (uri.empty() || uri.rfind("data:", 0) == 0)
          continue;
        std::ifstream bin(modelDir / uri, std::ios::binary | std::ios::ate);
        if (!bin || bin.tellg() < std::streamoff(offset) + length)
          continue;
        bin.seekg(offset);
        std::vector<stbi_uc> bytes(static_cast<size_t>(length));
        bin.read(reinterpret_cast<char *>(bytes.data()), length);
        if (bin.gcount() == length)
          loaded = DecodeVisualTextureMemory(bytes.data(), length, VisualTextureSampling::Linear);
      }
      if (loaded.Retry)
        return false;
      if (loaded.Image.Name.Get())
      {
        images[i] = out.Images.size();
        out.Images.push_back(std::move(loaded.Image));
      }
    }
  }

  if (!gltf.contains("materials") || !gltf["materials"].is_array())
  {
    for (size_t i = 0; i < images.size(); ++i)
      if (images[i] != MissingImage)
      {
        out.Aliases.insert_or_assign("img_" + std::to_string(i), images[i]);
        out.Aliases.try_emplace("body", images[i]);
      }
    return true;
  }

  size_t matIndex = 0;
  for (const auto &mat : gltf["materials"])
  {
    auto stem = mat.value("name", std::string());
    if (stem.empty())
      stem = matIndex == 0 ? "body" : "__mat_" + std::to_string(matIndex);
    ++matIndex;
    if (!mat.contains("pbrMetallicRoughness"))
      continue;

    const auto &pbr = mat["pbrMetallicRoughness"];
    if (pbr.contains("baseColorTexture") && gltf.contains("textures"))
    {
      const int index = pbr["baseColorTexture"].value("index", -1);
      const auto &textures = gltf["textures"];
      if (index >= 0 && size_t(index) < textures.size())
      {
        const int source = textures[index].value("source", -1);
        if (source >= 0 && size_t(source) < images.size() && images[source] != MissingImage)
        {
          out.Aliases.insert_or_assign(stem, images[source]);
          out.Aliases.try_emplace("body", images[source]);
          continue;
        }
      }
    }

    if (pbr.contains("baseColorFactor") && pbr["baseColorFactor"].is_array() &&
        pbr["baseColorFactor"].size() >= 3)
    {
      const auto &c = pbr["baseColorFactor"];
      const stbi_uc rgba[]{ColorComponent(c[0]), ColorComponent(c[1]), ColorComponent(c[2]),
                          c.size() > 3 ? ColorComponent(c[3]) : stbi_uc(255)};
      auto solid = UploadVisualTexture(rgba, 1, 1, VisualTextureSampling::Nearest);
      if (solid.Retry)
        return false;
      if (solid.Image.Name.Get())
      {
        const auto slot = out.Images.size();
        out.Images.push_back(std::move(solid.Image));
        out.Aliases.insert_or_assign(stem, slot);
        out.Aliases.insert_or_assign(stem + "_factor", slot);
        out.Aliases.try_emplace("body", slot);
      }
    }
  }
  return true;
}

void ItemGltfTextureCache::ModelTextures::RetireUnreferencedImages()
{
  std::vector<size_t> remap(Images.size(), MissingImage);
  for (const auto &alias : Aliases)
    remap[alias.second] = 0;
  const size_t used = std::count(remap.begin(), remap.end(), size_t(0));
  if (used == Images.size())
    return;
  std::vector<VisualTexture> retained;
  retained.reserve(used);
  for (size_t slot = 0; slot < Images.size(); ++slot)
    if (remap[slot] != MissingImage)
    {
      remap[slot] = retained.size();
      retained.push_back(std::move(Images[slot]));
    }
  for (auto &alias : Aliases)
    alias.second = remap[alias.second];
  Images.swap(retained);
}

GLuint ItemGltfTextureCache::Get(const std::filesystem::path &modelGltfAbs, const std::string &textureStem)
{
  std::lock_guard<std::mutex> lock(Mu);
  auto key = modelGltfAbs.lexically_normal().generic_string();
  auto model = ByModel.find(key);
  if (model == ByModel.end())
  {
    ModelTextures candidate;
    if (!ReadModelImages(modelGltfAbs, candidate) ||
        !IndexImageDirectory(modelGltfAbs.parent_path() / "textures", candidate) ||
        !IndexImageDirectory(modelGltfAbs.parent_path(), candidate))
      return 0;
    candidate.RetireUnreferencedImages();
    model = ByModel.try_emplace(std::move(key), std::move(candidate)).first;
  }
  const auto &textures = model->second;
  if (const auto alias = textures.Aliases.find(textureStem); alias != textures.Aliases.end() && !textureStem.empty())
    return textures.Images[alias->second].Name.Get();
  return textures.Aliases.empty() ? 0 : textures.Images[textures.Aliases.begin()->second].Name.Get();
}

GLuint ItemGltfTextureCache::GetLinearAlbedo(const std::filesystem::path &imageAbs)
{
  std::lock_guard<std::mutex> lock(Mu);
  auto spelling = imageAbs.generic_string();
  if (const auto alias = LinearAliases.find(spelling); alias != LinearAliases.end())
  {
    ++LookupCounters.FastHits;
    return alias->second->Name.Get();
  }
  ++LookupCounters.CanonicalResolutions;
  auto key = std::filesystem::weakly_canonical(imageAbs).generic_string();
  if (const auto found = LinearAlbedo.find(key); found != LinearAlbedo.end())
  {
    LinearAliases.emplace(std::move(spelling), &found->second);
    return found->second.Name.Get();
  }

  auto loaded = DecodeVisualTextureFile(imageAbs, VisualTextureSampling::SrgbMipmapped);
  if (!loaded.Image.Name.Get())
    return 0;
  const auto entry = LinearAlbedo.try_emplace(std::move(key), std::move(loaded.Image)).first;
  try
  {
    LinearAliases.emplace(std::move(spelling), &entry->second);
  }
  catch (...)
  {
    LinearAlbedo.erase(entry);
    throw;
  }
  return entry->second.Name.Get();
}

ItemGltfTextureCache::LookupStats ItemGltfTextureCache::GetLookupStats()
{
  std::lock_guard<std::mutex> lock(Mu);
  return LookupCounters;
}

VisualTextureStorage ItemGltfTextureCache::InspectCurrentStorage()
{
  VisualTextureStorage out;
  out.inlineBytes = sizeof(ItemGltfTextureCache) + sizeof(PublishedCache);
  out.supported = StlSourceStorage::SupportedProfile();
  auto *cache = PublishedCache.load(std::memory_order_acquire);
  if (!cache)
    return out;

  std::lock_guard<std::mutex> lock(cache->Mu);
  out.present = true;
  StlSourceStorage table, images;
  const auto recordImage = [&](const VisualTexture &image)
  {
    images.bytes.supported &= image.Name.IsOwned() && image.RgbaBytes > 0;
    images.Add(image.RgbaBytes);
    ++out.images;
  };
  table.Hash(cache->ByModel);
  for (const auto &[key, model] : cache->ByModel)
  {
    table.Text(key);
    table.Hash(model.Aliases);
    table.Buffer(model.Images.capacity(), sizeof(VisualTexture));
    for (const auto &[name, index] : model.Aliases)
    {
      table.Text(name);
      table.bytes.supported &= index < model.Images.size();
    }
    out.aliases += model.Aliases.size();
    for (const auto &image : model.Images)
      recordImage(image);
  }
  table.Hash(cache->LinearAlbedo);
  for (const auto &[key, image] : cache->LinearAlbedo)
  {
    table.Text(key);
    recordImage(image);
  }
  table.Hash(cache->LinearAliases);
  for (const auto &[key, image] : cache->LinearAliases)
  {
    table.Text(key);
    table.bytes.supported &= image && image->Name.IsOwned();
  }
  out.models = cache->ByModel.size();
  out.linearImages = cache->LinearAlbedo.size();
  out.linearAliases = cache->LinearAliases.size();
  out.tableBytes = table.bytes.requested;
  out.rgbaBytes = images.bytes.requested;
  out.supported &= table.bytes.supported && images.bytes.supported;
  return out;
}

ItemGltfTextureCache::MemoryStats ItemGltfTextureCache::InspectMemory(bool itemsOnly)
{
  std::lock_guard<std::mutex> lock(Mu);
  const auto include = [itemsOnly](const std::string &path)
  {
    return !itemsOnly || path.find("/models/items/") != std::string::npos || path.rfind("models/items/", 0) == 0;
  };
  RestoreTextureBinding binding;
  MemoryStats result;
  const auto query = [&](GLuint texture)
  {
    if (!glIsTexture(texture))
    {
      ++result.InvalidTextures;
      return;
    }
    ++result.Textures;
    glBindTexture(GL_TEXTURE_2D, texture);
    std::array<int, 3> dimensions{};
    for (int level = 0; level < 16; ++level)
    {
      GLint width = 0, height = 0;
      glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH, &width);
      glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_HEIGHT, &height);
      if (!width || !height)
        break;
      if (!level)
      {
        dimensions[0] = width;
        dimensions[1] = height;
      }
      ++dimensions[2];
      result.RgbaMipBytes += uint64_t(width) * uint64_t(height) * 4;
    }
    result.Dimensions.push_back(dimensions);
  };
  for (const auto &[key, model] : ByModel)
    if (include(key))
      for (const auto &image : model.Images)
        query(image.Name.Get());
  for (const auto &[key, image] : LinearAlbedo)
    if (include(key))
      query(image.Name.Get());
  return result;
}

void ItemGltfTextureCache::Clear()
{
  std::lock_guard<std::mutex> lock(Mu);
  LinearAliases.clear();
  LinearAlbedo.clear();
  ByModel.clear();
}

} // namespace cutum

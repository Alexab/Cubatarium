// #include <QPainter>
// #include <QJsonDocument>
// #include <QJsonObject>
// #include <QJsonValue>
// #include <QJsonArray>
// #include <QFile>
#include "Core/UiPump.h"
#include "Render/Textures/TextureCube.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Render/GlIncludes.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "ThirdParty/stb_image.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <memory>
#include <limits>
#include <unordered_map>

using json = nlohmann::json;

namespace cutum
{

namespace fs = std::filesystem;

UTextureCube::UTextureCube()
    : TypeId(0), NumTextureFrames(0), TextureId(0), Texture(0)
{
}

UTextureCube::UTextureCube(const std::string &Name, size_t type_id,
                           const std::vector<std::string> &texture_names)
    : Name(Name), TypeId(type_id), TextureNames(texture_names),
      NumTextureFrames((TextureNames.size() / 6) < 1 ? 1
                                                     : TextureNames.size() / 6),
      TextureId(0), Texture(0)
{
}

const std::string &UTextureCube::GetName() const { return Name; }

size_t UTextureCube::GetTypeId() const { return TypeId; }

GLuint UTextureCube::GetTextureId() const { return TextureId; }

const std::vector<std::string> &UTextureCube::GetTextureNames() const
{
  return TextureNames;
}

size_t UTextureCube::GetNumTextureFrames() const { return NumTextureFrames; }

void UTextureCube::SetNumTextureFrames(size_t count)
{
  NumTextureFrames = count < 1 ? 1 : count;
}

GLuint UTextureCube::GetTexture() const { return Texture; }

void UTextureCube::SetTexture(GLuint value)
{
  Texture = value;
  TextureId = value;
}

UTextureCubeStorage::UTextureCubeStorage(
    std::shared_ptr<UTextureBaseStorage> base_textures)
    : TextureBaseStorageInstance(base_textures)
{
}

void UTextureCubeStorage::SetBlockDefinitions(
    std::shared_ptr<UBlockDefinitionStorage> definitions)
{
  BlockDefinitions = std::move(definitions);
}

void UTextureCubeStorage::GenerateCubeTextures() {}

void UTextureCubeStorage::Load(const std::string &textures_path)
{
#ifdef CUBATARIUM_DEBUG
  std::cout << "UTextureCubeStorage::Load: Loading from " << textures_path
            << std::endl;
#endif

  try
  {
    int loaded_count = 0;
    for (const auto &entry : fs::directory_iterator(textures_path))
    {
      auto ext = entry.path().extension();
      if (ext.string() == ".json")
      {
        std::string Name;
        size_t Id;
        std::vector<std::string> textures;
        if (LoadJson(entry.path().string(), Name, Id, textures))
        {
          int stripFrames = 0;
          if (BlockDefinitions)
          {
            if (const BlockDefinition *def = BlockDefinitions->GetByName(Name))
            {
              if (textures.size() == 6 && def->Animation.FrameCount > 1)
              {
                stripFrames = def->Animation.FrameCount;
              }
            }
          }
          UTextureCube descr =
              CreateCubeTexture(Name, Id, textures, stripFrames);
          Textures[descr.GetTypeId()] = descr;
          loaded_count++;
#ifdef CUBATARIUM_DEBUG
          std::cout << "UTextureCubeStorage::Load: Added texture '" << Name
                    << "'" << std::endl;
#endif
        }
      }
    }

#ifdef CUBATARIUM_DEBUG
    std::cout << "UTextureCubeStorage::Load: Total loaded textures: "
              << loaded_count << std::endl;
#endif
  }
  catch (std::filesystem::filesystem_error &ex)
  {
    std::cerr << ex.what();
  }
}

const std::map<size_t, UTextureCube> &UTextureCubeStorage::GetTextures() const
{
  return Textures;
}

size_t UTextureCubeStorage::GetTypeIdByName(const std::string &Name) const
{
  const auto it = TexturesNames.find(Name);
  if (it != TexturesNames.end())
  {
    return it->second;
  }
  return 0;
}

void UTextureCubeStorage::Clear()
{
  for (auto &entry : Textures)
  {
    const GLuint tex = entry.second.GetTexture();
    if (tex != 0)
    {
      glDeleteTextures(1, &tex);
    }
  }
  Textures.clear();
  TexturesNames.clear();
}

namespace
{

/// Read-only pixel span; keeps the source alive through `Owner` when decoded.
struct PixelView
{
  const unsigned char *Ptr{nullptr};
  size_t Count{0};
  const unsigned char *data() const { return Ptr; }
  size_t size() const { return Count; }
  bool empty() const { return Count == 0; }
  unsigned char operator[](size_t i) const { return Ptr[i]; }
};

struct LoadedStem
{
  int Width{0};
  int Height{0};
  PixelView Rgba;
  std::shared_ptr<const unsigned char> Owner;
  bool Ok{false};
};

/// While a batch (PatchDescriptors) is running, decoded stem files are shared
/// between cubes instead of being re-read and re-decoded for every use.
std::unordered_map<std::string, LoadedStem> *gStemBatchCache = nullptr;

bool RgbaByteCount(int width, int height, size_t &bytes)
{
  if (width <= 0 || height <= 0 ||
      static_cast<size_t>(width) > std::numeric_limits<size_t>::max() / 4 / static_cast<size_t>(height))
  {
    return false;
  }
  bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
  return true;
}

LoadedStem LoadStemRgba(const UTextureBase &base)
{
  LoadedStem out;
  if (base.HasPixelData())
  {
    if (const TexturePixelData *px = base.GetPixelData())
    {
      out.Width = px->Width;
      out.Height = px->Height;
      size_t bytes = 0;
      if (RgbaByteCount(out.Width, out.Height, bytes) && px->Rgba.size() >= bytes)
      {
        out.Rgba = PixelView{px->Rgba.data(), bytes};
        out.Ok = true;
      }
      return out;
    }
  }
  if (base.GetFileName().empty())
  {
    return out;
  }
  if (gStemBatchCache)
  {
    const auto hit = gStemBatchCache->find(base.GetFileName());
    if (hit != gStemBatchCache->end())
    {
      return hit->second;
    }
  }
  int channels = 0;
  std::unique_ptr<unsigned char, decltype(&stbi_image_free)> data(
      stbi_load(base.GetFileName().c_str(), &out.Width, &out.Height, &channels, 4),
      &stbi_image_free);
  if (!data)
  {
    if (gStemBatchCache)
    {
      (*gStemBatchCache)[base.GetFileName()] = out; // remember the failure
    }
    return out;
  }
  size_t bytes = 0;
  if (!RgbaByteCount(out.Width, out.Height, bytes))
  {
    return out;
  }
  // Adopt the normalized decoder output. No second RGBA allocation/copy;
  // the unique owner also covers failure to allocate the shared control node.
  out.Owner = std::shared_ptr<const unsigned char>(std::move(data));
  out.Rgba = PixelView{out.Owner.get(), bytes};
  out.Ok = true;
  if (gStemBatchCache)
  {
    (*gStemBatchCache)[base.GetFileName()] = out;
  }
  return out;
}

} // namespace

void UTextureCubeStorage::BuildFromDescriptors(
    const std::vector<MergedCubeDesc> &descriptors)
{
  Clear();
  PatchDescriptors(descriptors);
}

void UTextureCubeStorage::PatchDescriptors(
    const std::vector<MergedCubeDesc> &descriptors)
{
  std::unordered_map<std::string, LoadedStem> batch_cache;
  struct BatchScope
  {
    explicit BatchScope(std::unordered_map<std::string, LoadedStem> *cache)
        : Previous(gStemBatchCache)
    {
      gStemBatchCache = cache;
    }
    ~BatchScope() { gStemBatchCache = Previous; }
    std::unordered_map<std::string, LoadedStem> *Previous;
  } batch_scope(&batch_cache);

  size_t processed = 0;
  for (const MergedCubeDesc &desc : descriptors)
  {
    UBlockingWorkPump::Pump("Building block textures",
                            static_cast<float>(processed++) /
                                static_cast<float>(descriptors.size()));
    const size_t typeId = static_cast<size_t>(desc.Id);
    std::vector<std::string> stems(desc.Stems.begin(), desc.Stems.end());
    int stripFrames = 0;
    if (BlockDefinitions)
    {
      if (const BlockDefinition *def = BlockDefinitions->GetById(desc.Id))
      {
        if (stems.size() == 6 && def->Animation.FrameCount > 1)
        {
          stripFrames = def->Animation.FrameCount;
        }
      }
    }
    else if (desc.AnimFrames > 1 && stems.size() == 6)
    {
      stripFrames = desc.AnimFrames;
    }

    const auto existing = Textures.find(typeId);
    if (existing == Textures.end())
    {
      // Fresh cube: build it once. (An atlas used to be composed here only to
      // test for emptiness and then composed again inside CreateCubeTexture.)
      UTextureCube cube =
          CreateCubeTexture(desc.Name, typeId, stems, stripFrames);
      if (cube.GetTexture() != 0)
      {
        Textures[cube.GetTypeId()] = cube;
      }
      continue;
    }

    const CubeAtlasPixels atlas =
        BuildCubeAtlasPixels(desc.Name, stems, stripFrames);
    if (atlas.Rgba.empty() || atlas.Width <= 0 || atlas.Height <= 0)
    {
      continue;
    }

    {
      GLuint tex = existing->second.GetTexture();
      if (tex != 0)
      {
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
        // GLES build keeps this path simple and portable: recreate texture.
        glDeleteTextures(1, &tex);
#else
        glBindTexture(GL_TEXTURE_2D, tex);
        GLint existing_w = 0;
        GLint existing_h = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH,
                                 &existing_w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT,
                                 &existing_h);
        if (existing_w == atlas.Width && existing_h == atlas.Height)
        {
          glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, atlas.Width, atlas.Height,
                          GL_RGBA, GL_UNSIGNED_BYTE, atlas.Rgba.data());
          glBindTexture(GL_TEXTURE_2D, 0);
          UTextureCube cube(desc.Name, typeId, stems);
          cube.SetNumTextureFrames(static_cast<size_t>(atlas.Frames));
          cube.SetTexture(tex);
          Textures[typeId] = cube;
          TexturesNames[desc.Name] = typeId;
          continue;
        }
        glDeleteTextures(1, &tex);
#endif
      }
      TexturesNames.erase(existing->second.GetName());
      Textures.erase(existing);
    }

    UTextureCube cube = UploadCubeAtlas(desc.Name, typeId, stems, atlas);
    if (cube.GetTexture() != 0)
    {
      Textures[cube.GetTypeId()] = cube;
    }
  }
}

void UTextureCubeStorage::RemoveCubeDescriptors(
    const std::vector<BlockId> &blockIds)
{
  for (const BlockId blockId : blockIds)
  {
    const size_t typeId = static_cast<size_t>(blockId);
    const auto it = Textures.find(typeId);
    if (it == Textures.end())
    {
      continue;
    }
    const GLuint tex = it->second.GetTexture();
    if (tex != 0)
    {
      glDeleteTextures(1, &tex);
    }
    TexturesNames.erase(it->second.GetName());
    Textures.erase(it);
  }
}

UTextureCubeStorage::CubeAtlasPixels UTextureCubeStorage::BuildCubeAtlasPixels(
    const std::string &cube_type_name,
    const std::vector<std::string> &texture_names, int stripFrameCount) const
{
  CubeAtlasPixels atlas;
  if (!TextureBaseStorageInstance || texture_names.empty())
  {
    return atlas;
  }
  const auto &bases = TextureBaseStorageInstance->GetBaseTextures();
  const auto first = bases.find(texture_names.front());
  if (first == bases.end())
  {
    std::cerr << "UTextureCubeStorage::BuildCubeAtlasPixels: unknown base texture '"
              << texture_names.front() << "' for " << cube_type_name << std::endl;
    return atlas;
  }
  const LoadedStem firstLoaded = LoadStemRgba(first->second);
  if (!firstLoaded.Ok)
  {
    return atlas;
  }
  const bool vertical = stripFrameCount > 1 && texture_names.size() == 6;
  const size_t frames = vertical ? static_cast<size_t>(stripFrameCount)
                                : std::max(size_t{1}, texture_names.size() / 6);
  if (frames > static_cast<size_t>(std::numeric_limits<int>::max()))
  {
    return atlas;
  }
  const int width = firstLoaded.Width;
  const int height = vertical ? firstLoaded.Height / static_cast<int>(frames)
                              : firstLoaded.Height;
  // GL dimensions are signed ints. Check their products before conversion;
  // pixel counts and offsets below use size_t throughout.
  if (height <= 0 || width > std::numeric_limits<int>::max() / 6 ||
      static_cast<size_t>(height) > static_cast<size_t>(std::numeric_limits<int>::max()) / frames)
  {
    return atlas;
  }
  const int totalWidth = width * 6;
  const int totalHeight = height * static_cast<int>(frames);
  size_t bytes = 0;
  if (!RgbaByteCount(totalWidth, totalHeight, bytes) || bytes > atlas.Rgba.max_size())
  {
    return atlas;
  }
  atlas.Width = totalWidth;
  atlas.Height = totalHeight;
  atlas.Frames = static_cast<int>(frames);
  atlas.Rgba.assign(bytes, 0);
  for (size_t frame = 0; frame < frames; ++frame)
  {
    for (size_t face = 0; face < 6; ++face)
    {
      const size_t index = vertical ? face : frame * 6 + face;
      if (index >= texture_names.size())
      {
        break;
      }
      const auto source = bases.find(texture_names[index]);
      if (source == bases.end())
      {
        continue;
      }
      // Repeated first faces share its decoded owner even outside a batch.
      const LoadedStem loaded = source == first ? firstLoaded : LoadStemRgba(source->second);
      if (!loaded.Ok)
      {
        continue;
      }
      const size_t sourceY = vertical ? frame * static_cast<size_t>(height) : 0;
      if (sourceY >= static_cast<size_t>(loaded.Height))
      {
        continue;
      }
      const size_t copyWidth = std::min(static_cast<size_t>(width), static_cast<size_t>(loaded.Width));
      const size_t copyHeight = std::min(static_cast<size_t>(height), static_cast<size_t>(loaded.Height) - sourceY);
      for (size_t y = 0; y < copyHeight; ++y)
      {
        const size_t sourceOffset = ((sourceY + y) * static_cast<size_t>(loaded.Width)) * 4;
        const size_t destinationOffset = ((frame * height + y) * totalWidth + face * width) * 4;
        std::memcpy(atlas.Rgba.data() + destinationOffset,
                    loaded.Rgba.data() + sourceOffset, copyWidth * 4);
      }
    }
  }
  return atlas;
}

UTextureCube UTextureCubeStorage::CreateCubeTexture(
    const std::string &cube_type_name, size_t cube_type_id,
    const std::vector<std::string> &texture_names, int stripFrameCount)
{
  const auto atlas = BuildCubeAtlasPixels(cube_type_name, texture_names, stripFrameCount);
  return UploadCubeAtlas(cube_type_name, cube_type_id, texture_names, atlas);
}

UTextureCube UTextureCubeStorage::UploadCubeAtlas(
    const std::string &cube_type_name, size_t cube_type_id,
    const std::vector<std::string> &texture_names, const CubeAtlasPixels &atlas)
{
  UTextureCube result(cube_type_name, cube_type_id, texture_names);
  if (atlas.Rgba.empty())
  {
    return result;
  }
  result.SetNumTextureFrames(static_cast<size_t>(atlas.Frames));
  GLuint textureId;
  glGenTextures(1, &textureId);
  glBindTexture(GL_TEXTURE_2D, textureId);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas.Width, atlas.Height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, atlas.Rgba.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glBindTexture(GL_TEXTURE_2D, 0);
  result.SetTexture(textureId);
  TexturesNames[cube_type_name] = cube_type_id;
  return result;
}

GLuint UTextureCubeStorage::LoadTexture(const std::string &image_path)
{
  GLuint textureId;
  glGenTextures(1, &textureId);
  glBindTexture(GL_TEXTURE_2D, textureId);

  int width, height, channels;
  unsigned char *data =
      stbi_load(image_path.c_str(), &width, &height, &channels, 4);
  if (!data)
  {
    std::cerr << "Failed to load texture: " << image_path << std::endl;
    return 0;
  }

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

  glBindTexture(GL_TEXTURE_2D, 0);

  stbi_image_free(data);
  return textureId;
}

bool UTextureCubeStorage::LoadJson(const std::string &file_name,
                                   std::string &Name, size_t &Id,
                                   std::vector<std::string> &textures)
{
  std::ifstream file(file_name);
  if (!file.is_open())
  {
    std::cerr << "Failed to open JSON file: " << file_name << std::endl;
    return false;
  }

  try
  {
    const json d = json::parse(file);
    std::string name_value = d.value("name", "");
    size_t id_value = d.value("id", 0);
    const auto textures_member = d.find("textures");

    if (name_value.empty() || id_value == 0 || textures_member == d.end() || textures_member->empty())
      return false;

    Name = name_value;
    Id = id_value;

    if (!textures_member->is_array())
      return false;

    textures.clear();
    for (const auto &texture : *textures_member)
    {
      textures.push_back(texture.get<std::string>());
    }

    return true;
  }
  catch (const json::exception &e)
  {
    std::cerr << "JSON parsing error in LoadJson: " << e.what() << std::endl;
    return false;
  }
}

} // namespace cutum

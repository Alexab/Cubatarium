#include "WorldGen/Core/CatalogJsonSource.h"
// #include <QPainter>
// #include <QJsonDocument>
// #include <QJsonObject>
// #include <QJsonValue>
// #include <QJsonArray>
// #include <QFile>
#include "Core/UiPump.h"
#include "Core/Memory/TexturePublicationObservation.h"
#include "Core/Memory/StbiAllocation.h"
#include "Render/Textures/TextureCube.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Render/GlIncludes.h"
#include "Render/Engine/RenderImageStorage.h"
#include "Render/Textures/GlTextureOwner.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "ThirdParty/stb_image.h"
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <memory>
#include <limits>
#include <stdexcept>
#include <unordered_map>

using json = nlohmann::json;

namespace cutum
{

namespace fs = std::filesystem;

namespace
{

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

#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
class AtlasPixelPackScope
{
public:
  AtlasPixelPackScope()
  {
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &Buffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    for (size_t i = 0; i < Parameters.size(); ++i)
    {
      glGetIntegerv(Parameters[i], &Values[i]);
      glPixelStorei(Parameters[i], i == 0 ? 1 : 0);
    }
  }
  AtlasPixelPackScope(const AtlasPixelPackScope &) = delete;
  AtlasPixelPackScope &operator=(const AtlasPixelPackScope &) = delete;
  ~AtlasPixelPackScope()
  {
    glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(Buffer));
    for (size_t i = 0; i < Parameters.size(); ++i)
      glPixelStorei(Parameters[i], Values[i]);
  }

private:
  static constexpr std::array<GLenum, 6> Parameters{
      GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS,
      GL_PACK_IMAGE_HEIGHT, GL_PACK_SKIP_IMAGES};
  std::array<GLint, Parameters.size()> Values{};
  GLint Buffer{};
};
#endif

} // namespace

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

void UTextureCubeStorage::InspectRenderImages(RenderImageCensus &out) const
{
  for (const auto &entry : Textures)
    out.Add(RenderImageRole::BlockAtlas, GL_TEXTURE_2D, entry.second.GetTexture());
}

// Prepared maps borrow existing GL names until publication. Newly created
// names belong to Pending until the no-allocation map swap commits ownership.
struct UTextureCubeStorage::TexturePublication
{
  struct Pending
  {
    UTextureCube *Target{};
    int Width{},Height{},Frames{},StripFrames{};
    GLuint NewName{},ReuseName{};
    size_t PreviousPixelsOffset{};
    bool AllowReuse{}, UpdateAttempted{};
    ~Pending(){if(NewName)glDeleteTextures(1,&NewName);}
  };
  UTextureCubeStorage &Storage;
  bool Replace;
  bool HasRemovals{};
  std::shared_ptr<const BlockDefinitionCatalog> Definitions;
  std::map<size_t,UTextureCube> NextTextures;
  std::map<std::string,size_t> NextNames;
  std::map<size_t,Pending> Rows;
  AtlasDecodeBatch Batch;
  CubeAtlasPixels Scratch;

  TexturePublication(UTextureCubeStorage &storage,bool replace)
      :Storage(storage),Replace(replace),Definitions(storage.BlockDefinitions?storage.BlockDefinitions->GetCatalogSnapshot():nullptr)
  {
    if(!Replace){NextTextures=storage.Textures;NextNames=storage.TexturesNames;}
  }
  int StripById(BlockId id,int fallback)const
  {
    if(!Definitions)return fallback;
    const auto value=Definitions->ById.find(id);
    return value!=Definitions->ById.end() && value->second.Animation.FrameCount>1?value->second.Animation.FrameCount:0;
  }
  int StripByName(const std::string &name)const
  {
    if(!Definitions)return 0;
    const auto id=Definitions->NameToId.find(name);return id==Definitions->NameToId.end()?0:StripById(id->second,0);
  }
  void Remove(BlockId id)
  {
    const auto target=NextTextures.find(static_cast<size_t>(id));if(target==NextTextures.end())return;
    const auto alias=NextNames.find(target->second.GetName());if(alias!=NextNames.end() && alias->second==id)NextNames.erase(alias);
    NextTextures.erase(target);HasRemovals=true;
  }
  void Prepare(const std::string &name,size_t id,const std::vector<std::string> &stems,int strip)
  {
    CubeAtlasPixels layout;
    const auto failure=GetStbiThreadFailureGeneration();
    const bool valid=Storage.BuildCubeAtlasPixelsInto(layout,name,stems,strip,&Batch,false);
    // A malformed/missing source keeps legacy skip/transparent behavior. An
    // allocator refusal must reject publication,including a secondary face.
    if(failure!=GetStbiThreadFailureGeneration())throw std::bad_alloc{};
    if(!valid)return;
    UTextureCube cube(name,id,stems);cube.SetNumTextureFrames(static_cast<size_t>(layout.Frames));
    const auto previous=NextTextures.find(id);
    if(previous!=NextTextures.end() && previous->second.GetName()!=name){
      const auto alias=NextNames.find(previous->second.GetName());
      if(alias!=NextNames.end() && alias->second==id)NextNames.erase(alias);
    }
    NextNames.insert_or_assign(name,id);
    const bool existed=previous!=NextTextures.end();
    auto target=NextTextures.insert_or_assign(id,cube).first;
    const auto pending=Rows.try_emplace(id);auto &row=pending.first->second;
    if(pending.second)row.AllowReuse=existed;
    row.Target=&target->second;row.Width=layout.Width;row.Height=layout.Height;row.Frames=layout.Frames;row.StripFrames=strip;
  }
  void Fill(const Pending &row)
  {
    const bool ok=Storage.BuildCubeAtlasPixelsInto(Scratch,row.Target->GetName(),row.Target->GetTextureNames(),row.StripFrames,&Batch,true);
    if(!ok || Scratch.Width!=row.Width || Scratch.Height!=row.Height || Scratch.Frames!=row.Frames)
      throw std::logic_error("Texture sources changed during publication");
  }
  static bool WriteReusedAtlas(const Pending &row, const unsigned char *pixels, bool observe) noexcept
  {
    glBindTexture(GL_TEXTURE_2D, row.ReuseName);
    if (observe)
      ObserveTextureMutation(row.ReuseName);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, row.Width, row.Height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (glGetError() == GL_NO_ERROR)
      return true;
    while (glGetError() != GL_NO_ERROR) {}
    return false;
  }
  void Publish()
  {
    if(Rows.empty() && !Replace && !HasRemovals)return;
    size_t maximum=0, previousBytes=0;
    if(!Rows.empty()){
      if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Pending GL error before texture publication");
      GLint limit=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);
      for(auto &entry:Rows){auto &row=entry.second;
        if(row.Width>limit || row.Height>limit)throw std::runtime_error("Texture atlas exceeds backend dimensions");
        size_t bytes{};
        if (!RgbaByteCount(row.Width, row.Height, bytes))
          throw std::length_error("Invalid RGBA extent for a texture atlas");
        maximum=std::max(maximum,bytes);
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
        const auto current=Storage.Textures.find(entry.first);
        if(!Replace && row.AllowReuse && current!=Storage.Textures.end() && current->second.GetTexture()){
          const auto name=current->second.GetTexture();glBindTexture(GL_TEXTURE_2D,name);
          GLint w=0,h=0;glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);glBindTexture(GL_TEXTURE_2D,0);
          if (w == row.Width && h == row.Height)
          {
            if (bytes > std::numeric_limits<size_t>::max() - previousBytes)
              throw std::length_error("Texture atlas rollback extent exceeds addressable size");
            row.ReuseName = name;
            row.PreviousPixelsOffset = previousBytes;
            previousBytes += bytes;
          }
        }
#endif
      }
      if (glGetError() != GL_NO_ERROR)
        throw std::runtime_error("GL texture publication preflight failed");

      // One upload scratch serves every row. Capture all reusable images before
      // any upload so rollback needs no allocation or decoding.
      Scratch.Rgba.resize(maximum);
      std::vector<unsigned char> previousPixels(previousBytes);
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
      if (!previousPixels.empty())
      {
        const AtlasPixelPackScope pack;
        for (const auto &[id, row] : Rows)
        {
          if (!row.ReuseName)
            continue;
          glBindTexture(GL_TEXTURE_2D, row.ReuseName);
          glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, previousPixels.data() + row.PreviousPixelsOffset);
          glBindTexture(GL_TEXTURE_2D, 0);
          if (glGetError() != GL_NO_ERROR)
            throw std::runtime_error("GL texture rollback readback failed");
        }
      }
#endif
      const UGlPixelUnpackScope unpack;
      for(auto &entry:Rows){auto &row=entry.second;if(!row.ReuseName){Fill(row);row.NewName=Storage.UploadCubeAtlas(Scratch);row.Target->SetTexture(row.NewName);}}
      try
      {
        for (auto &[id, row] : Rows)
        {
          if (!row.ReuseName)
            continue;
          Fill(row);
          row.UpdateAttempted = true;
          if (!WriteReusedAtlas(row, Scratch.Rgba.data(), true))
            throw std::runtime_error("GL texture update failed");
          row.Target->SetTexture(row.ReuseName);
        }
      }
      catch (...)
      {
        bool restored = true;
        for (const auto &[id, row] : Rows)
          if (row.UpdateAttempted)
            restored &= WriteReusedAtlas(row, previousPixels.data() + row.PreviousPixelsOffset, false);
        if (!restored)
          throw std::runtime_error("GL texture update and rollback failed");
        throw;
      }
    }
    static_assert(noexcept(Storage.Textures.swap(NextTextures)));
    static_assert(noexcept(Storage.TexturesNames.swap(NextNames)));
    Storage.Textures.swap(NextTextures);Storage.TexturesNames.swap(NextNames);
    for(auto &entry:Rows)entry.second.NewName=0;
    for(const auto &old:NextTextures){const auto name=old.second.GetTexture();const auto current=Storage.Textures.find(old.first);
      if(name && (current==Storage.Textures.end() || current->second.GetTexture()!=name))glDeleteTextures(1,&name);
    }
  }
};

UTextureCubeStorage::PublicationSourceLayout UTextureCubeStorage::GetPublicationSourceLayout()
{
  PublicationSourceLayout out;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  out.supported=true;out.stage_bytes=sizeof(TexturePublication);
  out.row_node_bytes=sizeof(std::_Tree_node<decltype(TexturePublication::Rows)::value_type,void*>);
  out.cube_node_bytes=sizeof(std::_Tree_node<decltype(Textures)::value_type,void*>);
  out.name_node_bytes=sizeof(std::_Tree_node<decltype(TexturesNames)::value_type,void*>);
#endif
  return out;
}

void UTextureCubeStorage::Load(const std::string &textures_path)
{
  TexturePublication prepared(*this,false);
  try{
    for(const auto &entry:fs::directory_iterator(textures_path)){
      if(entry.path().extension().string()!=".json")continue;
      UBlockingWorkPump::Pump("Building block textures");
      auto value=LoadLegacyCubeDescriptorFile(entry.path().string());
      if(value){const int strip=value->Textures.size()==6?prepared.StripByName(value->Name):0;prepared.Prepare(value->Name,value->Id,value->Textures,strip);}
    }
    prepared.Publish();
  }catch(const fs::filesystem_error &error){std::cerr<<error.what();}
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

UTextureCubeStorage::LoadedStem UTextureCubeStorage::LoadStemRgba(
    const UTextureBase &base,AtlasDecodeBatch *batch)
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
  if (batch)
  {
    const auto hit = batch->Stems.find(base.GetFileName());
    if (hit != batch->Stems.end())
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
    if (batch)
    {
      batch->Stems[base.GetFileName()] = out; // remember the failure
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
  if (batch)
  {
    batch->Stems[base.GetFileName()] = out;
  }
  return out;
}

UTextureCubeStorage::AtlasDecodeSourceLayout UTextureCubeStorage::GetAtlasDecodeSourceLayout()
{
  AtlasDecodeSourceLayout out;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  out.supported=true;
  out.cache_node_bytes=sizeof(std::_List_node<decltype(AtlasDecodeBatch::Stems)::value_type,void*>);
  static_assert(sizeof(std::_Ref_count_resource<unsigned char *,decltype(&stbi_image_free)>)<=64);
  out.control_request_upper=64;
#endif
  return out;
}

void UTextureCubeStorage::BuildFromDescriptors(const std::vector<MergedCubeDesc> &descriptors)
{
  if(descriptors.empty()){Clear();return;}
  PublishDescriptors(descriptors,true);
}
void UTextureCubeStorage::PatchDescriptors(const std::vector<MergedCubeDesc> &descriptors)
{
  if(descriptors.empty())return;
  PublishDescriptors(descriptors,false);
}
void UTextureCubeStorage::PatchDescriptors(const std::vector<MergedCubeDesc> &descriptors,const std::vector<BlockId> &removed)
{
  if(descriptors.empty()){RemoveCubeDescriptors(removed);return;}
  PublishDescriptors(descriptors,false,&removed);
}
void UTextureCubeStorage::PublishDescriptors(const std::vector<MergedCubeDesc> &descriptors,bool replace,const std::vector<BlockId> *removed)
{
  TexturePublication prepared(*this,replace);size_t processed=0;
  if(removed)for(const auto id:*removed)prepared.Remove(id);
  for(const auto &desc:descriptors){
    UBlockingWorkPump::Pump("Building block textures",static_cast<float>(processed++)/static_cast<float>(descriptors.size()));
    std::vector<std::string> stems(desc.Stems.begin(),desc.Stems.end());
    const int strip=prepared.StripById(desc.Id,desc.AnimFrames>1?desc.AnimFrames:0);
    prepared.Prepare(desc.Name,static_cast<size_t>(desc.Id),stems,strip);
  }
  prepared.Publish();
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
    const auto alias=TexturesNames.find(it->second.GetName());
    if(alias!=TexturesNames.end() && alias->second==typeId)TexturesNames.erase(alias);
    Textures.erase(it);
  }
}

UTextureCubeStorage::CubeAtlasPixels UTextureCubeStorage::BuildCubeAtlasPixels(
    const std::string &cube_type_name,
    const std::vector<std::string> &texture_names, int stripFrameCount) const
{
  return BuildCubeAtlasPixelsImpl(cube_type_name,texture_names,stripFrameCount,nullptr);
}

UTextureCubeStorage::CubeAtlasPixels UTextureCubeStorage::BuildCubeAtlasPixels(
    const std::string &cube_type_name,const std::vector<std::string> &texture_names,
    int stripFrameCount,AtlasDecodeBatch &batch) const
{
  return BuildCubeAtlasPixelsImpl(cube_type_name,texture_names,stripFrameCount,&batch);
}

UTextureCubeStorage::CubeAtlasPixels UTextureCubeStorage::BuildCubeAtlasPixelsImpl(
    const std::string &cube_type_name,const std::vector<std::string> &texture_names,
    int stripFrameCount,AtlasDecodeBatch *batch) const
{
  CubeAtlasPixels atlas;BuildCubeAtlasPixelsInto(atlas,cube_type_name,texture_names,stripFrameCount,batch,true);return atlas;
}

bool UTextureCubeStorage::BuildCubeAtlasPixelsInto(CubeAtlasPixels &atlas,
    const std::string &cube_type_name,const std::vector<std::string> &texture_names,
    int stripFrameCount,AtlasDecodeBatch *batch,bool fill) const
{
  atlas.Width=atlas.Height=atlas.Frames=0;atlas.Rgba.clear();
  if (!TextureBaseStorageInstance || texture_names.empty())
  {
    return false;
  }
  const auto &bases = TextureBaseStorageInstance->GetBaseTextures();
  const auto first = bases.find(texture_names.front());
  if (first == bases.end())
  {
    std::cerr << "UTextureCubeStorage::BuildCubeAtlasPixels: unknown base texture '"
              << texture_names.front() << "' for " << cube_type_name << std::endl;
    return false;
  }
  const LoadedStem firstLoaded = LoadStemRgba(first->second,batch);
  if (!firstLoaded.Ok)
  {
    return false;
  }
  const bool vertical = stripFrameCount > 1 && texture_names.size() == 6;
  const size_t frames = vertical ? static_cast<size_t>(stripFrameCount)
                                : std::max(size_t{1}, texture_names.size() / 6);
  if (frames > static_cast<size_t>(std::numeric_limits<int>::max()))
  {
    return false;
  }
  const int width = firstLoaded.Width;
  const int height = vertical ? firstLoaded.Height / static_cast<int>(frames)
                              : firstLoaded.Height;
  // GL dimensions are signed ints. Check their products before conversion;
  // pixel counts and offsets below use size_t throughout.
  if (height <= 0 || width > std::numeric_limits<int>::max() / 6 ||
      static_cast<size_t>(height) > static_cast<size_t>(std::numeric_limits<int>::max()) / frames)
  {
    return false;
  }
  const int totalWidth = width * 6;
  const int totalHeight = height * static_cast<int>(frames);
  size_t bytes = 0;
  if (!RgbaByteCount(totalWidth, totalHeight, bytes) || bytes > atlas.Rgba.max_size())
  {
    return false;
  }
  atlas.Width = totalWidth;
  atlas.Height = totalHeight;
  atlas.Frames = static_cast<int>(frames);
  if(fill)atlas.Rgba.assign(bytes, 0);
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
      const LoadedStem loaded = source == first ? firstLoaded : LoadStemRgba(source->second,batch);
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
      if(fill)for (size_t y = 0; y < copyHeight; ++y)
      {
        const size_t sourceOffset = ((sourceY + y) * static_cast<size_t>(loaded.Width)) * 4;
        const size_t destinationOffset = ((frame * height + y) * totalWidth + face * width) * 4;
        std::memcpy(atlas.Rgba.data() + destinationOffset,
                    loaded.Rgba.data() + sourceOffset, copyWidth * 4);
      }
    }
  }
  return true;
}

GLuint UTextureCubeStorage::UploadCubeAtlas(const CubeAtlasPixels &atlas)
{
  GLuint name=0;glGenTextures(1,&name);
  struct Retire {GLuint Name;~Retire(){if(Name)glDeleteTextures(1,&Name);}} pending{name};
  if(!name)throw std::runtime_error("GL texture name allocation failed");
  glBindTexture(GL_TEXTURE_2D,name);ObserveTextureCreated(name);ObserveTextureMutation(name);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,atlas.Width,atlas.Height,0,GL_RGBA,GL_UNSIGNED_BYTE,atlas.Rgba.data());
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);glBindTexture(GL_TEXTURE_2D,0);
  if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("GL texture allocation/upload failed");
  ObserveTextureUploaded(name);
  pending.Name=0;return name;
}

std::optional<LegacyCubeDescriptor> CompileLegacyCubeDescriptor(const json &d)
{
  LegacyCubeDescriptor value;
  value.Name=d.value("name","");
  // Preserve the legacy int default/conversion before the size_t ID cast.
  value.Id=d.value("id",0);
  const auto textures=d.find("textures");
  if(value.Name.empty() || value.Id==0 || textures==d.end() || textures->empty() || !textures->is_array())return {};
  for(const auto &texture:*textures)value.Textures.push_back(texture.get<std::string>());
  return value;
}

std::optional<LegacyCubeDescriptor> LoadLegacyCubeDescriptorFile(const std::string &file_name)
{
  std::ifstream file(file_name);
  if (!file.is_open())
  {
    std::cerr << "Failed to open JSON file: " << file_name << std::endl;
    return {};
  }

  try
  {
    const auto d = ParseAndCloseCatalogFileJson(file,CatalogJsonKind::LegacyCube);
    return CompileLegacyCubeDescriptor(d);
  }
  catch (const json::exception &e)
  {
    std::cerr << "JSON parsing error in LoadJson: " << e.what() << std::endl;
    return {};
  }
}

} // namespace cutum

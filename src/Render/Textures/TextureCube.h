#ifndef TEXTURECUBE_H
#define TEXTURECUBE_H

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json_fwd.hpp>
typedef unsigned int GLuint;
#include "Render/Textures/TextureBase.h"
#include "World/Math/BlockTypes.h"

namespace cutum
{

class UBlockDefinitionStorage;
struct MergedCubeDesc;
struct LegacyCubeDescriptor
{
  std::string Name;
  size_t Id{};
  std::vector<std::string> Textures;
};
// Owned descriptor shared by the actual legacy loader and CPU verification.
std::optional<LegacyCubeDescriptor> CompileLegacyCubeDescriptor(const nlohmann::json &data);
std::optional<LegacyCubeDescriptor> LoadLegacyCubeDescriptorFile(const std::string &path);

class UTextureCube
{
public:
  UTextureCube();
  UTextureCube(const std::string &Name, size_t type_id,
               const std::vector<std::string> &texture_names);
  UTextureCube(const UTextureCube &) = default;

  UTextureCube &operator=(const UTextureCube &) = default;

  const std::string &GetName() const;
  size_t GetTypeId() const;

  GLuint GetTextureId() const;

  const std::vector<std::string> &GetTextureNames() const;
  size_t GetNumTextureFrames() const;
  void SetNumTextureFrames(size_t count);

  GLuint GetTexture() const;
  void SetTexture(GLuint value);
  template<class Inspector> void InspectStorage(Inspector &out) const {
    out.Text(Name);out.Vector(TextureNames);for(const auto &name:TextureNames) out.Text(name);
  }

private:
  std::string Name;
  size_t TypeId;
  GLuint TextureId;
  std::vector<std::string> TextureNames;
  size_t NumTextureFrames;

  GLuint Texture;
};

class UTextureCubeStorage
{
  struct PixelView
  {
    const unsigned char *Ptr{nullptr};
    size_t Count{0};
    const unsigned char *data() const {return Ptr;}
    size_t size() const {return Count;}
    bool empty() const {return Count==0;}
    unsigned char operator[](size_t i) const {return Ptr[i];}
  };
  struct LoadedStem
  {
    int Width{0},Height{0};
    PixelView Rgba;
    std::shared_ptr<const unsigned char> Owner;
    bool Ok{false};
  };
public:
  // Explicit owner; independent builders never inherit another batch's cache.
  // Use one batch at a time and keep source registrations stable during builds.
  class AtlasDecodeBatch
  {
    friend class UTextureCubeStorage;
    std::unordered_map<std::string,LoadedStem> Stems;
  public:
    AtlasDecodeBatch()=default;
    AtlasDecodeBatch(const AtlasDecodeBatch &)=delete;
    AtlasDecodeBatch &operator=(const AtlasDecodeBatch &)=delete;
    template<class Inspector>void InspectStorage(Inspector &out)const {
      out.Map(Stems);for(const auto &entry:Stems){out.Text(entry.first);out.DecodedTexture(entry.second.Owner);}
    }
  };
  struct AtlasDecodeSourceLayout {size_t cache_node_bytes{},control_request_upper{};bool supported{};};
  static AtlasDecodeSourceLayout GetAtlasDecodeSourceLayout();
  struct PublicationSourceLayout {size_t stage_bytes{},row_node_bytes{},cube_node_bytes{},name_node_bytes{};bool supported{};};
  static PublicationSourceLayout GetPublicationSourceLayout();
  UTextureCubeStorage(std::shared_ptr<UTextureBaseStorage> base_textures);

  void
  SetBlockDefinitions(std::shared_ptr<UBlockDefinitionStorage> definitions);
  void GenerateCubeTextures();
  void Load(const std::string &textures_path);
  void Clear();
  void BuildFromDescriptors(const std::vector<MergedCubeDesc> &descriptors);
  void PatchDescriptors(const std::vector<MergedCubeDesc> &descriptors);
  void PatchDescriptors(const std::vector<MergedCubeDesc> &descriptors,
                        const std::vector<BlockId> &removed);
  void RemoveCubeDescriptors(const std::vector<BlockId> &blockIds);

  const std::map<size_t, UTextureCube> &GetTextures() const;
  size_t GetTypeIdByName(const std::string &Name) const;

  // CPU-side catalog only; GL names do not describe GPU allocation bytes.
  template<class Inspector> void InspectStorage(Inspector &out) const {
    out.Tree(Textures);out.Tree(TexturesNames);
    for(const auto &entry:Textures) entry.second.InspectStorage(out);
    for(const auto &entry:TexturesNames) out.Text(entry.first);
    out.TextureBases(TextureBaseStorageInstance);out.BlockStorage(BlockDefinitions);
  }

  // CPU assembly is also available to validation tools without a GL context.
  struct CubeAtlasPixels
  {
    int Width{0};
    int Height{0};
    int Frames{0};
    std::vector<unsigned char> Rgba;
  };

  CubeAtlasPixels
  BuildCubeAtlasPixels(const std::string &cube_type_name,
                       const std::vector<std::string> &texture_names,
                       int stripFrameCount) const;
  CubeAtlasPixels BuildCubeAtlasPixels(const std::string &cube_type_name,
      const std::vector<std::string> &texture_names,int stripFrameCount,
      AtlasDecodeBatch &batch) const;
private:
  struct TexturePublication;
  void PublishDescriptors(const std::vector<MergedCubeDesc> &descriptors,bool replace,
                          const std::vector<BlockId> *removed=nullptr);
  static LoadedStem LoadStemRgba(const UTextureBase &base,AtlasDecodeBatch *batch);
  CubeAtlasPixels BuildCubeAtlasPixelsImpl(const std::string &cube_type_name,
      const std::vector<std::string> &texture_names,int stripFrameCount,
      AtlasDecodeBatch *batch) const;
  bool BuildCubeAtlasPixelsInto(CubeAtlasPixels &atlas,const std::string &cube_type_name,
      const std::vector<std::string> &texture_names,int stripFrameCount,
      AtlasDecodeBatch *batch,bool fill) const;
  static GLuint UploadCubeAtlas(const CubeAtlasPixels &atlas);

  std::shared_ptr<UTextureBaseStorage> TextureBaseStorageInstance;
  std::shared_ptr<UBlockDefinitionStorage> BlockDefinitions;

  std::map<size_t, UTextureCube> Textures;
  std::map<std::string, size_t> TexturesNames;
};

} // namespace cutum

#endif // TEXTURECUBE_H

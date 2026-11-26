#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Render/GlIncludes.h"
#include <GLFW/glfw3.h>
#include "Render/Textures/TextureCube.h"
#include "Render/Engine/RenderImageStorageReport.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "Core/Memory/TexturePublicationObservation.h"
#include "Core/UiPump.h"
#include "WorldGen/Core/TexturePublicationSourceEnvelope.h"
#include "Test/StbiAllocationFixture.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Test/OrdinaryNewObservation.h"
#include "Test/CatalogBoundProfileFixture.h"
#include <nlohmann/json.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>
#include <type_traits>

namespace texture_publication_fixture
{
using namespace cutum;using Json=nlohmann::json;namespace fs=std::filesystem;
inline std::array<GLuint,8192> Created{};inline size_t CreatedCount{},FirstMutationAttempt=SIZE_MAX,FirstMutationDecoderAttempt=SIZE_MAX;inline bool FreezeAfterMutation{},Overflow{};
inline void Record(GLuint name)noexcept{if(CreatedCount<Created.size())Created[CreatedCount++]=name;else Overflow=true;}
inline void Mutation()noexcept{if(observation::State.active){if(FirstMutationAttempt==SIZE_MAX){FirstMutationAttempt=observation::State.attempts;FirstMutationDecoderAttempt=GetStbiProcessAllocationSnapshot().allocation_attempts;}if(FreezeAfterMutation)observation::State.fail_after=observation::State.attempts;}}
inline TexturePixelData Pixels(int width,int height,int stage,int face)
{TexturePixelData p;p.Width=width;p.Height=height;for(int y=0;y<height;++y)for(int x=0;x<width;++x){p.Rgba.push_back(static_cast<unsigned char>(stage+face));p.Rgba.push_back(static_cast<unsigned char>(11+13*x));p.Rgba.push_back(static_cast<unsigned char>(19+17*y));p.Rgba.push_back(255);}return p;}
inline std::vector<MergedCubeDesc> Author(UTextureBaseStorage &bases,int count,int width,int height,int stage,size_t firstId,const std::string &prefix)
{std::vector<MergedCubeDesc> rows;for(int i=0;i<count;++i){MergedCubeDesc d;d.Id=static_cast<BlockId>(firstId+i);d.Name=prefix+std::to_string(i)+std::string(65,'n');for(int f=0;f<6;++f){const auto stem=prefix+std::to_string(i)+"_face_"+std::to_string(f);d.Stems[f]=stem;bases.RegisterPixels(stem,Pixels(width,height,stage+i*7,f));}rows.push_back(std::move(d));}return rows;}
struct NamesInspector
{
  Json Names=Json::object();
  template<class T>void Tree(const T &v){if constexpr(std::is_same_v<typename T::key_type,std::string>)for(const auto &p:v)Names[p.first]=p.second;}
  void Text(const std::string &){}
  template<class T>void Vector(const T &){}
  template<class T>void TextureBases(const T &){}
  template<class T>void BlockStorage(const T &){}
};
inline Json Snapshot(const UTextureCubeStorage &storage)
{
  Json result={{"textures",Json::object()}};NamesInspector names;storage.InspectStorage(names);result["names"]=names.Names;
  for(const auto &entry:storage.GetTextures()){
    const auto &cube=entry.second;const auto tex=cube.GetTexture();Json value={{"gl",tex},{"name",cube.GetName()},{"frames",cube.GetNumTextureFrames()},{"stems",cube.GetTextureNames()},{"alive",tex && glIsTexture(tex)}};
    if(tex && glIsTexture(tex)){glBindTexture(GL_TEXTURE_2D,tex);GLint w{},h{};glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);std::vector<unsigned char> data(static_cast<size_t>(w)*h*4);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,data.data());value["w"]=w;value["h"]=h;value["rgba"]=data;}
    result["textures"][std::to_string(entry.first)]=std::move(value);
  }glBindTexture(GL_TEXTURE_2D,0);return result;
}
inline size_t Orphans(const UTextureCubeStorage &storage,bool cleanup)
{std::set<GLuint> owned;for(const auto &p:storage.GetTextures())owned.insert(p.second.GetTexture());size_t leaks=0;for(size_t i=0;i<CreatedCount;++i)if(glIsTexture(Created[i]) && !owned.count(Created[i])){++leaks;if(cleanup)glDeleteTextures(1,&Created[i]);}return leaks;}
inline void ResetObservation(){CreatedCount=0;FirstMutationAttempt=FirstMutationDecoderAttempt=SIZE_MAX;Overflow=false;FreezeAfterMutation=false;}

struct AtlasPeak
{
  std::array<GLuint, 2> predecessor{}, candidates{};
  std::array<std::uint64_t, 2> simultaneousBytes{};
  std::size_t observed{};
  bool consistent{true};

  void Uploaded(GLuint name) noexcept
  {
    if (observed >= candidates.size())
    {
      consistent = false;
      return;
    }
    candidates[observed] = name;
    GLint prior{};
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prior);
    const auto size = [&](GLuint texture) -> std::uint64_t {
      if (!texture || !glIsTexture(texture))
      {
        consistent = false;
        return 0;
      }
      glBindTexture(GL_TEXTURE_2D, texture);
      GLint w{}, h{};
      glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
      glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
      consistent &= w > 0 && h > 0;
      return std::uint64_t(w) * std::uint64_t(h) * 4;
    };
    auto &total = simultaneousBytes[observed];
    for (const auto texture : predecessor)
    {
      consistent &= std::find(candidates.begin(), candidates.end(), texture) == candidates.end();
      total += size(texture);
    }
    for (std::size_t i = 0; i <= observed; ++i)
      total += size(candidates[i]);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prior));
    ++observed;
  }
};

inline AtlasPeak *ObservePeak{};
inline bool RefuseSecondUpload{};

inline bool Golden(const UTextureCubeStorage &storage, const std::vector<MergedCubeDesc> &old,
                   const std::vector<MergedCubeDesc> &rows, int route);

inline int GpuPeak(const char *path)
{
  auto bases = std::make_shared<UTextureBaseStorage>();
  UTextureCubeStorage storage(bases);
  const auto original = Author(*bases, 2, 2, 2, 3, 10, "original_");
  const auto successor = Author(*bases, 2, 3, 2, 103, 10, "successor_");
  const auto sameSize = Author(*bases, 2, 3, 2, 93, 10, "same_size_");
  storage.PatchDescriptors(original);
  const auto before = Snapshot(storage);
  const auto observe = [&] {
    RenderImageCensus census;
    storage.InspectRenderImages(census);
    return census.Read();
  };
  const auto baseline = observe();
  const std::array<GLuint, 2> old{storage.GetTextures().at(10).GetTexture(),
                                  storage.GetTextures().at(11).GetTexture()};
  GLuint pbo{};
  glGenBuffers(1, &pbo);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
  glBufferData(GL_PIXEL_UNPACK_BUFFER, 4, nullptr, GL_STATIC_DRAW);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 37);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 2);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
  const auto unpackRestored = [&] {
    GLint buffer{}, alignment{}, length{}, rows{}, pixels{};
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &buffer);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &length);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &rows);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &pixels);
    return static_cast<GLuint>(buffer) == pbo && alignment == 8 && length == 37 && rows == 2 && pixels == 1;
  };
  AtlasPeak refused{old};
  ObservePeak = &refused;
  RefuseSecondUpload = true;
  ResetObservation();
  bool rejected = false;
  try { storage.PatchDescriptors(successor); }
  catch (const std::runtime_error &) { rejected = true; }
  RefuseSecondUpload = false;
  ObservePeak = nullptr;
  const auto afterFailure = observe();
  const bool rollback = rejected && refused.consistent && refused.observed == 1 &&
      refused.simultaneousBytes[0] == 336 && CreatedCount == 2 &&
      !glIsTexture(Created[0]) && !glIsTexture(Created[1]) && unpackRestored() && Snapshot(storage) == before;

  AtlasPeak published{old};
  ObservePeak = &published;
  ResetObservation();
  storage.PatchDescriptors(successor);
  ObservePeak = nullptr;
  const auto replacement = observe();
  const auto actual = Snapshot(storage);
  const bool publication = published.consistent && published.observed == 2 &&
      published.simultaneousBytes == std::array<std::uint64_t, 2>{336, 480} &&
      !glIsTexture(old[0]) && !glIsTexture(old[1]) && unpackRestored() &&
      actual["textures"]["10"]["rgba"][0] == 103 && actual["textures"]["11"]["rgba"][0] == 110;
  const std::array<GLuint, 2> live{storage.GetTextures().at(10).GetTexture(),
                                   storage.GetTextures().at(11).GetTexture()};
  ResetObservation();
  storage.PatchDescriptors(sameSize);
  const bool inPlace = unpackRestored() && storage.GetTextures().at(10).GetTexture() == live[0] &&
      storage.GetTextures().at(11).GetTexture() == live[1] && !CreatedCount && Golden(storage, original, sameSize, 3);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  glDeleteBuffers(1, &pbo);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
  storage.Clear();
  const auto retired = observe();
  const bool cleaned = !glIsTexture(live[0]) && !glIsTexture(live[1]) && !retired.references &&
      !retired.logicalBytes && glGetError() == GL_NO_ERROR;
  const bool ok = rollback && publication && inPlace && cleaned && baseline.logicalBytes == 192 &&
      afterFailure.logicalBytes == 192 && replacement.logicalBytes == 288;
  const Json report{{"schema", "block-atlas-gpu-peak.v1"}, {"status", ok ? "PASS" : "FAIL"},
      {"baseline", DescribeRenderImages(baseline)}, {"failed_publication", DescribeRenderImages(afterFailure)},
      {"replacement", DescribeRenderImages(replacement)}, {"retired", DescribeRenderImages(retired)},
      {"failed_upload_observed", refused.observed}, {"failed_upload_live_bytes", refused.simultaneousBytes[0]},
      {"simultaneous_logical_bytes", published.simultaneousBytes}, {"rollback", rollback},
      {"publication", publication}, {"same_size_reuse_under_unpack_state", inPlace}, {"cleaned", cleaned}};
  if (path)
  {
    std::ofstream file(path);
    file << report.dump(2) << '\n';
    if (!file)
      return 1;
  }
  return ok ? 0 : 1;
}

inline int Baseline(const char *path)
{
  const auto root=fs::path("build/desktop-msvc/s4-texture-publication-fixtures/baseline");fs::create_directories(root);const auto directory=root.string();
  Json report={{"schema","texture-publication-real-gl-baseline.v1"},{"routes",Json::array()}};bool all=true;
  for(int route=0;route<5;++route){bool reproduced=false;size_t prefix=SIZE_MAX,orphans=0;bool pixelChanged=false;
    for(size_t i=0;i<512 && !reproduced;++i){auto bases=std::make_shared<UTextureBaseStorage>();UTextureCubeStorage storage(bases);const auto old=Author(*bases,2,2,2,3,10,"old_");storage.PatchDescriptors(old);
      const auto rows=Author(*bases,2,route==3?3:2,2,93,route==1 || route==4?12:10,"new_");
      if(route==4)for(size_t j=0;j<rows.size();++j){std::ofstream file(root/(std::to_string(j)+".json"));file<<Json{{"name",rows[j].Name},{"id",rows[j].Id},{"textures",rows[j].Stems}};}
      const auto before=Snapshot(storage);ResetObservation();bool threw=false;
      {observation::Scope scope(i);try{if(route==0)storage.BuildFromDescriptors(rows);else if(route==4)storage.Load(directory);else storage.PatchDescriptors(rows);}catch(const std::bad_alloc &){threw=true;}}
      const auto after=Snapshot(storage);orphans=Orphans(storage,false);pixelChanged=after["textures"].contains("10") && after["textures"]["10"].contains("rgba") && after["textures"]["10"]["rgba"]!=before["textures"]["10"]["rgba"];
      reproduced=threw && (route==1?orphans>0:route==2?pixelChanged:before!=after);if(reproduced)prefix=i;
      storage.Clear();Orphans(storage,true);if(observation::State.live || observation::State.blocks){std::cerr<<"baseline retained ordinary requests\n";return 1;}
    }
    report["routes"].push_back({{"route",route},{"reproduced",reproduced},{"prefix",prefix},{"orphan_GL_names",orphans},{"prior_pixels_changed",pixelChanged}});all=all && reproduced;
  }
  int calls=0;bool fail=true;UBlockingWorkPump::Install([&](const char *,float){++calls;if(fail)throw std::bad_alloc{};},0);
  try{UBlockingWorkPump::Pump("fixture");}catch(const std::bad_alloc &){}fail=false;UBlockingWorkPump::Pump("fixture");UBlockingWorkPump::Clear();
  report["callback_stuck_after_throw"]=calls==1;all=all && calls==1;report["status"]=all?"REPRODUCED":"FAIL";
  if(path){std::ofstream file(path);file<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return all?0:1;
}
inline bool Golden(const UTextureCubeStorage &storage,const std::vector<MergedCubeDesc> &old,
    const std::vector<MergedCubeDesc> &rows,int route)
{
  if(storage.GetTextures().size()!=rows.size()+(route==1 || route==4?old.size():0))return false;
  const auto check=[&](const MergedCubeDesc &d,int width,int stage){const auto found=storage.GetTextures().find(d.Id);if(found==storage.GetTextures().end())return false;const auto &cube=found->second;
    if(cube.GetName()!=d.Name || cube.GetNumTextureFrames()!=1 || cube.GetTextureNames()!=std::vector<std::string>(d.Stems.begin(),d.Stems.end()) || storage.GetTypeIdByName(d.Name)!=d.Id || !glIsTexture(cube.GetTexture()))return false;
    glBindTexture(GL_TEXTURE_2D,cube.GetTexture());GLint w=0,h=0;glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);if(w!=width*6 || h!=2)return false;
    std::vector<unsigned char> bytes(static_cast<size_t>(w)*h*4);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,bytes.data());
    for(int face=0;face<6;++face)for(int y=0;y<2;++y)for(int x=0;x<width;++x){const unsigned char pixel[]={static_cast<unsigned char>(stage+face),static_cast<unsigned char>(11+13*x),static_cast<unsigned char>(19+17*y),255};for(int c=0;c<4;++c)if(bytes[(y*w+face*width+x)*4+c]!=pixel[c])return false;}return true;};
  for(size_t i=0;i<rows.size();++i)if(!check(rows[i],route==3?3:2,93+static_cast<int>(i)*7))return false;
  for(size_t i=0;i<old.size();++i){if(route==1 || route==4){if(!check(old[i],2,3+static_cast<int>(i)*7))return false;}else if(storage.GetTypeIdByName(old[i].Name)!=0)return false;}
  glBindTexture(GL_TEXTURE_2D,0);return glGetError()==GL_NO_ERROR;
}
inline TexturePublicationSourceDomain SourceDomain(const UTextureCubeStorage &storage,const std::vector<MergedCubeDesc> &rows,size_t maximum,const fs::path *directory=nullptr,size_t callback_heap=0)
{
  TexturePublicationSourceDomain d;d.prior_metadata_heap=InspectTextureMetadataSource(storage,d.overflow);d.maximum_atlas_bytes=maximum;
  if(directory){d.max_path_units=directory->native().size();for(const auto &entry:fs::directory_iterator(*directory)){d.max_path_units=std::max(d.max_path_units,entry.path().native().size());if(entry.path().extension()==".json")AddTexturePublicationLegacyFile(d,static_cast<size_t>(fs::file_size(entry.path())),entry.path().native().size());}}
  else for(const auto &row:rows)AddTexturePublicationRow(d,row.Name,std::vector<std::string>(row.Stems.begin(),row.Stems.end()),maximum);
  if(callback_heap){d.callback_calls=directory?d.legacy_files:rows.size();d.callback_heap_per_call=callback_heap;}return d;
}

struct LateAtlasUpdate
{
  GLuint First{}, Second{}, PackBuffer{};
  bool RejectTransfer{}, SawFirstWrite{}, Injected{}, PackRestoredBeforeUpdates{};

  void ConfigurePack(bool hostile) const noexcept
  {
    glBindBuffer(GL_PIXEL_PACK_BUFFER, hostile ? PackBuffer : 0);
    for (size_t i = 0; i < PackParameters.size(); ++i)
      glPixelStorei(PackParameters[i], hostile ? HostilePackValues[i] : (i == 0 ? 1 : 0));
  }

  bool PackStateRestored() const noexcept
  {
    GLint buffer{};
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &buffer);
    std::array<GLint, PackParameters.size()> actual{};
    for (size_t i = 0; i < PackParameters.size(); ++i)
      glGetIntegerv(PackParameters[i], &actual[i]);
    return static_cast<GLuint>(buffer) == PackBuffer && actual == HostilePackValues;
  }

  void Mutating(GLuint name) noexcept
  {
    if (name == First)
      PackRestoredBeforeUpdates = PackStateRestored();
    if (name != Second || Injected)
      return;
    ConfigurePack(false);
    std::array<unsigned char, 96> first{};
    glBindTexture(GL_TEXTURE_2D, First);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, first.data());
    SawFirstWrite = glGetError() == GL_NO_ERROR && first[0] == 93;
    ConfigurePack(true);
    glBindTexture(GL_TEXTURE_2D, Second);
    // The first mode makes the real transfer fail. The second leaves a GL
    // error pending while the transfer can still change the second texture.
    if (RejectTransfer)
      glBindTexture(GL_TEXTURE_2D, 0);
    else
      glBindTexture(0, 0);
    Injected = true;
  }

  static constexpr std::array<GLenum, 6> PackParameters{
      GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS,
      GL_PACK_IMAGE_HEIGHT, GL_PACK_SKIP_IMAGES};
  static constexpr std::array<GLint, PackParameters.size()> HostilePackValues{8, 37, 2, 1, 23, 1};
};

inline LateAtlasUpdate *ObserveLateUpdate{};

inline int LateInPlace(const char *path)
{
  Json cases = Json::array();
  bool passed = true;
  for (const bool rejectTransfer : {true, false})
  {
    auto bases = std::make_shared<UTextureBaseStorage>();
    UTextureCubeStorage storage(bases);
    const auto old = Author(*bases, 2, 2, 2, 3, 10, "old_");
    const auto next = Author(*bases, 3, 2, 2, 93, 10, "next_");
    storage.PatchDescriptors(old);
    const auto before = Snapshot(storage);
    LateAtlasUpdate late{storage.GetTextures().at(10).GetTexture(),
                         storage.GetTextures().at(11).GetTexture(), 0, rejectTransfer};
    const auto bound = BoundTexturePublicationSource(SourceDomain(storage, next, 96));
    glGenBuffers(1, &late.PackBuffer);
    late.ConfigurePack(true);
    glBufferData(GL_PIXEL_PACK_BUFFER, 4, nullptr, GL_STATIC_DRAW);

    ResetObservation();
    ObserveLateUpdate = &late;
    bool rejected = false;
    {
      observation::Scope scope;
      try { storage.PatchDescriptors(next); }
      catch (const std::runtime_error &) { rejected = true; }
    }
    ObserveLateUpdate = nullptr;
    const auto counts = observation::State;
    const bool packRestored = late.PackRestoredBeforeUpdates && late.PackStateRestored();
    late.ConfigurePack(false);

    const auto after = Snapshot(storage);
    const bool predecessorExact = after == before;
    const bool candidateRetired = CreatedCount == 1 && !glIsTexture(Created[0]);
    const bool bounded = bound.supported && !bound.overflow && bound.rollback_pixel_upper == 288 &&
        counts.peak <= bound.ordinary_new_upper && !counts.live && !counts.blocks;
    const bool cleanError = glGetError() == GL_NO_ERROR;
    ResetObservation();
    late.ConfigurePack(true);
    storage.PatchDescriptors(next);
    const bool retryPackRestored = late.PackStateRestored();
    late.ConfigurePack(false);
    glDeleteBuffers(1, &late.PackBuffer);
    const bool retry = retryPackRestored && CreatedCount == 1 && storage.GetTextures().at(10).GetTexture() == late.First &&
        storage.GetTextures().at(11).GetTexture() == late.Second && Golden(storage, old, next, 2);
    const auto newest = storage.GetTextures().at(12).GetTexture();
    storage.Clear();
    const bool cleared = !glIsTexture(late.First) && !glIsTexture(late.Second) && !glIsTexture(newest);
    passed &= rejected && late.Injected && late.SawFirstWrite && packRestored && predecessorExact &&
        candidateRetired && bounded && cleanError && retry && cleared;
    cases.push_back({{"reject_transfer", rejectTransfer}, {"rejected", rejected},
        {"first_write_visible_before_failure", late.SawFirstWrite}, {"pack_restored", packRestored},
        {"predecessor_exact", predecessorExact}, {"first_red_after_failure", after["textures"]["10"]["rgba"][0]},
        {"second_red_after_failure", after["textures"]["11"]["rgba"][0]},
        {"staged_candidate_retired", candidateRetired}, {"ordinary_peak", counts.peak},
        {"ordinary_upper", bound.ordinary_new_upper}, {"rollback_pixel_upper", bound.rollback_pixel_upper},
        {"bounded_and_retired", bounded},
        {"error_drained", cleanError}, {"exact_retry_with_same_names", retry}, {"cleared", cleared}});
  }
  const Json report{{"schema", "block-atlas-late-in-place.v1"}, {"status", passed ? "PASS" : "FAIL"},
                    {"cases", cases}};
  if (path)
  {
    std::ofstream file(path);
    file << report.dump(2) << '\n';
    if (!file)
      return 1;
  }
  return passed ? 0 : 1;
}

inline bool PngGolden(const UTextureCubeStorage &storage,const std::vector<MergedCubeDesc> &rows)
{
  for(const auto &d:rows){const auto it=storage.GetTextures().find(d.Id);if(it==storage.GetTextures().end() || it->second.GetName()!=d.Name || it->second.GetNumTextureFrames()!=1 || storage.GetTypeIdByName(d.Name)!=d.Id)return false;
    const auto name=it->second.GetTexture();if(!glIsTexture(name))return false;glBindTexture(GL_TEXTURE_2D,name);GLint w=0,h=0;glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);if(w!=384 || h!=64)return false;
    std::vector<unsigned char> pixels(384*64*4);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    for(int face=0;face<6;++face)for(int y=0;y<64;++y)for(int x=0;x<64;++x){const auto p=test::StbiFixtureRgb(x,y);const size_t at=(y*384+face*64+x)*4;for(int c=0;c<3;++c)if(pixels[at+c]!=p[c])return false;if(pixels[at+3]!=255)return false;}
  }glBindTexture(GL_TEXTURE_2D,0);return glGetError()==GL_NO_ERROR;
}
inline void Extended(const fs::path &root,Json &report,size_t &failures,size_t &prefixes)
{
  report["extended_routes"]=Json::array();size_t decoderPrefixes=0;bool negativePixels=false;
  const auto image=root/"decoder.png";const auto encoded=test::MakeStbiAllocationPng();{std::ofstream f(image,std::ios::binary);f.write(reinterpret_cast<const char *>(encoded.data()),encoded.size());}
  StbiSourceDomain png;png.width=png.height=64;png.png_idat_capacity_upper=16384;png.png_expanded_capacity_upper=64*64*3+64;
  for(int kind=0;kind<6;++kind){const bool file=kind!=0;const int route=file?kind-1:2;const auto directory=root/("extended_"+std::to_string(kind));fs::create_directories(directory);const auto input=directory.string();
    auto bases=std::make_shared<UTextureBaseStorage>();const auto old=Author(*bases,2,file && route==2?64:2,file && route==2?64:2,3,10,"old_");std::vector<MergedCubeDesc> rows;
    if(file){bases->Register("decoded",image.string());for(int i=0;i<2;++i){MergedCubeDesc d;d.Name="decoded_"+std::to_string(i)+std::string(65,'n');d.Id=static_cast<BlockId>((route==1 || route==4?12:10)+i);d.Stems.fill("decoded");rows.push_back(d);}}
    else rows=Author(*bases,2,2,2,93,10,"new_");
    if(route==4)for(size_t i=0;i<rows.size();++i){std::ofstream f(directory/(std::to_string(i)+".json"));f<<Json{{"name",rows[i].Name},{"id",rows[i].Id},{"textures",rows[i].Stems}};}
    size_t callbacks=0;std::unique_ptr<UTextureCubeStorage> storage;
    const auto setup=[&]{UBlockingWorkPump::Clear();storage=std::make_unique<UTextureCubeStorage>(bases);storage->PatchDescriptors(old);callbacks=0;if(!file)UBlockingWorkPump::Install([&](const char *,float){++callbacks;void *p=::operator new(512);*static_cast<volatile unsigned char *>(p)=1;::operator delete(p);},0);};
    const auto operation=[&]{if(route==0)storage->BuildFromDescriptors(rows);else if(route==4)storage->Load(input);else storage->PatchDescriptors(rows);};
    const auto golden=[&]{return file?PngGolden(*storage,rows):Golden(*storage,old,rows,route);};
    const auto cleanup=[&]{UBlockingWorkPump::Clear();storage->Clear();Orphans(*storage,true);storage.reset();};
    setup();auto domain=SourceDomain(*storage,rows,file?98304:96,route==4?&directory:nullptr,file?0:512);
    if(file)for(size_t i=0;i<rows.size()*6;++i)AddTexturePublicationDecode(domain,png,image.string().size());const auto bound=BoundTexturePublicationSource(domain);
    auto ledger=std::make_shared<UStbiAllocationLedger>();ResetObservation();{observation::Scope scope;UStbiAllocationScope active(ledger);operation();}const auto normal=observation::State;const auto decoded=ledger->GetSnapshot();
    bool ok=bound.supported && !bound.overflow && golden() && !Orphans(*storage,false) && normal.peak<=bound.ordinary_new_upper && decoded.peak_admitted_request_bytes<=bound.new_decoder_peak_upper && !decoded.live_request_bytes;
    if(file && !negativePixels){const auto name=storage->GetTextures().at(rows[0].Id).GetTexture();const auto p=test::StbiFixtureRgb(0,0);const unsigned char changed[]={static_cast<unsigned char>(p[0]^1),p[1],p[2],255};glBindTexture(GL_TEXTURE_2D,name);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,changed);glBindTexture(GL_TEXTURE_2D,0);negativePixels=!PngGolden(*storage,rows);ok=ok && negativePixels;}
    cleanup();ok=ok && !observation::State.live && !observation::State.blocks;
    size_t ordinaryChecked=0,decoderChecked=0;
    for(size_t i=0;ok && i<normal.attempts;++i){setup();const auto before=Snapshot(*storage);ledger=std::make_shared<UStbiAllocationLedger>();ResetObservation();bool refused=false;
      {observation::Scope scope(i);UStbiAllocationScope active(ledger);try{operation();}catch(const std::bad_alloc &){refused=true;}}
      const auto counts=observation::State;ok=refused && counts.refusals && Snapshot(*storage)==before && !CreatedCount && !counts.live && counts.peak<=bound.ordinary_new_upper && !ledger->GetSnapshot().live_request_bytes;
      if(!file){const auto prior=callbacks;UBlockingWorkPump::Pump("retry",0);ok=ok && callbacks==prior+1;}
      ledger=std::make_shared<UStbiAllocationLedger>();if(ok){ResetObservation();observation::Scope scope;UStbiAllocationScope active(ledger);operation();}ok=ok && golden() && !Orphans(*storage,false);cleanup();ok=ok && !observation::State.live && !observation::State.blocks && !ledger->GetSnapshot().live_request_bytes;if(ok)++ordinaryChecked;
    }
    for(size_t i=0;ok && i<decoded.allocation_attempts;++i){setup();const auto before=Snapshot(*storage);ledger=std::make_shared<UStbiAllocationLedger>(SIZE_MAX,i);ResetObservation();bool refused=false;
      {observation::Scope scope;UStbiAllocationScope active(ledger);try{operation();}catch(const std::bad_alloc &){refused=true;}}
      ok=refused && ledger->GetSnapshot().refusals && Snapshot(*storage)==before && !CreatedCount && !observation::State.live && !ledger->GetSnapshot().live_request_bytes;
      ledger=std::make_shared<UStbiAllocationLedger>();if(ok){observation::Scope scope;UStbiAllocationScope active(ledger);operation();}ok=ok && golden() && !Orphans(*storage,false);cleanup();ok=ok && !observation::State.live && !observation::State.blocks && !ledger->GetSnapshot().live_request_bytes;if(ok)++decoderChecked;
    }
    setup();ledger=std::make_shared<UStbiAllocationLedger>();ResetObservation();FreezeAfterMutation=true;bool late=false;
    {observation::Scope scope;UStbiAllocationScope active(ledger);try{operation();}catch(const std::bad_alloc &){late=true;}}
    const bool noLate=!late && observation::State.attempts==FirstMutationAttempt && GetStbiProcessAllocationSnapshot().allocation_attempts==FirstMutationDecoderAttempt && golden() && !Orphans(*storage,false);cleanup();ok=ok && noLate && !observation::State.live && !observation::State.blocks && !ledger->GetSnapshot().live_request_bytes;
    if(!ok)++failures;prefixes+=ordinaryChecked;decoderPrefixes+=decoderChecked;report["extended_routes"].push_back({{"kind",kind},{"route",route},{"pass",ok},{"ordinary_attempts",normal.attempts},{"ordinary_prefixes",ordinaryChecked},{"ordinary_peak",normal.peak},{"ordinary_upper",bound.ordinary_new_upper},{"decoder_attempts",decoded.allocation_attempts},{"decoder_prefixes",decoderChecked},{"decoder_peak",decoded.peak_admitted_request_bytes},{"decoder_upper",bound.new_decoder_peak_upper},{"no_late_allocations",noLate}});
  }
  report["decoder_refusal_and_exact_retry_cases"]=decoderPrefixes;
  report["changed_real_GL_pixel_rejected"]=negativePixels;if(!negativePixels)++failures;
  {auto bases=std::make_shared<UTextureBaseStorage>();const auto old=Author(*bases,2,2,2,3,10,"combined_old_");const auto rows=Author(*bases,1,2,2,93,10,"combined_new_");const std::vector<BlockId> removed{10};std::unique_ptr<UTextureCubeStorage> storage;
    const auto setup=[&]{storage=std::make_unique<UTextureCubeStorage>(bases);storage->PatchDescriptors(old);};const auto operation=[&]{storage->PatchDescriptors(rows,removed);};
    setup();const auto bound=BoundTexturePublicationSource(SourceDomain(*storage,rows,96));ResetObservation();{observation::Scope scope;operation();}const auto normal=observation::State;bool ok=normal.peak<=bound.ordinary_new_upper && !Orphans(*storage,false);storage->Clear();storage.reset();ok=ok && !observation::State.live;
    size_t checked=0;
    for(size_t i=0;ok && i<normal.attempts;++i){setup();const auto before=Snapshot(*storage);ResetObservation();bool refused=false;
      {observation::Scope scope(i);try{operation();}catch(const std::bad_alloc &){refused=true;}}
      ok=refused && observation::State.refusals && !observation::State.live && !CreatedCount && Snapshot(*storage)==before;
      const auto oldName=before["textures"]["10"]["gl"].get<GLuint>();if(ok){observation::Scope scope;operation();}ok=ok && !glIsTexture(oldName) && storage->GetTypeIdByName(rows[0].Name)==10 && storage->GetTypeIdByName(old[0].Name)==0 && storage->GetTextures().size()==2 && !Orphans(*storage,false);
      storage->Clear();storage.reset();ok=ok && !observation::State.live && !observation::State.blocks;if(ok)++checked;
    }
    setup();ResetObservation();FreezeAfterMutation=true;bool late=false;{observation::Scope scope;try{operation();}catch(const std::bad_alloc &){late=true;}}const bool noLate=!late && observation::State.attempts==FirstMutationAttempt && !observation::State.refusals;
    storage->Clear();storage.reset();ok=ok && noLate && !observation::State.live && !observation::State.blocks;prefixes+=checked;if(!ok)++failures;report["combined_removal_patch"]={{"pass",ok},{"attempts",normal.attempts},{"prefixes",checked},{"peak",normal.peak},{"upper",bound.ordinary_new_upper},{"no_late_allocations",noLate}};
  }
  // Logical-name retirement only removes the alias owned by the retired ID.
  {auto bases=std::make_shared<UTextureBaseStorage>();UTextureCubeStorage storage(bases);auto rows=Author(*bases,2,2,2,93,10,"alias_");rows[0].Name=rows[1].Name;ResetObservation();storage.PatchDescriptors(rows);const auto name=rows[1].Name;const auto first=storage.GetTextures().at(rows[0].Id).GetTexture(),last=storage.GetTextures().at(rows[1].Id).GetTexture();
    storage.RemoveCubeDescriptors({rows[0].Id});const bool ok=storage.GetTypeIdByName(name)==rows[1].Id && !glIsTexture(first) && glIsTexture(last);storage.RemoveCubeDescriptors({rows[1].Id});const bool retired=storage.GetTypeIdByName(name)==0 && !glIsTexture(last) && !Orphans(storage,false);storage.Clear();if(!ok || !retired)++failures;report["name_alias_owner_retirement"]=ok && retired;
  }
  // Same GL extent with changed animation metadata still reuses the old name.
  {auto bases=std::make_shared<UTextureBaseStorage>();UTextureCubeStorage storage(bases);auto rows=Author(*bases,1,2,4,13,10,"animation_");rows[0].AnimFrames=2;ResetObservation();storage.PatchDescriptors(rows);const auto old=storage.GetTextures().at(10).GetTexture();rows[0].AnimFrames=4;storage.PatchDescriptors(rows);
    bool ok=storage.GetTextures().at(10).GetTexture()==old && storage.GetTextures().at(10).GetNumTextureFrames()==4;
    auto definitions=std::make_shared<UBlockDefinitionStorage>();BlockDefinition definition;definition.Id=10;definition.Name=rows[0].Name;definition.Animation.FrameCount=2;definitions->ReplaceAll({{10,definition}},{{definition.Name,10}});storage.SetBlockDefinitions(definitions);storage.PatchDescriptors(rows);ok=ok && storage.GetTextures().at(10).GetTexture()==old && storage.GetTextures().at(10).GetNumTextureFrames()==2;
    storage.Clear();if(!ok)++failures;report["same_extent_animation_and_definition_priority"]=ok;
  }
  {auto bases=std::make_shared<UTextureBaseStorage>();UTextureCubeStorage storage(bases);auto old=Author(*bases,1,2,2,3,10,"duplicate_old_");storage.PatchDescriptors(old);auto rows=Author(*bases,3,2,2,33,10,"duplicate_");rows[1].Id=rows[2].Id=rows[0].Id;storage.PatchDescriptors(rows);const auto state=Snapshot(storage);const auto &v=state["textures"]["10"];
    const bool ok=storage.GetTextures().size()==1 && storage.GetTextures().at(10).GetName()==rows.back().Name && v["rgba"][0]==47 && storage.GetTypeIdByName(rows[0].Name)==0 && storage.GetTypeIdByName(rows[1].Name)==0 && !Orphans(storage,false);storage.Clear();if(!ok)++failures;report["duplicate_id_last_valid_wins"]=ok;
  }
  {auto bases=std::make_shared<UTextureBaseStorage>();UTextureCubeStorage storage(bases);const auto old=Author(*bases,1,2,2,3,10,"invalid_old_");storage.PatchDescriptors(old);const auto before=Snapshot(storage);auto invalid=old;invalid[0].Stems.fill("absent");storage.PatchDescriptors(invalid);bool ok=Snapshot(storage)==before;
    {observation::Scope scope(0);storage.PatchDescriptors({});}ok=ok && !observation::State.attempts && Snapshot(storage)==before;
    const auto name=storage.GetTextures().at(10).GetTexture();{observation::Scope scope(0);storage.BuildFromDescriptors({});}ok=ok && !observation::State.attempts && storage.GetTextures().empty() && !glIsTexture(name);storage.Clear();if(!ok)++failures;report["invalid_source_and_empty_operations"]=ok;
  }
  {auto bases=std::make_shared<UTextureBaseStorage>();UTextureCubeStorage storage(bases);const auto old=Author(*bases,1,2,2,3,10,"limit_old_");storage.PatchDescriptors(old);const auto before=Snapshot(storage);GLint limit=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);const auto rows=Author(*bases,1,limit/6+1,1,93,10,"limit_new_");ResetObservation();bool rejected=false;
    try{storage.PatchDescriptors(rows);}catch(const std::runtime_error &){rejected=true;}const bool ok=rejected && !CreatedCount && Snapshot(storage)==before;storage.Clear();if(!ok)++failures;report["backend_dimension_rejected_before_publication"]=ok;
  }
  {auto bases=std::make_shared<UTextureBaseStorage>();UTextureCubeStorage storage(bases);const auto old=Author(*bases,1,2,2,3,10,"legacy_old_");storage.PatchDescriptors(old);const auto before=Snapshot(storage);const auto directory=root/"invalid_legacy";fs::create_directories(directory);
    {std::ofstream f(directory/"cube.json");f<<Json{{"name","missing_source"},{"id",10},{"textures",std::vector<std::string>(6,"absent")}};}storage.Load(directory.string());const bool ok=Snapshot(storage)==before;storage.Clear();if(!ok)++failures;report["legacy_missing_source_preserves_existing_texture"]=ok;
  }
  // Actual publication phases with retained target/base and escaped CPU data.
  {auto input=std::make_shared<UTextureBaseStorage>();const auto old=Author(*input,2,2,2,3,10,"graph_old_");const auto rows=Author(*input,2,3,2,93,10,"graph_new_");
    const auto directory=root/"graph_legacy";fs::create_directories(directory);std::vector<std::string> explicitStems(old[0].Stems.begin(),old[0].Stems.end());explicitStems.insert(explicitStems.end(),rows[0].Stems.begin(),rows[0].Stems.end());
    {std::ofstream f(directory/"cube.json");f<<"\xEF\xBB\xBF\r\n"<<Json{{"name","graph_explicit"},{"id",12},{"textures",explicitStems}};}
    std::shared_ptr<UTextureBaseStorage> bases;std::shared_ptr<UTextureCubeStorage> storage;UTextureCubeStorage::CubeAtlasPixels escaped;
    ResetObservation();{observation::Scope scope;bases=std::make_shared<UTextureBaseStorage>(*input);storage=std::make_shared<UTextureCubeStorage>(bases);storage->PatchDescriptors(old);escaped=storage->BuildCubeAtlasPixels("escaped",std::vector<std::string>(old[0].Stems.begin(),old[0].Stems.end()),0);}
    const auto census=[&]{detail::CatalogStorageInspector out;out.TextureCubes(storage);out.TextureBases(bases);out.AtlasPixels(&escaped);return out.Result;};
    catalog_bound_profile_fixture::Profile profile(BoundRetainedCatalogRequests(census()));
    size_t calls=0;UBlockingWorkPump::Install([&](const char *,float){++calls;void *p=::operator new(512);::operator delete(p);},0);report["phases"]=Json::array();
    const auto phase=[&](const char *name,const TexturePublicationSourceEnvelope &source,auto operation){const auto c=census();const auto entry=BoundRetainedCatalogRequests(c);const auto bound=ComposeCatalogConstruction(entry,source.combined_additional_upper,source.supported,source.overflow);const auto held=GetStbiProcessAllocationSnapshot().live_request_bytes;const auto observed=observation::State.live+held;auto ledger=std::make_shared<UStbiAllocationLedger>();
      const auto ownerBound=profile.Phase(entry,source.combined_additional_upper,source.supported,source.overflow);
      observation::State.peak=observation::State.live;observation::State.active=true;{UStbiAllocationScope active(ledger);operation();}observation::State.active=false;
      const auto peak=observation::State.peak+held+ledger->GetSnapshot().peak_admitted_request_bytes;const bool ok=bound.supported && !bound.overflow && c.decoder_request_bytes==held && observed<=entry.requested_heap_upper && peak<=bound.requested_heap_upper && ownerBound.supported && !ownerBound.overflow && peak<=ownerBound.requested_heap_upper;if(!ok)++failures;
      report["phases"].push_back({{"name",name},{"pass",ok},{"entry_observed",observed},{"entry_upper",entry.requested_heap_upper},{"observed_components",peak},{"upper",bound.requested_heap_upper},{"owner_profile_upper",ownerBound.requested_heap_upper}});
    };
    const auto source=BoundTexturePublicationSource(SourceDomain(*storage,rows,144,nullptr,512));phase("multi_texture_patch",source,[&]{storage->PatchDescriptors(rows);});
    const auto fileSource=BoundTexturePublicationSource(SourceDomain(*storage,{},192,&directory,512));phase("legacy_explicit_frames",fileSource,[&]{storage->Load(directory.string());});
    UBlockingWorkPump::Clear();const auto snapshot=Snapshot(*storage);const auto &legacy=snapshot["textures"]["12"];bool oracle=legacy["frames"]==2 && legacy["w"]==12 && legacy["h"]==4 && storage->GetTypeIdByName("graph_explicit")==12 && escaped.Width==12 && escaped.Height==2 && !Orphans(*storage,false);
    for(int frame=0;frame<2;++frame)for(int face=0;face<6;++face)for(int y=0;y<2;++y)for(int x=0;x<2;++x){const unsigned char pixel[]={static_cast<unsigned char>((frame?93:3)+face),static_cast<unsigned char>(11+13*x),static_cast<unsigned char>(19+17*y),255};for(int c=0;c<4;++c)oracle=oracle && legacy["rgba"][((frame*2+y)*12+face*2+x)*4+c]==pixel[c];}
    for(int face=0;face<6;++face)for(int y=0;y<2;++y)for(int x=0;x<2;++x){const unsigned char pixel[]={static_cast<unsigned char>(3+face),static_cast<unsigned char>(11+13*x),static_cast<unsigned char>(19+17*y),255};for(int c=0;c<4;++c)oracle=oracle && escaped.Rgba[(y*12+face*2+x)*4+c]==pixel[c];}
    const auto alias=[&]{detail::CatalogStorageInspector one,many;one.TextureCubes(storage);one.AtlasPixels(&escaped);for(int i=0;i<64;++i){many.TextureCubes(storage);many.AtlasPixels(&escaped);}return one.Result.known_storage_bytes==many.Result.known_storage_bytes && one.Result.shared_owners==many.Result.shared_owners;};const bool dedup=alias();
    const auto retained=BoundRetainedCatalogRequests(census());const auto observed=observation::State.live+GetStbiProcessAllocationSnapshot().live_request_bytes;oracle=oracle && dedup && observed<=retained.requested_heap_upper;
    report["retained"]={{"observed",observed},{"upper",retained.requested_heap_upper},{"alias64_deduplicated",dedup}};report["retained_and_independent_pixel_oracles"]=oracle;if(!oracle)++failures;
    auto boundChecks=profile.Controls(retained);if(!boundChecks["pass"].get<bool>())++failures;report["owner_profile_binding"]=boundChecks;
    storage->Clear();storage.reset();bases.reset();escaped={};const bool zero=!observation::State.live && !observation::State.blocks && !GetStbiProcessAllocationSnapshot().live_request_bytes;if(!zero)++failures;report["graph_final_zero"]=zero;
  }
  TexturePublicationSourceDomain overflow;overflow.rows=SIZE_MAX;const bool overflowRejected=BoundTexturePublicationSource(overflow).overflow;TexturePublicationSourceDomain missing;missing.callback_calls=1;missing.callback_profile_known=false;
  const bool unknownRejected=!BoundTexturePublicationSource(missing).supported;bool extra=false;const auto bound=BoundTexturePublicationSource(TexturePublicationSourceDomain{});
  {observation::Scope scope(0);try{void *p=::operator new(bound.combined_additional_upper+1);::operator delete(p);}catch(const std::bad_alloc &){extra=observation::State.max_request>bound.combined_additional_upper;}}
  if(!overflowRejected || !unknownRejected || !extra)++failures;report["overflow_rejected"]=overflowRejected;report["unknown_callback_profile_rejected"]=unknownRejected;report["unmodeled_allocation_control"]=extra;
  const auto before=GetStbiThreadFailureGeneration();std::thread foreign([]{auto ledger=std::make_shared<UStbiAllocationLedger>(0);UStbiAllocationScope active(ledger);void *p=StbiAllocate(8);StbiFree(p);});foreign.join();const bool independent=before==GetStbiThreadFailureGeneration();if(!independent)++failures;report["allocator_failure_generation_thread_isolation"]=independent;report["allocator_thread_inline_bytes"]=StbiCallingThreadInlineBytes();
}
inline int Accepted(const char *path)
{
  const auto root=fs::path("build/desktop-msvc/s4-texture-publication-fixtures/accepted");fs::create_directories(root);const auto directory=root.string();
  Json report={{"schema","texture-publication-real-gl.v1"},{"routes",Json::array()}};size_t failures=0,prefixes=0;
  for(int route=0;route<5;++route){auto bases=std::make_shared<UTextureBaseStorage>();auto storage=std::make_unique<UTextureCubeStorage>(bases);const auto old=Author(*bases,2,2,2,3,10,"old_");storage->PatchDescriptors(old);
    const auto rows=Author(*bases,2,route==3?3:2,2,93,route==1 || route==4?12:10,"new_");
    if(route==4)for(size_t j=0;j<rows.size();++j){std::ofstream file(root/(std::to_string(j)+".json"));file<<Json{{"name",rows[j].Name},{"id",rows[j].Id},{"textures",rows[j].Stems}};}
    const auto operation=[&]{if(route==0)storage->BuildFromDescriptors(rows);else if(route==4)storage->Load(directory);else storage->PatchDescriptors(rows);};
    const auto source=BoundTexturePublicationSource(SourceDomain(*storage,rows,route==3?144:96,route==4?&root:nullptr));
    const auto original=Snapshot(*storage);ResetObservation();{observation::Scope scope;operation();}const auto normal=observation::State;
    bool ok=source.supported && !source.overflow && normal.peak<=source.ordinary_new_upper && Golden(*storage,old,rows,route) && !Orphans(*storage,false) && !Overflow;
    for(const auto &row:old){const auto name=original["textures"][std::to_string(row.Id)]["gl"].get<GLuint>();const bool reused=route==1 || route==2 || route==4;ok=ok && bool(glIsTexture(name))==reused;if(route==2)ok=ok && storage->GetTextures().at(row.Id).GetTexture()==name;}
    storage->Clear();storage.reset();ok=ok && !observation::State.live && !observation::State.blocks;
    size_t checked=0;
    for(size_t i=0;ok && i<normal.attempts;++i){storage=std::make_unique<UTextureCubeStorage>(bases);storage->PatchDescriptors(old);const auto before=Snapshot(*storage);ResetObservation();bool refused=false;
      {observation::Scope scope(i);try{operation();}catch(const std::bad_alloc &){refused=true;}}
      const auto counts=observation::State;ok=refused && counts.refusals && Snapshot(*storage)==before && !CreatedCount && !Orphans(*storage,false) && !counts.live && !counts.blocks && counts.peak<=source.ordinary_new_upper;
      if(!ok)report["first_failure"]={{"route",route},{"prefix",i},{"refused",refused},{"refusals",counts.refusals},{"created",CreatedCount},{"live",counts.live},{"snapshot_equal",Snapshot(*storage)==before}};
      if(ok){ResetObservation();observation::Scope scope;operation();}ok=ok && Golden(*storage,old,rows,route) && !Orphans(*storage,false);
      storage->Clear();Orphans(*storage,true);storage.reset();ok=ok && !observation::State.live && !observation::State.blocks;if(ok)++checked;
    }
    // Refuse any ordinary-new request after the first real GL mutation.
    storage=std::make_unique<UTextureCubeStorage>(bases);storage->PatchDescriptors(old);ResetObservation();FreezeAfterMutation=true;bool lateThrow=false;
    {observation::Scope scope;try{operation();}catch(const std::bad_alloc &){lateThrow=true;}}
    const auto committed=observation::State;const bool noLate=!lateThrow && !committed.refusals && committed.attempts==FirstMutationAttempt && GetStbiProcessAllocationSnapshot().allocation_attempts==FirstMutationDecoderAttempt && Golden(*storage,old,rows,route) && !Orphans(*storage,false);
    storage->Clear();Orphans(*storage,true);storage.reset();ok=ok && noLate && !observation::State.live && !observation::State.blocks;
    if(!ok)++failures;prefixes+=checked;report["routes"].push_back({{"route",route},{"pass",ok},{"attempts",normal.attempts},{"prefixes",checked},{"peak",normal.peak},{"ordinary_upper",source.ordinary_new_upper},{"no_allocation_after_first_GL_mutation",noLate}});
  }
  int calls=0;bool fail=true;UBlockingWorkPump::Install([&](const char *,float){++calls;if(fail)throw std::bad_alloc{};},0);
  try{UBlockingWorkPump::Pump("fixture");}catch(const std::bad_alloc &){}fail=false;UBlockingWorkPump::Pump("fixture");UBlockingWorkPump::Clear();const bool callback=calls==2;if(!callback)++failures;
  Extended(root,report,failures,prefixes);
  report["callback_recovers_after_throw"]=callback;report["ordinary_refusal_and_exact_retry_cases"]=prefixes;report["failed_cases"]=failures;report["status"]=failures?"FAIL":"PASS";
  if(path){std::ofstream file(path);file<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return failures?1:0;
}
}
namespace cutum
{
void ObserveTextureCreated(unsigned int name)noexcept{texture_publication_fixture::Record(name);}
void ObserveTextureMutation(unsigned int name)noexcept
{
  using namespace texture_publication_fixture;
  Mutation();
  if (RefuseSecondUpload && CreatedCount == 2)
    glBindTexture(0, 0);
  if (auto *observer = ObserveLateUpdate)
    observer->Mutating(name);
}
void ObserveTextureUploaded(unsigned int name)noexcept
{
  if (auto *observer = texture_publication_fixture::ObservePeak)
    observer->Uploaded(name);
}
}
int main(int argc,char **argv)
{
  if(!glfwInit())return 77;glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
  GLFWwindow *window=glfwCreateWindow(32,32,"Texture publication verification",nullptr,nullptr);if(!window){glfwTerminate();return 77;}glfwMakeContextCurrent(window);glewExperimental=GL_TRUE;if(glewInit()!=GLEW_OK){glfwDestroyWindow(window);glfwTerminate();return 77;}while(glGetError()!=GL_NO_ERROR){}
  glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ALIGNMENT,1);int result=1;
  try{
    if(argc>1 && std::string(argv[1])=="--baseline")
      result=texture_publication_fixture::Baseline(argc>2?argv[2]:nullptr);
    else if(argc>1 && std::string(argv[1])=="--gpu-peak")
      result=texture_publication_fixture::GpuPeak(argc>2?argv[2]:nullptr);
    else if(argc>1 && std::string(argv[1])=="--late-in-place")
      result=texture_publication_fixture::LateInPlace(argc>2?argv[2]:nullptr);
    else
      result=texture_publication_fixture::Accepted(argc>1?argv[1]:nullptr);
  }catch(const std::exception &e){std::cerr<<e.what()<<std::endl;}
  glfwDestroyWindow(window);glfwTerminate();return result;
}

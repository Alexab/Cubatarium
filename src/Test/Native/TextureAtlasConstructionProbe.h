#pragma once
#include "Render/GlIncludes.h"
#include "Render/Textures/TextureCube.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "Core/Memory/StbiAllocation.h"
#include "Core/Memory/StbiSourceEnvelope.h"
#include "Test/StbiAllocationFixture.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace cutum
{
// Real desktop GL upload/update/replacement, checked against authored source
// pixels rather than the CPU atlas builder. This does not verify framebuffer
// consumers, other backends or overall GPU memory budgets.
inline nlohmann::json RunTextureAtlasConstructionProbe(const std::filesystem::path &root)
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  return {{"status","UNTESTED"},{"reason","desktop_GL_texture_readback_required"}};
#else
  struct State
  {
    std::array<GLenum,12> Names{GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,GL_PACK_SKIP_PIXELS,
        GL_PACK_IMAGE_HEIGHT,GL_PACK_SKIP_IMAGES,GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,
        GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS,GL_UNPACK_IMAGE_HEIGHT,GL_UNPACK_SKIP_IMAGES};
    std::array<GLint,12> Values{};
    GLint Texture{},Pack{},Unpack{};
    State() {
      glGetIntegerv(GL_TEXTURE_BINDING_2D,&Texture);
      glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&Pack);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&Unpack);
      glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
      for(size_t i=0;i<Names.size();++i) {
        glGetIntegerv(Names[i],&Values[i]);
        glPixelStorei(Names[i],Names[i]==GL_PACK_ALIGNMENT || Names[i]==GL_UNPACK_ALIGNMENT ? 1 : 0);
      }
    }
    ~State() {
      for(size_t i=0;i<Names.size();++i) glPixelStorei(Names[i],Values[i]);
      glBindBuffer(GL_PIXEL_PACK_BUFFER,Pack);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,Unpack);
      glBindTexture(GL_TEXTURE_2D,Texture);
    }
  } state;
  const auto require=[](bool passed,const char *message) {if(!passed) throw std::runtime_error(message);};
  auto bases=std::make_shared<UTextureBaseStorage>();
  UTextureCubeStorage storage(bases);
  struct Cleanup {UTextureCubeStorage &Storage;~Cleanup(){Storage.Clear();}} cleanup{storage};
  MergedCubeDesc descriptor;descriptor.Name="_qa_atlas_construction";descriptor.Id=60000;
  const auto pixel=[](int stage,int frame,int face,int x,int y) {
    return std::array<unsigned char,4>{static_cast<unsigned char>(1+stage*17+frame*5+face),
        static_cast<unsigned char>(11+x*29),static_cast<unsigned char>(19+y*37),255};
  };
  const auto author=[&](int stage,int width,int frameHeight,int frames) {
    for(int face=0;face<6;++face) {
      const auto name="_qa_atlas_face_"+std::to_string(face);descriptor.Stems[face]=name;
      TexturePixelData source;source.Width=width;source.Height=frameHeight*frames;
      for(int frame=0;frame<frames;++frame) for(int y=0;y<frameHeight;++y) for(int x=0;x<width;++x) {
        const auto value=pixel(stage,frame,face,x,y);source.Rgba.insert(source.Rgba.end(),value.begin(),value.end());
      }
      bases->RegisterPixels(name,std::move(source));
    }
    descriptor.AnimFrames=frames;
  };
  const auto matches=[&](const std::vector<unsigned char> &data,int stage,int width,int height,int frames) {
    if(data.size()!=static_cast<size_t>(width*6*height*frames*4)) return false;
    for(int frame=0;frame<frames;++frame) for(int face=0;face<6;++face) for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
      const auto expected=pixel(stage,frame,face,x,y);
      const size_t offset=((frame*height+y)*width*6+face*width+x)*4;
      for(size_t c=0;c<4;++c) if(data[offset+c]!=expected[c]) return false;
    }
    return true;
  };
  const auto read=[&](BlockId id,int stage,int width,int height,int frames) {
    const auto &cube=storage.GetTextures().at(id);
    require(cube.GetNumTextureFrames()==static_cast<size_t>(frames),"native atlas frame metadata differs from source");
    glBindTexture(GL_TEXTURE_2D,cube.GetTexture());
    GLint w{},h{};glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);
    require(w==width*6 && h==height*frames,"native atlas GL dimensions differ from source");
    std::vector<unsigned char> data(static_cast<size_t>(w)*h*4);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,data.data());
    require(matches(data,stage,width,height,frames),"native atlas GPU pixels differ from independent source oracle");
    return data;
  };
  author(0,2,2,1);storage.PatchDescriptors({descriptor});auto data=read(descriptor.Id,0,2,2,1);
  const GLuint first=storage.GetTextures().at(descriptor.Id).GetTexture();
  author(1,2,2,1);storage.PatchDescriptors({descriptor});read(descriptor.Id,1,2,2,1);
  require(first==storage.GetTextures().at(descriptor.Id).GetTexture(),"same-size native patch did not reuse texture");
  author(2,3,2,1);storage.PatchDescriptors({descriptor});read(descriptor.Id,2,3,2,1);
  author(3,3,2,2);storage.PatchDescriptors({descriptor});read(descriptor.Id,3,3,2,2);
  const GLuint animated=storage.GetTextures().at(descriptor.Id).GetTexture();
  author(4,3,1,4);storage.PatchDescriptors({descriptor});read(descriptor.Id,4,3,1,4);
  require(animated==storage.GetTextures().at(descriptor.Id).GetTexture(),"same-size frame metadata patch replaced texture");
  TexturePixelData invalid;invalid.Width=2;invalid.Height=2;invalid.Rgba.resize(3);
  bases->RegisterPixels(descriptor.Stems[0],std::move(invalid));storage.PatchDescriptors({descriptor});read(descriptor.Id,4,3,1,4);
  // Exercise the legacy list loader and the same upload implementation for
  // ordinary multi-frame textures (twelve face names, not vertical strips).
  nlohmann::json names=nlohmann::json::array();
  for(int frame=0;frame<2;++frame) for(int face=0;face<6;++face) {
    const auto name="_qa_atlas_explicit_"+std::to_string(frame)+"_"+std::to_string(face);names.push_back(name);
    TexturePixelData source;source.Width=2;source.Height=2;
    for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
      const auto value=pixel(5,frame,face,x,y);source.Rgba.insert(source.Rgba.end(),value.begin(),value.end());
    }
    bases->RegisterPixels(name,std::move(source));
  }
  std::filesystem::create_directories(root);
  {std::ofstream file(root/"cube.json",std::ios::binary);file << "\xEF\xBB\xBF\r\n" << nlohmann::json{{"name","_qa_atlas_explicit"},{"id",60001},{"textures",names}};}
  {std::ofstream file(root/"invalid.json");file << R"({"name":"_qa_bad_atlas","id":60002,"textures":null})";}
  storage.Load(root.string());read(60001,5,2,2,2);
  require(storage.GetTextures().size()==2,"legacy atlas loader installed invalid texture JSON");
  const auto encoded=test::MakeStbiAllocationPng();
  const auto imagePath=root/"decoder.png";
  {std::ofstream file(imagePath,std::ios::binary);file.write(reinterpret_cast<const char *>(encoded.data()),encoded.size());}
  bases->Register("_qa_decoded_png",imagePath.string());
  MergedCubeDesc decoded;decoded.Name="_qa_decoded_cube";decoded.Id=60003;decoded.Stems.fill("_qa_decoded_png");
  const auto decoder=std::make_shared<UStbiAllocationLedger>();
  {UStbiAllocationScope active(decoder);storage.PatchDescriptors({decoded});}
  const auto decoderCounts=decoder->GetSnapshot();
  const auto decoderLayout=GetStbiDecoderLayout();
  StbiSourceDomain pngDomain;pngDomain.width=pngDomain.height=64;
  pngDomain.png_idat_capacity_upper=16384;pngDomain.png_expanded_capacity_upper=64*64*3+64;
  const auto decoderEnvelope=BoundStbiOrdinarySource(pngDomain);
  require(decoderEnvelope.supported && !decoderEnvelope.overflow &&
      decoderCounts.peak_admitted_request_bytes<=decoderEnvelope.admitted_peak_upper,
      "native PNG decode requests exceed declared source envelope");
  require(decoderCounts.reallocations>=2 && decoderCounts.live_request_bytes==0 && decoderCounts.reserved_request_bytes==0,
      "native decoded atlas batch left STBI allocations/reservations behind");
  glBindTexture(GL_TEXTURE_2D,storage.GetTextures().at(decoded.Id).GetTexture());
  GLint decodedWidth{},decodedHeight{};
  glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&decodedWidth);
  glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&decodedHeight);
  require(decodedWidth==384 && decodedHeight==64,"native decoded atlas dimensions differ from PNG source");
  std::vector<unsigned char> decodedPixels(384*64*4);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,decodedPixels.data());
  for(int face=0;face<6;++face) for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
    const auto expected=test::StbiFixtureRgb(x,y);const size_t offset=(y*384+face*64+x)*4;
    require(decodedPixels[offset+3]==255,"native decoded PNG alpha differs from source");
    for(size_t c=0;c<3;++c) require(decodedPixels[offset+c]==expected[c],"native decoded PNG atlas differs from independent source");
  }
  data[0]^=1;require(!matches(data,0,2,2,1),"native atlas independent oracle accepted changed pixel");
  return {{"schema","texture-atlas-construction-native.v1"},{"status","PASS"},
      {"real_GL_readbacks",8},{"same_size_reused",true},{"dimension_replacement",true},
      {"same_size_frame_metadata_updated",true},{"invalid_replacement_retained",true},
      {"explicit_list_frames",2},{"negative_pixel_control",true},
      {"legacy_loader_bom_crlf",true},{"invalid_legacy_descriptor_skipped",true},
      {"stbi_decode_allocation",{{"header_bytes",StbiAllocationHeaderBytes()},
          {"successful_allocations",decoderCounts.successful_allocations},{"reallocations",decoderCounts.reallocations},
          {"peak_admitted_request_bytes",decoderCounts.peak_admitted_request_bytes},
          {"peak_live_request_bytes",decoderCounts.peak_live_request_bytes},
          {"source_peak_upper",decoderEnvelope.admitted_peak_upper},{"compiled_format_mask",decoderLayout.format_mask},
          {"jpeg_heap_state",decoderLayout.jpeg_heap_state},{"gif_info_heap_state",decoderLayout.gif_info_heap_state},
          {"context_inline",decoderLayout.context_inline},{"png_inline",decoderLayout.png_inline},
          {"zlib_inline",decoderLayout.zlib_inline},{"gif_inline",decoderLayout.gif_inline},
          {"final_live_request_bytes",decoderCounts.live_request_bytes},{"readback_vector_capacity",decodedPixels.capacity()},
          {"scope","STBI requested payload+prefix only; atlas readback buffer is separate,not a process/GPU cap"}}},
      {"scope","Isolated real desktop GL texture transfers, not framebuffer/backend/performance/GPU-memory acceptance"}};
#endif
}
} // namespace cutum

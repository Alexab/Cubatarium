#pragma once
#include "Render/GlIncludes.h"
#include "Render/Textures/TextureCube.h"
#include "ResourcePacks/BlockMergeRegistry.h"
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
  data[0]^=1;require(!matches(data,0,2,2,1),"native atlas independent oracle accepted changed pixel");
  return {{"schema","texture-atlas-construction-native.v1"},{"status","PASS"},
      {"real_GL_readbacks",7},{"same_size_reused",true},{"dimension_replacement",true},
      {"same_size_frame_metadata_updated",true},{"invalid_replacement_retained",true},
      {"explicit_list_frames",2},{"negative_pixel_control",true},
      {"legacy_loader_bom_crlf",true},{"invalid_legacy_descriptor_skipped",true},
      {"scope","Isolated real desktop GL texture transfers, not framebuffer/backend/performance/GPU-memory acceptance"}};
#endif
}
} // namespace cutum

// Exact float-bit palette/root comparison over every packaged glTF clip.
#include "Creatures/Visual/Gltf/CreatureGltfLoader.h"
#include "Creatures/Visual/Gltf/CreatureGltfBonePalette.h"
#include "Creatures/Visual/Gltf/CreatureGltfAnimPlayer.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

#ifndef NDEBUG
#error Release required
#endif

namespace cutum
{
void LinearComputeGltfSkinMatricesInto(const CreatureGltfMeshAsset &, const GltfAnimationCpu *, float,
  const GltfAnimationCpu *, float, float, GltfSkinScratch &, std::vector<glm::mat4> &);
glm::mat4 LinearSampleGltfRootTransform(const GltfAnimationCpu &, float, bool);
}
using namespace cutum;
using Json = nlohmann::json;
namespace fs = std::filesystem;

static bool Same(const glm::mat4 &a,const glm::mat4 &b)
{
  for(int c=0;c<4;++c) for(int r=0;r<4;++r)
    if(std::memcmp(&a[c][r],&b[c][r],sizeof(float))) return false;
  return true;
}

int main(int argc,char **argv)
{
  try
  {
    if(argc<3) throw std::runtime_error("usage: equivalence OUTPUT.json CREATURES_ROOT [EXTRA.gltf ...]");
    std::vector<fs::path> files;
    for(const auto &entry:fs::directory_iterator(argv[2]))
    {
      const auto definition=entry.path()/"creature.json";
      if(!entry.is_directory()||!fs::is_regular_file(definition)) continue;
      std::ifstream in(definition);Json def;in>>def;
      if(def["visual"].value("backend","")=="gltf_skeleton")
        files.push_back(entry.path()/def["visual"]["gltf"].value("model","model.gltf"));
    }
    if(files.size()!=35) throw std::runtime_error("Expected35 packaged glTF species");
    std::sort(files.begin(),files.end());
    for(int i=3;i<argc;++i) files.emplace_back(argv[i]);
    Json report={{"scope","Actual loaded assets, all clips, exact/adjacent keys and crossfades; float-bit equivalence to original linear lookup, not world/terrain/GPU/performance acceptance"},
      {"assets",Json::array()},{"palette_checks",0},{"root_checks",0}};
    uint64_t totalPalettes=0,totalRoots=0;
    for(const auto &path:files)
    {
      const auto asset=CreatureGltfLoader::LoadFromFile(path.string());
      if(!asset||!asset->loaded||!asset->hasSkin) throw std::runtime_error("Load failed: "+path.string());
      GltfSkinScratch oldScratch,newScratch;std::vector<glm::mat4> before,after;
      uint64_t palettes=0,roots=0;
      const auto pose=[&](const GltfAnimationCpu *clip,float time,const GltfAnimationCpu *fade,float fadeTime,float weight)
      {
        LinearComputeGltfSkinMatricesInto(*asset,clip,time,fade,fadeTime,weight,oldScratch,before);
        ComputeGltfSkinMatricesInto(*asset,clip,time,fade,fadeTime,weight,newScratch,after);
        if(before.size()!=after.size()) throw std::runtime_error("Palette size mismatch: "+path.string());
        for(size_t i=0;i<before.size();++i) if(!Same(before[i],after[i]))
          throw std::runtime_error("Palette float mismatch: "+path.string()+" time="+std::to_string(time));
        ++palettes;
      };
      pose(nullptr,0,nullptr,0,0);
      for(const auto &clip:asset->animations)
      {
        const float duration=GltfAnimationDuration(clip);
        std::vector<float> times{-.01f,duration+.01f};
        for(int i=0;i<=256;++i) times.push_back(duration*static_cast<float>(i)/256.f);
        for(const auto &channel:clip.channels) for(float time:channel.keyTimes)
        {
          times.push_back(time);
          times.push_back(std::nextafter(time,-std::numeric_limits<float>::infinity()));
          times.push_back(std::nextafter(time,std::numeric_limits<float>::infinity()));
        }
        std::sort(times.begin(),times.end());times.erase(std::unique(times.begin(),times.end()),times.end());
        for(float time:times)
        {
          pose(&clip,time,nullptr,0,0);
          for(bool loop:{false,true})
          {
            if(!Same(LinearSampleGltfRootTransform(clip,time,loop),SampleGltfRootTransform(clip,time,loop)))
              throw std::runtime_error("Root float mismatch: "+path.string());
            ++roots;
          }
        }
        for(const auto &fade:asset->animations)
          for(int phase=0;phase<=16;++phase)
            for(float weight:{0.f,.001f,.00101f,.25f,.5f,1.f})
              pose(&clip,duration*phase/16.f,&fade,GltfAnimationDuration(fade)*(16-phase)/16.f,weight);
        pose(nullptr,0,&clip,duration*.37f,.5f);
      }
      report["assets"].push_back({{"path",path.string()},{"clips",asset->animations.size()},
        {"palette_checks",palettes},{"root_checks",roots},{"exact_float_bits",true}});
      totalPalettes+=palettes;totalRoots+=roots;
      std::cout<<path.parent_path().filename().string()<<" exact palettes="<<palettes<<" roots="<<roots<<'\n';
    }
    report["palette_checks"]=totalPalettes;report["root_checks"]=totalRoots;report["passed"]=true;
    std::ofstream out(argv[1]);out<<report.dump(2)<<'\n';if(!out)throw std::runtime_error("Write failed");
  }
  catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}

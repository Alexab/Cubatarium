// Research evidence only: real loader, real local-TRS skinning, no art approval.
#include "Creatures/Visual/Gltf/CreatureGltfLoader.h"
#include "Creatures/Visual/Gltf/CreatureGltfBonePalette.h"
#include "Creatures/Visual/Gltf/CreatureGltfAnimPlayer.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace cutum;
int main(int argc,char **argv)
{
  if(argc!=3){std::cerr<<"usage: creature_gltf_render_envelope_audit <creatures-root> <report.json>\n";return 2;}
  using Json=nlohmann::json;
  Json report={{"schema","creature-gltf-render-envelope-audit.v1"},{"passed",true},
    {"scope","Real CPU loader and skinning all asset clips33 phases plus cold-entry clip-pair fades; no GPU, terrain, world performance or art acceptance"},
    {"resources",Json::array()}};
  size_t total=0;bool passed=true;
  for(const auto &entry:std::filesystem::directory_iterator(argv[1]))
  {
    const auto definition=entry.path()/"creature.json";
    if(!std::filesystem::is_regular_file(definition))continue;
    Json def;std::ifstream(definition)>>def;
    if(def["visual"].value("backend","")!="gltf_skeleton")continue;
    const std::string file=def["visual"]["gltf"].value("model","model.gltf");
    const auto start=std::chrono::steady_clock::now();
    const auto asset=CreatureGltfLoader::LoadFromFile((entry.path()/file).string());
    if(!asset){std::cerr<<"missing asset "<<entry.path()<<'\n';return 1;}
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    double observed=0.;size_t poses=0;bool covered=std::isfinite(asset->conservativeRenderRadius);
    const auto sample=[&](const GltfAnimationCpu *animation,float t,const GltfAnimationCpu *fade,float outgoing,float weight)
    {
      std::vector<glm::mat4> palette;
      if(asset->hasSkin)palette=ComputeGltfSkinMatricesBlended(*asset,animation,t,fade,outgoing,weight);
      glm::mat4 root(1.f);
      if(!asset->hasSkin && animation)
      {
        root=SampleGltfRootTransform(*animation,t,false);
        if(fade)root=SampleGltfRootTransform(*fade,outgoing,false)*(1.f-weight)+root*weight;
      }
      for(const auto &p:asset->primitives)
      {
        const auto &v=p.mesh.interleavedPosUv;
        for(size_t i=0;i+4<v.size();i+=5)
        {
          const glm::vec4 point(v[i],v[i+1],v[i+2],1.f);glm::vec4 posed=root*point;
          if(p.skinned && asset->hasSkin)
          {
            posed=glm::vec4(0.f);
            for(int k=0;k<4;++k)posed+=palette.at(p.jointIndices.at(i/5*4+k))*point*p.jointWeights.at(i/5*4+k);
          }
          const double radius=glm::length(glm::dvec3(posed));observed=std::max(observed,radius);
          if(radius>asset->conservativeRenderRadius+.000001)covered=false;
        }
      }
      ++poses;
    };
    sample(nullptr,0,nullptr,0,0);
    for(const auto &clip:asset->animations)for(int i=0;i<=32;++i)
      sample(&clip,GltfAnimationDuration(clip)*i/32.f,nullptr,0,0);
    for(const auto &from:asset->animations)for(const auto &to:asset->animations)
    {
      if(&from==&to)continue;
      for(int phase=0;phase<16;++phase)for(int step=0;step<=10;++step)
        sample(&to,std::min(.015f*step,GltfAnimationDuration(to)),&from,
          GltfAnimationDuration(from)*phase/16.f,step/10.f);
    }
    report["resources"].push_back({{"id",entry.path().filename().string()},{"covered",covered},
      {"radius",asset->conservativeRenderRadius},{"max_observed_radius",observed},
      {"radius_over_observed",asset->conservativeRenderRadius/std::max(observed,.00001)},
      {"load_and_bound_ms",elapsed},{"poses",poses}});
    passed&=covered;total+=poses;std::cout<<entry.path().filename().string()<<" "<<poses<<" "<<covered<<'\n';
  }
  report["passed"]=passed;report["poses"]=total;
  std::ofstream(argv[2])<<report.dump(2)<<'\n';return passed?0:1;
}

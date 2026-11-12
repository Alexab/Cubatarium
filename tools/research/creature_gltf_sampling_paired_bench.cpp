// Paired retained-asset CPU experiment. Not a world/GPU performance gate.
#include "Creatures/Visual/Gltf/CreatureGltfLoader.h"
#include "Creatures/Visual/Gltf/CreatureGltfBonePalette.h"
#include "Creatures/Visual/Gltf/CreatureGltfAnimPlayer.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

#ifndef NDEBUG
#error This research benchmark requires a Release build
#endif

namespace cutum
{
void LinearComputeGltfSkinMatricesInto(const CreatureGltfMeshAsset &, const GltfAnimationCpu *, float,
  const GltfAnimationCpu *, float, float, GltfSkinScratch &, std::vector<glm::mat4> &);
glm::mat4 LinearSampleGltfRootTransform(const GltfAnimationCpu &, float, bool);
}

using namespace cutum;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
static volatile float consumed = 0.f;

static Json Stats(std::vector<double> v)
{
  std::sort(v.begin(), v.end());
  return {{"min", v.front()}, {"p50", v[v.size()/2]}, {"p95", v[static_cast<size_t>(.95*(v.size()-1))]}, {"max", v.back()}};
}
static uint64_t Hash(uint64_t hash, const glm::mat4 &m)
{
  for (int c=0;c<4;++c) for(int r=0;r<4;++r)
  { uint32_t bits;std::memcpy(&bits,&m[c][r],4);hash^=bits;hash*=1099511628211ull; }
  return hash;
}

int main(int argc,char **argv)
{
  try
  {
    if(argc!=4)throw std::runtime_error("usage: paired OUTPUT.json BEFORE.gltf CURRENT.gltf");
    std::array<std::shared_ptr<CreatureGltfMeshAsset>,2> assets{
      CreatureGltfLoader::LoadFromFile(argv[2]),CreatureGltfLoader::LoadFromFile(argv[3])};
    for(const auto &a:assets)if(!a||!a->loaded||!a->hasSkin)throw std::runtime_error("Asset load failed");
    Json report={{"scope","Paired one-process retained-asset CPU batch averages, randomized mirrored order; no individual frame p95/GPU/world acceptance"},
      {"compiler",_MSC_FULL_VER},{"cases",Json::array()},{"warm_loader_ms",Json::array()}};
    std::array<std::vector<double>,2> loadTimes;
    for(int batch=0;batch<21;++batch)for(int slot=0;slot<2;++slot)
    {
      const int which=(batch+slot)%2;
      const auto start=Clock::now();auto a=CreatureGltfLoader::LoadFromFile(argv[2+which]);
      loadTimes[which].push_back(std::chrono::duration<double,std::milli>(Clock::now()-start).count());
      if(!a||!a->loaded)throw std::runtime_error("Warm reload failed");
    }
    for(int i=0;i<2;++i)report["warm_loader_ms"].push_back({{"asset",i==0?"before":"current"},{"stats",Stats(loadTimes[i])}});
    using PoseFn=decltype(&ComputeGltfSkinMatricesInto);
    const std::array<PoseFn,2> functions{LinearComputeGltfSkinMatricesInto,ComputeGltfSkinMatricesInto};
    uint32_t random=123456789;
    for(const std::string name:{"walk","run"})for(bool fade:{false,true})
    {
      std::array<GltfSkinScratch,4> scratch;std::array<std::vector<glm::mat4>,4> palettes;
      std::array<const GltfAnimationCpu *,2> clips,idle;
      std::array<float,2> duration,idleDuration;
      for(int a=0;a<2;++a)
      {
        clips[a]=&assets[a]->animations.at(assets[a]->animationIndexByName.at(name));
        idle[a]=&assets[a]->animations.at(assets[a]->animationIndexByName.at("idle"));
        duration[a]=GltfAnimationDuration(*clips[a]);idleDuration[a]=GltfAnimationDuration(*idle[a]);
      }
      const auto compute=[&](int condition,float phase)
      {
        const int a=condition/2,implementation=condition%2;
        functions[implementation](*assets[a],clips[a],phase*duration[a],fade?idle[a]:nullptr,
          phase*idleDuration[a],fade?.5f:0.f,scratch[condition],palettes[condition]);
        consumed=consumed+palettes[condition].front()[3].x*1e-9f;
      };
      for(int i=0;i<1024;++i)for(int c=0;c<4;++c)compute(c,static_cast<float>(i%997)/997.f);
      std::array<std::vector<double>,4> timings;
      constexpr size_t count=1024,batches=81;
      for(size_t batch=0;batch<batches;++batch)
      {
        std::array<int,4> order{0,1,2,3};
        for(int i=3;i>0;--i){random=random*1664525u+1013904223u;std::swap(order[i],order[random%(i+1)]);}
        std::array<double,4> elapsed{};
        for(int slot=0;slot<8;++slot)
        {
          const int c=order[slot<4?slot:7-slot];
          const auto begin=Clock::now();
          for(size_t i=0;i<count;++i)compute(c,static_cast<float>(((batch*count+i)*73+19)%997)/997.f);
          elapsed[c]+=std::chrono::duration<double,std::micro>(Clock::now()-begin).count();
        }
        for(int c=0;c<4;++c)timings[c].push_back(elapsed[c]/(2*count));
      }
      std::array<uint64_t,4> signatures;signatures.fill(1469598103934665603ull);
      for(int i=0;i<=1024;++i)for(int c=0;c<4;++c)
      { compute(c,static_cast<float>(i)/1024.f);for(const auto &m:palettes[c])signatures[c]=Hash(signatures[c],m); }
      if(signatures[0]!=signatures[1]||signatures[2]!=signatures[3])throw std::runtime_error("Palette signature mismatch");
      Json row={{"clip",name},{"fade",fade},{"pose_signatures",signatures},{"conditions",Json::array()}};
      for(int c=0;c<4;++c)row["conditions"].push_back({{"asset",c/2==0?"before":"current"},
        {"implementation",c%2==0?"linear":"indexed"},{"batch_average_us_per_pose",Stats(timings[c])},{"raw_batches_us_per_pose",timings[c]}});
      std::vector<double> currentRatio,oldRatio,combinedRatio;
      for(size_t b=0;b<batches;++b)
      { currentRatio.push_back(timings[3][b]/timings[2][b]);oldRatio.push_back(timings[1][b]/timings[0][b]);combinedRatio.push_back(timings[3][b]/timings[0][b]); }
      row["indexed_over_linear_current"]=Stats(currentRatio);
      row["indexed_over_linear_before"]=Stats(oldRatio);
      row["indexed_current_over_linear_before"]=Stats(combinedRatio);
      report["cases"].push_back(row);
    }
    for(const auto &asset:assets)for(const auto &animation:asset->animations)
    {
      const float duration=GltfAnimationDuration(animation);
      for(int i=0;i<=512;++i)
        if(Hash(0,LinearSampleGltfRootTransform(animation,duration*i/512.f,false))!=
           Hash(0,SampleGltfRootTransform(animation,duration*i/512.f,false)))throw std::runtime_error("Root signature mismatch");
    }
    report["palette_and_root_signatures_bit_exact"]=true;
    std::ofstream out(argv[1]);out<<report.dump(2)<<'\n';if(!out)throw std::runtime_error("Report write failed");
    std::cout<<"paired CPU cases=4, matrices exact, consumed="<<consumed<<'\n';
  }
  catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Test/OrdinaryNewObservation.h"
#include "WorldGen/Core/Noise.h"
#include <nlohmann/json.hpp>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

namespace
{
using Json=nlohmann::json;
uint64_t Digest()
{
  uint64_t hash=1469598103934665603ull;
  const auto add=[&](float value){uint32_t bits;std::memcpy(&bits,&value,sizeof(bits));for(int i=0;i<4;++i){hash^=(bits>>(i*8))&255;hash*=1099511628211ull;}};
  const uint32_t seeds[]={0,1,42,12345,0x9e3779b9u,0xdeadbeefu,0xffffffffu};
  for(int round=0;round<3;++round){
    for(uint32_t n=0;n<4099;++n)add(cutum::Perlin2D(0.125f,-0.375f,n+uint32_t(round)*100003u));
    for(auto seed:seeds)for(int n=0;n<9;++n){
      const float x=n*13.125f-49.875f,y=n*0.1875f-1.25f,z=31.75f-n*19.375f;
      add(cutum::Perlin2D(x,z,seed));add(cutum::Perlin3D(x,y,z,seed));
      add(cutum::FBM2D(x,z,seed,5,0.55f,1.9f));add(cutum::FBM3D(x,y,z,seed,4,0.5f,2.0f));
      add(cutum::NormalizedFBM2D(x,z,seed,6,0.6f,2.1f));add(cutum::SmoothFBM2D(x,z,seed,4,0.5f,2.0f));
    }
  }
  return hash;
}
}
int main(int argc,char **argv)
{
  const bool baseline=argc>1 && std::string(argv[1])=="--baseline";
  const auto path=argc>(baseline?2:1)?argv[baseline?2:1]:"noise-storage-source.json";
  constexpr uint64_t golden=14369016571923994047ull; // original0a8278396 Release binary
  uint64_t digest{};
  cutum::NoisePermutationCacheStorage boundary[4];size_t measured[4]{},peak{};
  std::thread worker([&]{
    observation::Scope scope;
    size_t at=0;
    for(uint32_t n=0;n<4097;++n){
      (void)cutum::Perlin2D(0.125f,-0.375f,n);
      if(n==0 || n==2047 || n==2048 || n==4096){boundary[at]=cutum::GetNoisePermutationCacheStorage();measured[at++]=observation::State.live;}
    }
    digest=Digest();peak=observation::State.peak;
  });worker.join();
  size_t failed=0;
  Json report={{"schema","noise-storage-source.v1"},{"status",baseline?"BASELINE":"PASS"},{"digest",digest},
    {"golden_digest",golden},{"negative_digest_control",(digest^1)!=golden},{"boundaries",Json::array()},{"refusal_cases",Json::array()}};
  if(digest!=golden || observation::State.live || observation::State.blocks)++failed;
  for(size_t i=0;i<4;++i){const auto &s=boundary[i];
    const bool ok=s.supported && !s.overflow && measured[i]==s.retained_requests && s.retained_requests<=s.retained_upper && peak<=s.peak_upper;
    if(!ok)++failed;
    report["boundaries"].push_back({{"entries",s.entries},{"buckets",s.buckets},{"map_inline",s.map_inline},{"node_bytes",s.node_bytes},
      {"measured",measured[i]},{"source_retained",s.retained_requests},{"retained_upper",s.retained_upper},{"peak_upper",s.peak_upper},{"pass",ok}});
  }
  struct Case {size_t before{},delta{},after_failure{},retained{},blocks{},peak{};float value{};bool threw{};cutum::NoisePermutationCacheStorage source;};
  const auto run=[](size_t warm,size_t prefix){
    Case result;
    std::thread t([&]{
      observation::Scope scope;
      for(uint32_t n=0;n<warm;++n)(void)cutum::Perlin2D(0.125f,-0.375f,n);
      result.before=observation::State.attempts;
      observation::State.fail_after=prefix==SIZE_MAX?SIZE_MAX:result.before+prefix;
      try{result.value=cutum::Perlin2D(0.125f,-0.375f,static_cast<uint32_t>(warm));}catch(const std::bad_alloc &){result.threw=true;}
      result.delta=observation::State.attempts-result.before;result.after_failure=observation::State.live;
      observation::State.fail_after=SIZE_MAX;
      result.value=cutum::Perlin2D(0.125f,-0.375f,static_cast<uint32_t>(warm));
      result.source=cutum::GetNoisePermutationCacheStorage();result.retained=observation::State.live;
      result.blocks=observation::State.blocks;result.peak=observation::State.peak;
    });t.join();return result;
  };
  for(size_t warm:{size_t{0},size_t{8},size_t{64},size_t{512},size_t{1024},size_t{2048}}){
    const auto normal=run(warm,SIZE_MAX);
    if(normal.threw || observation::State.live || observation::State.blocks)++failed;
    for(size_t prefix=0;prefix<normal.delta;++prefix){const auto c=run(warm,prefix);
      const bool ok=c.threw && c.value==normal.value && c.source.supported && c.retained==c.source.retained_requests &&
        c.after_failure<=c.source.peak_upper && c.peak<=c.source.peak_upper && observation::State.live==0 && observation::State.blocks==0;
      if(!ok)++failed;
      report["refusal_cases"].push_back({{"warm",warm},{"prefix",prefix},{"threw",c.threw},{"after_failure",c.after_failure},
        {"retry_retained",c.retained},{"source_retained",c.source.retained_requests},{"final0",observation::State.live==0&&observation::State.blocks==0},{"pass",ok}});
    }
  }
  report["observed_peak"]=peak;report["failed_cases"]=failed;report["final_bytes"]=observation::State.live;report["final_blocks"]=observation::State.blocks;
  report["status"]=failed?"FAIL":"PASS";
  std::ofstream out(path);out<<report.dump(2)<<'\n';std::cout<<report.dump()<<'\n';return out && !failed?0:1;
}

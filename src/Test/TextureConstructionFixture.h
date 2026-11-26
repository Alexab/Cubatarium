#pragma once
#include "Test/CatalogBoundProfileFixture.h"
#include "Test/OrdinaryNewObservation.h"
#include "WorldGen/Core/CatalogConstructionComposition.h"
#include "WorldGen/Core/TextureConstructionComposition.h"
#include "Test/StbiAllocationFixture.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <map>
namespace texture_construction_fixture
{
using namespace cutum;using Json=nlohmann::json;namespace fs=std::filesystem;
inline Json Bases(const UTextureBaseStorage &b){Json v=Json::object();for(const auto &p:b.GetBaseTextures()){v[p.first]={{"file",p.second.GetFileName()},{"pixels",p.second.HasPixelData()}};if(const auto *px=p.second.GetPixelData())v[p.first]["payload"]={{"w",px->Width},{"h",px->Height},{"rgba",px->Rgba}};}return v;}
inline int Baseline(const char *path)
{
  const auto root=fs::path("build/desktop-msvc/s4-texture-construction-fixtures/baseline");fs::create_directories(root);
  for(const auto *name:{"a.png","b.png","c.png"}){std::ofstream f(root/name);f<<"registration only";}
  const auto input=root.string();bool changed=false;size_t prefix=SIZE_MAX;
  for(size_t i=0;i<128 && !changed;++i){auto out=std::make_unique<UTextureBaseStorage>();out->Register("old","previous");const auto before=Bases(*out);bool refused=false;
    {observation::Scope scope(i);try{out->Load(input);}catch(const std::bad_alloc &){refused=true;}}
    if(refused && Bases(*out)!=before){changed=true;prefix=i;}out.reset();if(observation::State.live || observation::State.blocks)return 1;
  }
  Json report={{"schema","legacy-texture-registration-baseline.v1"},{"status",changed?"REPRODUCED":"FAIL"},{"prefix",prefix},{"prior_target_changed",changed}};
  if(path){std::ofstream f(path);f<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return changed?0:1;
}
struct Image {int w{},h{};std::vector<unsigned char> rgba;};
inline Image Pixels(int w,int h,int stage)
{Image p{w,h,{}};for(int y=0;y<h;++y)for(int x=0;x<w;++x){p.rgba.push_back(static_cast<unsigned char>(stage+3*x+7*y));p.rgba.push_back(static_cast<unsigned char>(29+x));p.rgba.push_back(static_cast<unsigned char>(71+y));p.rgba.push_back(255);}return p;}
inline void Ppm(const fs::path &path,const Image &p)
{fs::create_directories(path.parent_path());std::ofstream f(path,std::ios::binary);f<<"P6\n"<<p.w<<' '<<p.h<<"\n255\n";for(size_t i=0;i<p.rgba.size();i+=4)f.write(reinterpret_cast<const char *>(p.rgba.data()+i),3);}
struct Inputs
{
  fs::path root,registration;std::string registrationPath,descriptorPath;
  std::shared_ptr<UTextureBaseStorage> bases;
  std::map<std::string,Image> golden;
  std::map<std::string,StbiSourceDomain> domains;
  std::vector<std::string> mixed,explicitFrames,strips;
  Json descriptor;
};
inline Inputs Prepare()
{
  Inputs in;in.root="build/desktop-msvc/s4-texture-construction-fixtures/accepted";in.registration=in.root/"registrations";
  fs::create_directories(in.root);in.bases=std::make_shared<UTextureBaseStorage>();
  const auto memory=[&](const std::string &name,Image p){in.bases->RegisterPixels(name,{p.w,p.h,p.rgba});in.golden.emplace(name,std::move(p));};
  memory("memory",Pixels(2,2,53));memory("tiny",Pixels(1,1,93));
  const auto encoded=test::MakeStbiAllocationPng();const auto png=in.root/"image.png";
  {std::ofstream f(png,std::ios::binary);f.write(reinterpret_cast<const char *>(encoded.data()),encoded.size());}
  Image pngGold{64,64,{}};for(int y=0;y<64;++y)for(int x=0;x<64;++x){const auto p=test::StbiFixtureRgb(x,y);pngGold.rgba.insert(pngGold.rgba.end(),p.begin(),p.end());pngGold.rgba.push_back(255);}
  in.golden["png"]=std::move(pngGold);in.bases->Register("png",png.string());StbiSourceDomain pngDomain;pngDomain.width=pngDomain.height=64;pngDomain.png_idat_capacity_upper=16384;pngDomain.png_expanded_capacity_upper=64*64*3+64;in.domains["png"]=pngDomain;
  in.golden["file"]=Pixels(2,2,13);const auto ppm=in.root/"image.ppm";Ppm(ppm,in.golden.at("file"));in.bases->Register("file",ppm.string());
  StbiSourceDomain pnm;pnm.format=StbiSourceFormat::Pnm;pnm.width=pnm.height=2;in.domains["file"]=pnm;
  for(int frame=0;frame<2;++frame)for(int face=0;face<6;++face){const auto name="frame_"+std::to_string(frame)+"_"+std::to_string(face);memory(name,Pixels(2,2,21+30*frame+face));in.explicitFrames.push_back(name);
    if(frame==0){const auto strip="strip_"+std::to_string(face);Image image{2,4,{}};for(int f=0;f<2;++f){const auto piece=Pixels(2,2,21+30*f+face);image.rgba.insert(image.rgba.end(),piece.rgba.begin(),piece.rgba.end());}memory(strip,std::move(image));in.strips.push_back(strip);}}
  in.bases->RegisterPixels("short",{2,2,{1,2,3}});in.mixed={"file","memory","png","absent","tiny","file"};
  in.descriptor={{"name",std::string(101,'n')},{"id",548},{"textures",in.mixed},{"ignored",std::string(513,'i')}};
  in.descriptorPath=(in.root/"cube.json").string();{std::ofstream f(in.descriptorPath,std::ios::binary);f<<"\xEF\xBB\xBF\r\n"<<in.descriptor.dump();}
  Ppm(in.registration/"file.png",Pixels(3,2,113));Ppm(in.registration/"memory.png",Pixels(1,1,153));Ppm(in.registration/"uppercase.PNG",Pixels(2,2,1));fs::create_directories(in.registration/"directory.png");
  in.registrationPath=in.registration.string();return in;
}
struct BatchSourceInspector
{
  size_t entries{},buckets{};
  template<class T>void Map(const T &v){entries=v.size();buckets=v.bucket_count();}
  void Text(const std::string &){}
  void DecodedTexture(const std::shared_ptr<const unsigned char> &){}
};
inline CpuAtlasSourceDomain Domain(const Inputs &in,const std::vector<std::string> &names,int strip,bool batch,
    const UTextureCubeStorage::AtlasDecodeBatch *prior=nullptr)
{
  CpuAtlasSourceDomain d;d.face_references=names.size();d.strip_frames=strip;d.batch=batch;
  if(prior){BatchSourceInspector out;prior->InspectStorage(out);d.prior_cache_entries=out.entries;d.prior_cache_buckets=out.buckets;}
  if(!names.empty()){const auto first=in.golden.find(names.front());if(first!=in.golden.end()){d.first_width=first->second.w;d.first_height=first->second.h;}}
  for(const auto &name:names){const auto source=in.domains.find(name);if(source!=in.domains.end())AddCpuAtlasDecodedSource(d,source->second,in.bases->GetBaseTextures().at(name).GetFileName().size());}return d;
}
inline bool Matches(const UTextureCubeStorage::CubeAtlasPixels &atlas,const Inputs &in,
    const std::vector<std::string> &names,int width,int height,int frames,bool strip=false)
{
  if(!width)return atlas.Width==0 && atlas.Height==0 && atlas.Rgba.empty();
  if(atlas.Width!=width*6 || atlas.Height!=height*frames || atlas.Frames!=frames || atlas.Rgba.size()!=size_t(width*6*height*frames*4))return false;
  for(int f=0;f<frames;++f)for(int face=0;face<6;++face)for(int y=0;y<height;++y)for(int x=0;x<width;++x){
    const size_t index=strip?face:f*6+face;const Image *source=nullptr;if(index<names.size()){const auto it=in.golden.find(names[index]);if(it!=in.golden.end())source=&it->second;}
    const int sourceY=strip?f*height+y:y;const size_t offset=((f*height+y)*width*6+face*width+x)*4;
    for(int c=0;c<4;++c){const auto expected=source && x<source->w && sourceY<source->h?source->rgba[(sourceY*source->w+x)*4+c]:0;if(atlas.Rgba[offset+c]!=expected)return false;}
  }return true;
}
inline CatalogCandidateEnvelope RegistrationBound(const Inputs &in)
{size_t count=0,width=in.registration.native().size();for(const auto &p:fs::directory_iterator(in.registration)){++count;width=std::max(width,p.path().native().size());}return BoundLegacyBaseDirectorySource(count,width);}
inline bool Descriptor(const std::optional<LegacyCubeDescriptor> &v,const Inputs &in)
{return v && v->Name==std::string(101,'n') && v->Id==548 && v->Textures==in.mixed;}
inline int Run(const char *path,bool owner_binding=false)
{
  auto in=Prepare();Json report={{"schema","texture-source-construction-composition.v1"},{"atlas_cases",Json::array()},{"phases",Json::array()}};size_t failures=0,prefixes=0,decoderPrefixes=0;
  const auto registrationBound=RegistrationBound(in);auto target=std::make_unique<UTextureBaseStorage>();target->RegisterPixels("kept",{1,1,{3,4,5,6}});target->RegisterPixels("file",{1,1,{7,8,9,10}});
  {observation::Scope scope;target->Load(in.registrationPath);}const auto registrationNormal=observation::State;
  bool registrationOk=target->GetBaseTextures().size()==4 && !target->GetBaseTextures().count("uppercase") && target->GetBaseTextures().count("directory") &&
      !target->GetBaseTextures().at("file").HasPixelData() && target->GetBaseTextures().at("file").GetFileName()==(in.registration/"file.png").string() &&
      target->GetBaseTextures().at("kept").GetPixelData()->Rgba==std::vector<uint8_t>({3,4,5,6}) && registrationNormal.peak<=registrationBound.requested_heap_upper;
  target.reset();registrationOk=registrationOk && !observation::State.live && !observation::State.blocks;
  for(size_t i=0;registrationOk && i<registrationNormal.attempts;++i){target=std::make_unique<UTextureBaseStorage>();target->RegisterPixels("kept",{1,1,{3,4,5,6}});target->RegisterPixels("file",{1,1,{7,8,9,10}});const auto before=Bases(*target);bool threw=false;
    {observation::Scope scope(i);try{target->Load(in.registrationPath);}catch(const std::bad_alloc &){threw=true;}}
    const auto counts=observation::State;registrationOk=threw && counts.refusals && Bases(*target)==before && counts.peak<=registrationBound.requested_heap_upper && !counts.live;
    if(registrationOk){observation::Scope scope;target->Load(in.registrationPath);}target.reset();registrationOk=registrationOk && !observation::State.live && !observation::State.blocks;++prefixes;
  }
  if(!registrationOk)++failures;report["registration"]={{"pass",registrationOk},{"attempts",registrationNormal.attempts},{"peak",registrationNormal.peak},{"upper",registrationBound.requested_heap_upper}};
  const auto descriptorBound=BoundLegacyCubeFileSource(static_cast<size_t>(fs::file_size(in.descriptorPath)),fs::path(in.descriptorPath).native().size());
  std::optional<LegacyCubeDescriptor> descriptor;
  {observation::Scope scope;descriptor=LoadLegacyCubeDescriptorFile(in.descriptorPath);}const auto descriptorNormal=observation::State;
  bool descriptorOk=Descriptor(descriptor,in) && descriptorNormal.peak<=descriptorBound.requested_heap_upper;descriptor.reset();descriptorOk=descriptorOk && !observation::State.live && !observation::State.blocks;
  for(size_t i=0;descriptorOk && i<descriptorNormal.attempts;++i){bool threw=false;{observation::Scope scope(i);try{descriptor=LoadLegacyCubeDescriptorFile(in.descriptorPath);}catch(const std::bad_alloc &){threw=true;}}
    const auto counts=observation::State;descriptorOk=threw && counts.refusals && !descriptor && !counts.live && counts.peak<=descriptorBound.requested_heap_upper;
    if(descriptorOk){observation::Scope scope;descriptor=LoadLegacyCubeDescriptorFile(in.descriptorPath);}descriptorOk=descriptorOk && Descriptor(descriptor,in);descriptor.reset();descriptorOk=descriptorOk && !observation::State.live && !observation::State.blocks;++prefixes;
  }
  if(!descriptorOk)++failures;report["descriptor_file"]={{"pass",descriptorOk},{"attempts",descriptorNormal.attempts},{"peak",descriptorNormal.peak},{"upper",descriptorBound.requested_heap_upper}};
  report["invalid_descriptor_files"]=Json::array();
  for(int kind=0;kind<3;++kind){const auto file=in.root/("invalid_descriptor_"+std::to_string(kind)+".json");
    if(kind<2){std::ofstream f(file);f<<(kind==0?"{\"textures\":[":"{\"name\":\"bad\",\"id\":5,\"textures\":[null]}");}
    const auto input=kind==2?std::string{}:file.string();const auto bound=BoundLegacyCubeFileSource(kind==2?0:static_cast<size_t>(fs::file_size(file)),fs::path(input).native().size());
    {observation::Scope scope;descriptor=LoadLegacyCubeDescriptorFile(input);}const auto normal=observation::State;
    bool ok=!descriptor && !normal.live && normal.peak<=bound.requested_heap_upper;
    for(size_t i=0;ok && i<normal.attempts;++i){{observation::Scope scope(i);try{descriptor=LoadLegacyCubeDescriptorFile(input);}catch(const std::bad_alloc &){}}
      const auto refused=observation::State;ok=!descriptor && refused.refusals && !refused.live && !refused.blocks && refused.peak<=bound.requested_heap_upper;
      if(ok){observation::Scope scope;descriptor=LoadLegacyCubeDescriptorFile(input);}ok=ok && !descriptor && !observation::State.live && !observation::State.blocks;++prefixes;
    }
    if(!ok)++failures;report["invalid_descriptor_files"].push_back({{"kind",kind},{"pass",ok},{"attempts",normal.attempts},{"peak",normal.peak},{"upper",bound.requested_heap_upper}});
  }
  // Direct borrowed-DOM conversion includes invalid/legacy integer behavior;
  // file conversion above includes actual open,managed parser and retirement.
  size_t candidateCases=0;
  for(int kind=0;kind<9;++kind){auto data=in.descriptor;bool expected=kind==0 || kind==8;
    if(kind==1)data.erase("name");if(kind==2)data["id"]=0;if(kind==3)data["textures"]=nullptr;if(kind==4)data["textures"]=Json::array();if(kind==5)data["textures"]="text";if(kind==6)data["textures"][2]=12;if(kind==7)data["name"]=false;if(kind==8)data["id"]=-1;
    const auto bound=BoundLegacyCubeCandidateSource(MeasureCatalogCandidateInput(data));bool jsonError=false;
    {observation::Scope scope;try{descriptor=CompileLegacyCubeDescriptor(data);}catch(const Json::exception &){jsonError=true;}}const auto normal=observation::State;
    const bool valid=bool(descriptor)==expected && jsonError==(kind==6 || kind==7) && normal.peak<=bound.requested_heap_upper && (kind!=8 || descriptor->Id==static_cast<size_t>(-1));
    descriptor.reset();if(!valid || observation::State.live || observation::State.blocks)++failures;
    for(size_t i=0;i<normal.attempts;++i){bool refused=false;{observation::Scope scope(i);try{descriptor=CompileLegacyCubeDescriptor(data);}catch(const std::bad_alloc &){refused=true;}catch(const Json::exception &){}}
      const auto counts=observation::State;descriptor.reset();if(!refused || !counts.refusals || counts.peak>bound.requested_heap_upper || observation::State.live || observation::State.blocks){++failures;break;}
      bool retryError=false;{observation::Scope scope;try{descriptor=CompileLegacyCubeDescriptor(data);}catch(const Json::exception &){retryError=true;}}
      if(bool(descriptor)!=expected || retryError!=(kind==6 || kind==7))++failures;descriptor.reset();if(observation::State.live || observation::State.blocks)++failures;++prefixes;
    }++candidateCases;
  }
  struct Case {const char *name;std::vector<std::string> refs;int strip,w,h,frames;};
  const std::vector<Case> cases={{"mixed",in.mixed,0,2,2,1},{"explicit",in.explicitFrames,0,2,2,2},{"strip",in.strips,2,2,2,2},{"png_repeated",std::vector<std::string>(6,"png"),0,64,64,1},{"five",std::vector<std::string>(5,"memory"),0,2,2,1},{"seven",std::vector<std::string>(7,"memory"),0,2,2,1},{"empty",{},0,0,0,0},{"missing",{"absent"},0,0,0,0},{"invalid_memory",{"short"},0,0,0,0},{"invalid_strip",std::vector<std::string>(6,"file"),3,0,0,0}};
  UTextureCubeStorage storage(in.bases);
  for(bool useBatch:{false,true})for(const auto &item:cases){const auto domain=Domain(in,item.refs,item.strip,useBatch);const auto bound=BoundCpuAtlasSource(domain);
    std::unique_ptr<UTextureCubeStorage::AtlasDecodeBatch> batch;UTextureCubeStorage::CubeAtlasPixels atlas;
    auto ledger=std::make_shared<UStbiAllocationLedger>();const auto build=[&]{if(useBatch){batch=std::make_unique<UTextureCubeStorage::AtlasDecodeBatch>();atlas=storage.BuildCubeAtlasPixels(item.name,item.refs,item.strip,*batch);}else atlas=storage.BuildCubeAtlasPixels(item.name,item.refs,item.strip);};
    {observation::Scope scope;UStbiAllocationScope active(ledger);build();}const auto normal=observation::State;const auto decode=ledger->GetSnapshot();
    bool ok=bound.supported && !bound.overflow && Matches(atlas,in,item.refs,item.w,item.h,item.frames,item.strip>1 && item.refs.size()==6) && normal.peak<=bound.ordinary_new_upper && decode.peak_admitted_request_bytes<=bound.new_decoder_peak_upper;
    atlas={};batch.reset();ok=ok && !observation::State.live && !observation::State.blocks && !ledger->GetSnapshot().live_request_bytes;
    for(size_t i=0;ok && i<normal.attempts;++i){ledger=std::make_shared<UStbiAllocationLedger>();bool refused=false;
      {observation::Scope scope(i);UStbiAllocationScope active(ledger);try{build();}catch(const std::bad_alloc &){refused=true;}}
      const auto counts=observation::State;const auto decoded=ledger->GetSnapshot();atlas={};batch.reset();ok=refused && counts.refusals && counts.peak<=bound.ordinary_new_upper && decoded.peak_admitted_request_bytes<=bound.new_decoder_peak_upper && !observation::State.live && !observation::State.blocks && !ledger->GetSnapshot().live_request_bytes;
      ledger=std::make_shared<UStbiAllocationLedger>();if(ok){observation::Scope scope;UStbiAllocationScope active(ledger);build();}ok=ok && Matches(atlas,in,item.refs,item.w,item.h,item.frames,item.strip>1 && item.refs.size()==6);atlas={};batch.reset();ok=ok && !observation::State.live && !ledger->GetSnapshot().live_request_bytes;++prefixes;
    }
    for(size_t i=0;ok && i<decode.allocation_attempts;++i){ledger=std::make_shared<UStbiAllocationLedger>(SIZE_MAX,i);
      {observation::Scope scope;UStbiAllocationScope active(ledger);build();}const auto counts=observation::State;const auto decoded=ledger->GetSnapshot();atlas={};batch.reset();ok=decoded.refusals && counts.peak<=bound.ordinary_new_upper && decoded.peak_admitted_request_bytes<=bound.new_decoder_peak_upper && !observation::State.live && !observation::State.blocks && !ledger->GetSnapshot().live_request_bytes;
      ledger=std::make_shared<UStbiAllocationLedger>();if(ok){observation::Scope scope;UStbiAllocationScope active(ledger);build();}ok=ok && Matches(atlas,in,item.refs,item.w,item.h,item.frames,item.strip>1 && item.refs.size()==6);atlas={};batch.reset();ok=ok && !observation::State.live && !ledger->GetSnapshot().live_request_bytes;++decoderPrefixes;
    }
    if(!ok)++failures;report["atlas_cases"].push_back({{"name",item.name},{"batch",useBatch},{"pass",ok},{"ordinary_attempts",normal.attempts},{"ordinary_peak",normal.peak},{"ordinary_upper",bound.ordinary_new_upper},{"decoder_attempts",decode.allocation_attempts},{"decoder_peak",decode.peak_admitted_request_bytes},{"decoder_upper",bound.new_decoder_peak_upper}});
  }
  // One observed owner graph across actual file/registration/atlas phases.
  std::shared_ptr<UTextureBaseStorage> bases;std::shared_ptr<UTextureCubeStorage> cubes;
  std::unique_ptr<UTextureCubeStorage::AtlasDecodeBatch> oldBatch,newBatch;
  UTextureCubeStorage::CubeAtlasPixels oldAtlas,currentAtlas,returnedAgain;
  auto oldLedger=std::make_shared<UStbiAllocationLedger>();
  {observation::Scope scope;UStbiAllocationScope active(oldLedger);bases=std::make_shared<UTextureBaseStorage>(*in.bases);cubes=std::make_shared<UTextureCubeStorage>(bases);oldBatch=std::make_unique<UTextureCubeStorage::AtlasDecodeBatch>();oldAtlas=cubes->BuildCubeAtlasPixels("graph",in.mixed,0,*oldBatch);}
  const auto census=[&](){detail::CatalogStorageInspector out;out.TextureCubes(cubes);out.TextureBases(bases);out.AtlasBatch(oldBatch.get());out.AtlasBatch(newBatch.get());out.AtlasPixels(&oldAtlas);out.AtlasPixels(&currentAtlas);out.AtlasPixels(&returnedAgain);if(descriptor)out.LegacyCube(&*descriptor);return out.Result;};
  catalog_bound_profile_fixture::Profile profile(BoundRetainedCatalogRequests(census()));
  const auto phase=[&](const char *name,const CatalogCandidateEnvelope &source,auto operation){
    const auto counted=census();const auto entry=BoundRetainedCatalogRequests(counted);const auto upper=ComposeCatalogConstruction(entry,source.requested_heap_upper,source.supported,source.overflow);
    const auto ownerBound=profile.Phase(entry,source.requested_heap_upper,source.supported,source.overflow);
    const auto held=GetStbiProcessAllocationSnapshot().live_request_bytes;const auto observed=observation::State.live+held;auto ledger=std::make_shared<UStbiAllocationLedger>();const auto attempts=observation::State.attempts;
    observation::State.peak=observation::State.live;observation::State.active=true;{UStbiAllocationScope active(ledger);operation();}observation::State.active=false;
    const auto decoded=ledger->GetSnapshot();const auto components=observation::State.peak+held+decoded.peak_admitted_request_bytes;
    const bool ok=upper.supported && !upper.overflow && counted.decoder_request_bytes==held && observed<=entry.requested_heap_upper && components<=upper.requested_heap_upper &&
      (!owner_binding || (ownerBound.supported && !ownerBound.overflow && components<=ownerBound.requested_heap_upper));
    if(!ok)++failures;report["phases"].push_back({{"name",name},{"pass",ok},{"entry_observed",observed},{"entry_upper",entry.requested_heap_upper},{"retained_decoder",held},{"new_decoder_peak",decoded.peak_admitted_request_bytes},{"new_decoder_attempts",decoded.allocation_attempts},{"ordinary_peak",observation::State.peak},{"ordinary_attempts",observation::State.attempts-attempts},{"observed_components",components},{"upper",upper.requested_heap_upper},{"owner_profile_upper",ownerBound.requested_heap_upper}});
  };
  phase("legacy_registration",registrationBound,[&]{bases->Load(in.registrationPath);});
  phase("legacy_descriptor_file",descriptorBound,[&]{descriptor=LoadLegacyCubeDescriptorFile(in.descriptorPath);});
  Inputs current=in;current.bases=bases;current.golden["file"]=Pixels(3,2,113);current.golden["memory"]=Pixels(1,1,153);
  current.domains["file"].width=3;StbiSourceDomain memory;memory.format=StbiSourceFormat::Pnm;memory.width=memory.height=1;current.domains["memory"]=memory;
  auto atlasDomain=Domain(current,in.mixed,0,true);const auto atlasBound=BoundCpuAtlasSource(atlasDomain);
  const CatalogCandidateEnvelope atlasSource{atlasBound.combined_additional_upper,0,atlasBound.supported,atlasBound.overflow};
  phase("new_cpu_batch",atlasSource,[&]{newBatch=std::make_unique<UTextureCubeStorage::AtlasDecodeBatch>();currentAtlas=cubes->BuildCubeAtlasPixels("graph",descriptor->Textures,0,*newBatch);});
  const auto hitBound=BoundCpuAtlasSource(Domain(current,in.mixed,0,true,newBatch.get()));
  phase("cache_hit_returned_atlas",CatalogCandidateEnvelope{hitBound.combined_additional_upper,0,hitBound.supported,hitBound.overflow},[&]{returnedAgain=cubes->BuildCubeAtlasPixels("graph",descriptor->Textures,0,*newBatch);});
  bool graph=Matches(oldAtlas,in,in.mixed,2,2,1) && Matches(currentAtlas,current,in.mixed,3,2,1) && currentAtlas.Rgba==returnedAgain.Rgba && currentAtlas.Rgba.data()!=returnedAgain.Rgba.data() && Descriptor(descriptor,in) && report["phases"].back()["new_decoder_attempts"]==0;
  const auto counted=census();const auto retained=BoundRetainedCatalogRequests(counted);const auto held=GetStbiProcessAllocationSnapshot().live_request_bytes;const auto observed=observation::State.live+held;
  const auto aliasCheck=[&]{detail::CatalogStorageInspector one,many;one.AtlasBatch(oldBatch.get());one.AtlasPixels(&oldAtlas);if(descriptor)one.LegacyCube(&*descriptor);
    for(int i=0;i<64;++i){many.AtlasBatch(oldBatch.get());many.AtlasPixels(&oldAtlas);if(descriptor)many.LegacyCube(&*descriptor);}return one.Result.known_storage_bytes==many.Result.known_storage_bytes && one.Result.decoder_request_bytes==many.Result.decoder_request_bytes && one.Result.shared_owners==many.Result.shared_owners;};
  const bool dedup=aliasCheck();graph=graph && dedup && observed<=retained.requested_heap_upper && counted.decoder_request_bytes==held;
  report["retained"]={{"observed",observed},{"upper",retained.requested_heap_upper},{"decoder_requests_once",held},{"decoder_allocations",counted.decoder_allocations},{"alias64_deduplicated",dedup}};
  if(owner_binding){auto checks=profile.Controls(retained);if(!checks["pass"].get<bool>())++failures;report["owner_profile_binding"]=checks;}
  std::thread release([owner=std::move(oldBatch)]()mutable{owner.reset();});release.join();const bool crossThread=oldLedger->GetSnapshot().live_request_bytes==0 && Matches(oldAtlas,in,in.mixed,2,2,1);
  graph=graph && crossThread;if(!graph)++failures;report["old_current_returned_oracles"]=graph;report["escaped_batch_cross_thread_retirement"]=crossThread;
  descriptor.reset();oldAtlas={};currentAtlas={};returnedAgain={};newBatch.reset();cubes.reset();current.bases.reset();bases.reset();
  const bool retired=!observation::State.live && !observation::State.blocks && !GetStbiProcessAllocationSnapshot().live_request_bytes;if(!retired)++failures;report["graph_final_zero"]=retired;
  if(!retired){report["remaining_bytes"]=observation::State.live;report["status"]="FAIL";if(path){std::ofstream f(path);f<<report.dump(2)<<'\n';}return 1;}
  // Cache identity is explicit,including remembered failures. A standalone
  // call or another batch observes the real current file instead.
  bool isolation=false,negativeCache=false;
  {const auto file=in.root/"isolation.ppm";const auto oldPixels=Pixels(2,2,33),newPixels=Pixels(2,2,133);Ppm(file,oldPixels);auto b=std::make_shared<UTextureBaseStorage>();b->Register("image",file.string());UTextureCubeStorage c(b);const std::vector<std::string> names(6,"image");UTextureCubeStorage::AtlasDecodeBatch cache;
    const auto old=c.BuildCubeAtlasPixels("isolated",names,0,cache);Ppm(file,newPixels);const auto retainedOld=c.BuildCubeAtlasPixels("isolated",names,0,cache);const auto fresh=c.BuildCubeAtlasPixels("isolated",names,0);UTextureCubeStorage::AtlasDecodeBatch other;const auto separate=c.BuildCubeAtlasPixels("isolated",names,0,other);
    isolation=old.Rgba==retainedOld.Rgba && old.Rgba[0]==33 && fresh.Rgba[0]==133 && fresh.Rgba==separate.Rgba;
    // A present invalid file avoids any dependence on stale test directories.
    const auto failed=in.root/"negative.ppm";{std::ofstream f(failed);f<<"not image data";}b->Register("negative",failed.string());const std::vector<std::string> badNames(6,"negative");UTextureCubeStorage::AtlasDecodeBatch negative;
    const auto a=c.BuildCubeAtlasPixels("negative",badNames,0,negative);Ppm(failed,newPixels);const auto cached=c.BuildCubeAtlasPixels("negative",badNames,0,negative);const auto retry=c.BuildCubeAtlasPixels("negative",badNames,0);negativeCache=a.Rgba.empty() && cached.Rgba.empty() && !retry.Rgba.empty() && retry.Rgba[0]==133;
  }
  if(!isolation || !negativeCache)++failures;report["explicit_cache_isolation"]=isolation;report["negative_cache_new_batch_recovery"]=negativeCache;
  bool concurrentA=false,concurrentB=false;
  const auto reader=[&](bool &ok){UTextureCubeStorage::AtlasDecodeBatch cache;auto pixels=storage.BuildCubeAtlasPixels("parallel",in.mixed,0,cache);ok=Matches(pixels,in,in.mixed,2,2,1);};
  std::thread first(reader,std::ref(concurrentA)),second(reader,std::ref(concurrentB));first.join();second.join();const bool concurrent=concurrentA && concurrentB && !GetStbiProcessAllocationSnapshot().live_request_bytes;if(!concurrent)++failures;report["independent_concurrent_batches"]=concurrent;
  const std::vector<std::string> controlNames(6,"memory");const auto valid=storage.BuildCubeAtlasPixels("control",controlNames,0);auto damaged=valid;damaged.Rgba[0]^=1;
  const bool pixelControl=Matches(valid,in,controlNames,2,2,1) && !Matches(damaged,in,controlNames,2,2,1);if(!pixelControl)++failures;report["changed_pixel_control"]=pixelControl;
  CpuAtlasSourceDomain overflow;overflow.file_references=SIZE_MAX;overflow.batch=true;
  const bool overflowRejected=BoundCpuAtlasSource(overflow).overflow && BoundLegacyBaseDirectorySource(SIZE_MAX,64).overflow && BoundLegacyCubeFileSource(SIZE_MAX,64).overflow;
  bool extra=false;{observation::Scope scope(0);try{void *p=::operator new(atlasBound.combined_additional_upper+1);::operator delete(p);}catch(const std::bad_alloc &){extra=observation::State.max_request>atlasBound.combined_additional_upper;}}
  if(!overflowRejected || !extra)++failures;report["overflow_rejected"]=overflowRejected;report["unmodeled_allocation_rejected"]=extra;
  {const auto badPath=(in.root/"not_directory").string();{std::ofstream f(badPath);f<<"file";}UTextureBaseStorage b;b.RegisterPixels("old",{1,1,{1,2,3,4}});const auto before=Bases(b);b.Load(badPath);const bool ok=Bases(b)==before;if(!ok)++failures;report["filesystem_error_preserves_prior"]=ok;}
  report["candidate_cases"]=candidateCases;report["ordinary_refusal_and_retry_cases"]=prefixes;report["decoder_refusal_and_retry_cases"]=decoderPrefixes;
  report["failed_cases"]=failures;report["status"]=failures?"FAIL":"PASS";if(path){std::ofstream f(path);f<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return failures?1:0;
}
}

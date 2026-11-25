#pragma once
#include "WorldGen/Core/BiomeMapSourceEnvelope.h"
#include "Test/StbiAllocationFixture.h"
#include "Test/OrdinaryNewObservation.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

inline int RunBiomeMapSourceFixture(const char *reportPath)
{
  using namespace cutum;using Json=nlohmann::json;namespace fs=std::filesystem;
  struct Input {const char *name;std::vector<unsigned char> source,rgb;StbiSourceDomain domain;};
  Input png{"png",test::MakeStbiAllocationPng(),{},{StbiSourceFormat::Png,64,64,3,16384,64*64*3+64}};
  for(int y=0;y<64;++y)for(int x=0;x<64;++x){const auto c=test::StbiFixtureRgb(x,y);png.rgb.insert(png.rgb.end(),c.begin(),c.end());}
  const std::string ppmHeader="P6\n2 2\n255\n",pgmHeader="P5\n2 2\n65535\n";
  Input ppm{"ppm",{ppmHeader.begin(),ppmHeader.end()},{0x2d,0x8a,0x3f,0xe8,0xd4,0x6a,0x7a,0x7a,0x7a,0xd8,0xe8,0xf0},{StbiSourceFormat::Pnm,2,2,3}};
  ppm.source.insert(ppm.source.end(),ppm.rgb.begin(),ppm.rgb.end());
  Input pgm{"pgm16",{pgmHeader.begin(),pgmHeader.end()},{},{StbiSourceFormat::Pnm,2,2,3}};
  for(unsigned char v:{17,49,101,203}){pgm.source.push_back(v);pgm.source.push_back(v);pgm.rgb.insert(pgm.rgb.end(),3,v);}
  std::array<Input,3> inputs{std::move(png),std::move(ppm),std::move(pgm)};
  const auto root=fs::path("build/desktop-msvc/s4-biome-map-source-fixtures");fs::create_directories(root);
  Json report={{"schema","biome-map-source.v1"},{"cases",Json::array()}};size_t failures=0,ownerRefusals=0,decoderRefusals=0;
  const auto processBefore=GetStbiProcessAllocationSnapshot();
  const auto valid=[](const Input &input,const auto &image){
    if(!image || image->Width!=input.domain.width || image->Height!=input.domain.height || image->ByteCount()!=input.rgb.size())return false;
    return std::equal(input.rgb.begin(),input.rgb.end(),image->Data());
  };
  const auto clean=[](const auto &ledger){const auto s=ledger->GetSnapshot();return !s.live_request_bytes && !s.live_allocations && !s.reserved_request_bytes && !observation::State.live && !observation::State.blocks;};
  for(const auto &input:inputs){
    const auto path=(root/(std::string(input.name)+".img")).string();
    {std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char *>(input.source.data()),input.source.size());}
    const auto bound=BoundBiomeMapSource(input.domain);auto ledger=std::make_shared<UStbiAllocationLedger>();
    std::shared_ptr<const WorldGenBiomeMapImage> image;
    {UStbiAllocationScope active(ledger);observation::Scope ordinary;image=DecodeWorldGenBiomeMapImage(path.c_str());}
    const auto normal=observation::State;const auto decoder=ledger->GetSnapshot();const bool semantic=valid(input,image);
    const auto allocation=InspectStbiAllocation(image?image->Data():nullptr);
    // Only the shared object/control uses ordinary new at this boundary.
    // Decoder scratch has retired before that single allocation is reached.
    const auto combined=std::max(decoder.peak_admitted_request_bytes,decoder.live_request_bytes+normal.peak);
    const bool ok=semantic && bound.supported && !bound.overflow && normal.attempts==1 && normal.live==bound.owner_request &&
        decoder.live_allocations==1 && allocation.owner.get()==ledger.get() && allocation.request_bytes==image->DecodedRequestBytes() &&
        allocation.payload_bytes==bound.decoder.returned_payload_upper && normal.live+decoder.live_request_bytes<=bound.retained_request_upper && combined<=bound.combined_peak_upper;
    image.reset();if(!ok || !clean(ledger))++failures;
    report["cases"].push_back({{"name",input.name},{"pass",ok},{"ordinary_attempts",normal.attempts},{"owner_request",normal.live},
        {"decoder_attempts",decoder.allocation_attempts},{"retained_decoder_request",decoder.live_request_bytes},{"decoder_peak",decoder.peak_admitted_request_bytes},
        {"combined_peak",combined},{"combined_upper",bound.combined_peak_upper},{"eliminated_rgb_vector_bytes",input.rgb.size()}});
    for(size_t prefix=0;prefix<normal.attempts;++prefix){
      auto fault=std::make_shared<UStbiAllocationLedger>();bool refused=false;
      {UStbiAllocationScope active(fault);observation::Scope ordinary(prefix);try{image=DecodeWorldGenBiomeMapImage(path.c_str());}catch(const std::bad_alloc &){refused=true;}}
      image.reset();if(!refused || !clean(fault))++failures;
      {UStbiAllocationScope active(fault);observation::Scope ordinary;image=DecodeWorldGenBiomeMapImage(path.c_str());}
      if(!valid(input,image))++failures;image.reset();if(!clean(fault))++failures;++ownerRefusals;
    }
    for(size_t prefix=0;prefix<decoder.allocation_attempts;++prefix){
      auto fault=std::make_shared<UStbiAllocationLedger>(SIZE_MAX,prefix);
      {UStbiAllocationScope active(fault);observation::Scope ordinary;image=DecodeWorldGenBiomeMapImage(path.c_str());}
      if(image || !fault->GetSnapshot().refusals)++failures;image.reset();if(!clean(fault))++failures;
      auto retry=std::make_shared<UStbiAllocationLedger>();
      {UStbiAllocationScope active(retry);observation::Scope ordinary;image=DecodeWorldGenBiomeMapImage(path.c_str());}
      if(!valid(input,image))++failures;image.reset();if(!clean(retry))++failures;++decoderRefusals;
    }
  }
  // One epoch across both versions observes the prior/current overlap.
  auto shared=std::make_shared<UStbiAllocationLedger>();std::shared_ptr<const WorldGenBiomeMapImage> old,current;
  const auto oldPath=(root/"png.img").string(),newPath=(root/"ppm.img").string();
  {UStbiAllocationScope active(shared);observation::Scope ordinary;
    old=DecodeWorldGenBiomeMapImage(oldPath.c_str());current=DecodeWorldGenBiomeMapImage(newPath.c_str());}
  const auto pair=shared->GetSnapshot();const auto owners=observation::State;
  const auto first=BoundBiomeMapSource(inputs[0].domain),second=BoundBiomeMapSource(inputs[1].domain);
  const auto pairUpper=std::max(first.combined_peak_upper,first.retained_request_upper+second.combined_peak_upper);
  const auto pairObserved=std::max(pair.peak_admitted_request_bytes+first.owner_request,pair.live_request_bytes+owners.peak);
  const bool overlap=valid(inputs[0],old) && valid(inputs[1],current) && old->Data()!=current->Data() && pair.live_allocations==2 &&
      owners.live==first.owner_request+second.owner_request && pairObserved<=pairUpper;
  current.reset();auto escaped=old;old.reset();const bool escapedValid=valid(inputs[0],escaped);
  std::thread release([image=std::move(escaped)]() mutable {image.reset();});release.join();
  const bool released=clean(shared);if(!overlap || !escapedValid || !released)++failures;
  report["prior_current"]={{"pass",overlap},{"combined_observed_upper",pairObserved},{"source_upper",pairUpper},{"escaped_valid",escapedValid},{"cross_thread_final_zero",released}};
  const auto badPath=(root/"malformed.img").string();{std::ofstream out(badPath);out<<"incomplete image";}
  auto malformed=std::make_shared<UStbiAllocationLedger>();
  {UStbiAllocationScope active(malformed);observation::Scope ordinary;current=DecodeWorldGenBiomeMapImage(badPath.c_str());}
  const bool malformedClean=!current && clean(malformed) && !DecodeWorldGenBiomeMapImage(nullptr) && !DecodeWorldGenBiomeMapImage("");if(!malformedClean)++failures;
  size_t bytes=99;const bool sizing=!CheckedBiomeMapRgbBytes(0,1,bytes) && bytes==0 && !CheckedBiomeMapRgbBytes(SIZE_MAX,2,bytes) && bytes==0 &&
      CheckedBiomeMapRgbBytes(SIZE_MAX/3,1,bytes) && bytes==(SIZE_MAX/3)*3;
  auto huge=inputs[0].domain;huge.png_idat_capacity_upper=SIZE_MAX;const auto overflow=BoundBiomeMapSource(huge);
  auto rgba=inputs[0].domain;rgba.requested_components=4;
  const bool arithmetic=sizing && overflow.overflow && !BoundBiomeMapSource(rgba).supported;if(!arithmetic)++failures;
  const auto ordinaryUpper=first.owner_request;
  {observation::Scope scope;auto *extra=::operator new(ordinaryUpper+1);::operator delete(extra);}
  const bool negative=observation::State.peak>ordinaryUpper && !observation::State.live;if(!negative)++failures;
  const auto processAfter=GetStbiProcessAllocationSnapshot();const bool processClean=processAfter.live_request_bytes==processBefore.live_request_bytes &&
      processAfter.live_allocations==processBefore.live_allocations && !processAfter.reserved_request_bytes;if(!processClean)++failures;
  report["owner_refusal_and_exact_retries"]=ownerRefusals;report["decoder_refusal_and_exact_retries"]=decoderRefusals;
  report["malformed_clean"]=malformedClean;report["checked_sizing_and_overflow"]=arithmetic;report["negative_control"]=negative;report["process_live_restored"]=processClean;
  report["failed_cases"]=failures;report["status"]=failures?"FAIL":"PASS";
  report["scope"]="Actual production RGB file decode,one shared owner,no copied vector. Source STBI requested/admitted bounds plus ordinary object/control;borrowed path,file CRT,optional ledger,thread runtime,prior escaped roots separate. Not physical process memory or whole C05 acceptance.";
  if(reportPath){std::ofstream out(reportPath);out<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return failures?1:0;
}

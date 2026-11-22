#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
#include "Test/StbiFormatFixtures.h"
#include "ThirdParty/stb_image.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace
{
using Json=nlohmann::json;
void Require(bool value,const char *message) {if(!value) throw std::runtime_error(message);}
void CheckPixels(const unsigned char *data,const cutum::test::StbiFormatFixture &fixture,int components)
{
  for(size_t pixel=0;pixel<fixture.rgb.size()/3;++pixel) {
    for(int c=0;c<3;++c)
      Require(std::abs(int(data[pixel*components+c])-int(fixture.rgb[pixel*3+c]))<=fixture.tolerance,"decoded RGB differs from authored source");
    if(components==4) Require(data[pixel*components+3]==255,"decoded alpha differs from authored opaque source");
  }
}
}
int main(int argc,char **argv)
{
  using namespace cutum;
#if defined(_WIN32)
  SetErrorMode(SEM_NOGPFAULTERRORBOX|SEM_FAILCRITICALERRORS);
#endif
  const std::string mode=argc>1?argv[1]:"--all";
  const std::filesystem::path reportPath=argc>2?argv[2]:"build/desktop-msvc/s4-decoder-formats-units-20261121.json";
  Json report={{"schema","stbi-ordinary-source-envelope-test.v1"},{"status","RUNNING"},{"cases",Json::array()}};
  const auto save=[&] {std::ofstream file(reportPath);file << report.dump(2) << '\n';};
  try {
    const auto fixtures=test::MakeStbiFormatFixtures();const auto layout=GetStbiDecoderLayout();
    report["layout"]={{"jpeg_heap_state",layout.jpeg_heap_state},{"gif_info_heap_state",layout.gif_info_heap_state},
        {"context_inline",layout.context_inline},{"png_inline",layout.png_inline},{"zlib_inline",layout.zlib_inline},
        {"gif_inline",layout.gif_inline},{"format_mask",layout.format_mask},{"prefix_bytes",StbiAllocationHeaderBytes()}};
    if(mode=="--conversion-range") {
      bool checked=true;
      for(unsigned route=0;route<4;++route) {
        const auto before=GetStbiProcessAllocationSnapshot();
        void *input=StbiAllocate(8);Require(input!=nullptr,"conversion size fixture input allocation failed");
        auto ledger=std::make_shared<UStbiAllocationLedger>(0);
        {UStbiAllocationScope active(ledger);
          std::unique_ptr<void,decltype(&stbi_image_free)> result(TestStbiConversionRange(route,input),&stbi_image_free);
          Require(!result,"size fixture unexpectedly allocated a buffer");
        }
        const auto counts=ledger->GetSnapshot();
        const auto after=GetStbiProcessAllocationSnapshot();
        checked=checked && counts.allocation_attempts==(route==3?1:0) && !counts.live_request_bytes && !counts.reserved_request_bytes &&
            after.live_request_bytes==before.live_request_bytes && after.live_allocations==before.live_allocations;
        if(route==3) {
          const size_t bytes=size_t{4}*16384*16384*sizeof(stbi_us);
          checked=checked && after.peak_admitted_request_bytes==bytes+2*StbiAllocationHeaderBytes()+8;
          report["supported_intermediate_bytes"]=bytes;
          report["process_admitted_peak"]=after.peak_admitted_request_bytes;
        }
        report["cases"].push_back({{"route",route},{"allocation_attempts",counts.allocation_attempts},
            {"refusals",counts.refusals},{"live_request_bytes",counts.live_request_bytes}});
      }
      report["status"]=checked?"PASS":"SIZE_CHECK_AFTER_ALLOCATION_REQUEST";save();return checked?0:1;
    }
    if(mode=="--pic-malformed") {
      auto bytes=std::find_if(fixtures.begin(),fixtures.end(),[](const auto &f){return f.name=="pic";})->bytes;
      bytes.resize(109);report["phase"]="before_truncated_PIC_RGB";save();
      auto ledger=std::make_shared<UStbiAllocationLedger>(1024*1024);int w=0,h=0,c=0;
      {UStbiAllocationScope active(ledger);
        std::unique_ptr<unsigned char,decltype(&stbi_image_free)> output(stbi_load_from_memory(bytes.data(),static_cast<int>(bytes.size()),&w,&h,&c,3),&stbi_image_free);
        Require(!output,"truncated PIC unexpectedly decoded");
      }
      Require(ledger->GetSnapshot().live_request_bytes==0,"failed PIC left decoder allocation");
      report["status"]="PASS";report["phase"]="truncated_PIC_rejected_no_live_credit";save();return 0;
    }
    if(mode=="--png-size-check") {
      const auto bytes=test::MakeStbiOversizedPngGuess();auto ledger=std::make_shared<UStbiAllocationLedger>();int w=0,h=0,c=0;
      {UStbiAllocationScope active(ledger);
        std::unique_ptr<unsigned char,decltype(&stbi_image_free)> output(stbi_load_from_memory(bytes.data(),static_cast<int>(bytes.size()),&w,&h,&c,3),&stbi_image_free);
        Require(!output,"unsupported huge PNG unexpectedly decoded");
      }
      StbiSourceDomain domain;domain.width=16384;domain.height=16384;domain.requested_components=3;
      const auto bound=BoundStbiOrdinarySource(domain);const auto counts=ledger->GetSnapshot();
      report["admitted_peak"]=counts.peak_admitted_request_bytes;report["source_upper"]=bound.admitted_peak_upper;
      report["system_failures"]=counts.system_failures;report["final_live"]=counts.live_request_bytes;
      report["status"]=counts.peak_admitted_request_bytes<=bound.admitted_peak_upper?"PASS":"OUT_OF_RANGE_REQUEST";
      save();return report["status"]=="PASS"?0:1;
    }
    if(mode=="--conversion-failure") {
      bool clean=true;
      for(bool enlarge:{false,true}) for(bool flip:{false,true}) {
        const auto &fixture=*std::find_if(fixtures.begin(),fixtures.end(),[&](const auto &f){return f.name==(enlarge?"ppm":"pgm16");});
        auto ledger=std::make_shared<UStbiAllocationLedger>(2*1024*1024,enlarge?2:3);int w=0,h=0,c=0;
        report["phase"]=std::string(enlarge?"8_to_16":"16_to_8")+(flip?"_flip":"_no_flip");save();
        stbi_set_flip_vertically_on_load(flip?1:0);
        {UStbiAllocationScope active(ledger);
          std::unique_ptr<void,decltype(&stbi_image_free)> pixels(enlarge?static_cast<void *>(stbi_load_16_from_memory(fixture.bytes.data(),static_cast<int>(fixture.bytes.size()),&w,&h,&c,3)):
              static_cast<void *>(stbi_load_from_memory(fixture.bytes.data(),static_cast<int>(fixture.bytes.size()),&w,&h,&c,3)),&stbi_image_free);
          Require(!pixels,"conversion fault did not reject allocation");
        }
        const auto counts=ledger->GetSnapshot();clean=clean && counts.live_request_bytes==0 && counts.reserved_request_bytes==0;
        report["cases"].push_back({{"enlarge",enlarge},{"flip",flip},{"live_request_bytes",counts.live_request_bytes},
            {"reserved_request_bytes",counts.reserved_request_bytes},{"refusals",counts.refusals}});save();
      }
      stbi_set_flip_vertically_on_load(0);report["status"]=clean?"PASS":"LEAK";save();return clean?0:1;
    }
    const auto root=std::filesystem::path("build/desktop-msvc/s4-decoder-format-fixtures");std::filesystem::create_directories(root);
    size_t failCases=0,truncatedReturned=0,byteRefusals=0,retries=0,shortHeaders=0;uint32_t covered=0;
    for(const auto &fixture:fixtures) {
      const auto bit=1u<<static_cast<unsigned>(fixture.domain.format);if(!(layout.format_mask&bit)) continue;
      const auto path=root/(fixture.name+".img");{std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char *>(fixture.bytes.data()),fixture.bytes.size());}
      int iw=0,ih=0,ic=0;
      Require(stbi_info_from_memory(fixture.bytes.data(),static_cast<int>(fixture.bytes.size()),&iw,&ih,&ic) &&
          iw==fixture.domain.width && ih==fixture.domain.height,"fixture metadata differs from authored dimensions");
      for(int components:{3,4}) {
        auto domain=fixture.domain;domain.requested_components=components;const auto bound=BoundStbiOrdinarySource(domain);
        Require(bound.supported && !bound.overflow,"ordinary decoder domain not modeled");
        size_t attempts=0;
        for(bool fromFile:{false,true}) {
          auto ledger=std::make_shared<UStbiAllocationLedger>(2*1024*1024);size_t retained=0;int w=0,h=0,c=0;
          report["phase"]=fixture.name+(components==3?"_RGB":"_RGBA")+(fromFile?"_file":"_memory");save();
          {UStbiAllocationScope active(ledger);
            std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(fromFile?stbi_load(path.string().c_str(),&w,&h,&c,components):
                stbi_load_from_memory(fixture.bytes.data(),static_cast<int>(fixture.bytes.size()),&w,&h,&c,components),&stbi_image_free);
            Require(pixels && w==domain.width && h==domain.height,"authored image failed ordinary decode");CheckPixels(pixels.get(),fixture,components);
            const auto info=InspectStbiAllocation(pixels.get());retained=info.request_bytes;
            Require(info.payload_bytes==bound.returned_payload_upper && info.request_bytes==bound.returned_request_upper,
                "actual returned request differs from source return model");
            const auto held=ledger->GetSnapshot();Require(held.live_allocations==1 && held.live_request_bytes==retained,"decoder internals survived ordinary return");
          }
          const auto counts=ledger->GetSnapshot();Require(counts.live_request_bytes==0 && counts.reserved_request_bytes==0,"normal decode/free left credits");
          Require(counts.peak_admitted_request_bytes<=bound.admitted_peak_upper,"decoder request peak exceeds source envelope");
          report["cases"].push_back({{"fixture",fixture.name},{"components",components},{"file_api",fromFile},
              {"retained_request",retained},{"admitted_peak",counts.peak_admitted_request_bytes},{"source_peak_upper",bound.admitted_peak_upper},
              {"attempts",counts.allocation_attempts},{"final_live",0}});
          if(!fromFile) attempts=counts.allocation_attempts;
        }
        for(size_t allowed=0;allowed<attempts;++allowed) {
          auto fault=std::make_shared<UStbiAllocationLedger>(2*1024*1024,allowed);int w=0,h=0,c=0;
          {UStbiAllocationScope active(fault);
            std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(stbi_load_from_memory(fixture.bytes.data(),static_cast<int>(fixture.bytes.size()),&w,&h,&c,components),&stbi_image_free);
            if(pixels) CheckPixels(pixels.get(),fixture,components);
          }
          const auto counts=fault->GetSnapshot();Require(counts.refusals && !counts.live_request_bytes && !counts.reserved_request_bytes,
              "allocation failure left a decoder block/reservation");
          Require(counts.peak_admitted_request_bytes<=bound.admitted_peak_upper,"refused decode request peak exceeds source envelope");++failCases;
        }
        auto limited=std::make_shared<UStbiAllocationLedger>(StbiAllocationHeaderBytes()-1);
        {UStbiAllocationScope active(limited);int w=0,h=0,c=0;
          std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(stbi_load_from_memory(fixture.bytes.data(),static_cast<int>(fixture.bytes.size()),&w,&h,&c,components),&stbi_image_free);
          Require(!pixels,"byte refusal did not stop decoder allocation");
        }
        Require(limited->GetSnapshot().refusals && !limited->GetSnapshot().live_request_bytes &&
            !limited->GetSnapshot().reserved_request_bytes,"byte refusal retained a decoder block/reservation");++byteRefusals;
        auto retry=std::make_shared<UStbiAllocationLedger>(2*1024*1024);
        {UStbiAllocationScope active(retry);int w=0,h=0,c=0;
          std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(stbi_load_from_memory(fixture.bytes.data(),static_cast<int>(fixture.bytes.size()),&w,&h,&c,components),&stbi_image_free);
          Require(pixels && w==domain.width && h==domain.height,"normal decode retry failed");CheckPixels(pixels.get(),fixture,components);
        }
        Require(!retry->GetSnapshot().live_request_bytes && !retry->GetSnapshot().reserved_request_bytes,"normal retry retained decoder storage");++retries;
        auto shortHeader=std::make_shared<UStbiAllocationLedger>(2*1024*1024);
        {UStbiAllocationScope active(shortHeader);int w=0,h=0,c=0;
          std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(stbi_load_from_memory(fixture.bytes.data(),2,&w,&h,&c,components),&stbi_image_free);
          Require(!pixels,"two-byte header unexpectedly decoded");
        }
        Require(!shortHeader->GetSnapshot().live_request_bytes && !shortHeader->GetSnapshot().reserved_request_bytes,"short header retained decoder storage");++shortHeaders;
        // Existing formats differ in EOF permissiveness. Record returned data
        // versus rejection; both outcomes must retire all allocations safely.
        auto truncated=fixture.bytes;truncated.pop_back();auto shortLedger=std::make_shared<UStbiAllocationLedger>(2*1024*1024);
        {UStbiAllocationScope active(shortLedger);int w=0,h=0,c=0;
          std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(stbi_load_from_memory(truncated.data(),static_cast<int>(truncated.size()),&w,&h,&c,components),&stbi_image_free);
          if(pixels) ++truncatedReturned;
        }
        Require(!shortLedger->GetSnapshot().live_request_bytes && !shortLedger->GetSnapshot().reserved_request_bytes,"truncated decode left credits");
        Require(shortLedger->GetSnapshot().peak_admitted_request_bytes<=bound.admitted_peak_upper,"truncated decode request peak exceeds source envelope");
      }
      covered|=bit;
    }
    Require(covered==layout.format_mask,"compiled ordinary format family lacks fixture");
    StbiSourceDomain overflow;overflow.format=StbiSourceFormat::Jpeg;overflow.width=overflow.height=std::numeric_limits<int>::max();
    Require(BoundStbiOrdinarySource(overflow).overflow,"source multiplication overflow not detected");
    StbiSourceDomain unsupported;unsupported.width=2;unsupported.height=2;unsupported.requested_components=2;
    Require(!BoundStbiOrdinarySource(unsupported).supported,"model accepted output configuration outside engine consumer scope");
    report["status"]="PASS";report["format_mask"]=covered;report["fixture_count"]=fixtures.size();
    report["persistent_allocation_failure_cases"]=failCases;report["truncated_returned_data_cases"]=truncatedReturned;
    report["byte_refusal_cases"]=byteRefusals;report["normal_retry_cases"]=retries;report["truncated_header_cases"]=shortHeaders;
    report["scope"]="Ordinary8bit RGB/RGBA9 families;not animated-all-frames/every subvariant/backend/CRT/stack/process budgets";save();
    std::cout << "stbi_source_formats_mask=" << covered << " fixtures=" << fixtures.size() << " file_memory_RGB_RGBA_cases=" << report["cases"].size()
        << " allocation_failure_cases=" << failCases << " truncated_returned=" << truncatedReturned << " final_live=0" << std::endl;
  } catch(const std::exception &error) {report["status"]="FAIL";report["error"]=error.what();save();std::cerr << error.what() << std::endl;return 1;}
}

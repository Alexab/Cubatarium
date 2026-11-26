#pragma once
#include "Core/Memory/GlogResourceSource.h"

namespace glog_resource_fixture
{
using namespace cutum;using F=GlogResourceField;
inline GlogResourceCensus Inspect()
{
  GlogResourceCensus s{};Require(CubatariumGlogResourceSource(s.data(),s.size()),"resource query");return s;
}
inline size_t Value(const GlogResourceCensus &s,F f){return GlogValue(s,f);}
inline Json Run()
{
  const std::string text(8192,'d');FLAGS_log_dir=text;
  google::SetEmailLogging(google::GLOG_FATAL,text.c_str());google::SetApplicationFingerprint(text);
  const auto expected_directory=text+"/";
  Json report={{"schema","glog-retained-resources.v1"},{"directory_cases",Json::array()},{"refusal_cases",Json::array()}};
  size_t failed=0;
  Require(!CubatariumGlogResourceSource(nullptr,32),"null resource query rejected");
  GlogResourceCensus bad{};Require(!CubatariumGlogResourceSource(bad.data(),31),"short resource query rejected");
  const auto directory=[&](size_t prefix){
    Require(Used<Records.size(),"directory record limit");auto &c=Records[Used++];
    google::InitGoogleLogging("resource_probe");c.fail_after=prefix;Active=&c;bool threw=false;
    try{(void)google::GetLoggingDirectories();}catch(const std::bad_alloc &){threw=true;}
    const auto after_failure=c.live.load();c.fail_after=SIZE_MAX;
    const auto &dirs=google::GetLoggingDirectories();const bool recovered=dirs.size()==1 && dirs[0]==expected_directory;
    const auto attempts=c.attempts.load(),retained=c.live.load();google::ShutdownGoogleLogging();Active=nullptr;
    return Json{{"prefix",prefix},{"threw",threw},{"after_failure",after_failure},{"attempts",attempts},
      {"retained",retained},{"exact_retry",recovered},{"final0",c.live==0&&c.blocks==0}};
  };
  const auto normal_directory=directory(SIZE_MAX);
  Require(normal_directory["exact_retry"]==true && normal_directory["final0"]==true,"normal directory output");
  const auto directory_attempts=normal_directory["attempts"].get<size_t>();
  for(size_t i=0;i<directory_attempts;++i){const auto c=directory(i);
    if(c["threw"]!=true || c["exact_retry"]!=true || c["final0"]!=true || c["after_failure"]!=0)++failed;
    report["directory_cases"].push_back(c);
  }
  const auto build=[&]{
    for(int i=0;i<google::NUM_SEVERITIES;++i){
      google::SetLogDestination(static_cast<google::LogSeverity>(i),text.c_str());
      google::SetLogSymlink(static_cast<google::LogSeverity>(i),text.c_str());
    }
    google::SetLogFilenameExtension(text.c_str());
    for(int i=0;i<64;++i)google::AddLogSink(&Capture);
    google::InstallPrefixFormatter(Prefix,nullptr);(void)google::GetLoggingDirectories();
  };
  GlogResourceDomain domain;domain.sink_capacity_upper=128;domain.directory_capacity_upper=1;
  domain.string_request_upper=std::max(Value(Inspect(),F::MaxStringRequest),2*(text.size()+1)+95);
  domain.history_known=domain.external_owners_known=true;
  google::InitGoogleLogging("resource_probe");const auto entry=Inspect();
  Require(Used<Records.size(),"resource record limit");auto &c=Records[Used++];Active=&c;build();
  const auto normal_attempts=c.attempts.load(),normal_live=c.live.load(),normal_peak=c.peak.load();
  const auto full=Inspect();const auto bound=BoundGlogResources(full,domain);
  bool exact=Value(full,F::RetainedRequests)==Value(entry,F::RetainedRequests)+normal_live;
  exact=exact && Value(full,F::Destinations)==4 && Value(full,F::SinkCount)==64 && Value(full,F::DirectoryCount)==1 &&
    Value(full,F::PrefixPresent)==1 && bound.supported && !bound.overflow && Value(full,F::RetainedRequests)<=bound.retained_upper &&
    Value(entry,F::RetainedRequests)+normal_peak<=bound.configuration_peak_upper;
  if(!exact)++failed;
  const auto query_attempts=c.attempts.load();c.fail_after=query_attempts;const auto under_refusal=Inspect();
  const bool query_zero=c.attempts==query_attempts && full==under_refusal;c.fail_after=SIZE_MAX;
  if(!query_zero)++failed;
  google::RemoveLogSink(&Capture);const auto removed=Inspect();
  const bool capacity_kept=Value(removed,F::SinkCount)==0 && Value(removed,F::SinkCapacity)==Value(full,F::SinkCapacity) &&
    Value(removed,F::RetainedRequests)==Value(full,F::RetainedRequests);
  if(!capacity_kept)++failed;
  auto absent_history=domain;absent_history.sink_capacity_upper=0;
  auto unknown=domain;unknown.history_known=false;
  auto caller=domain;caller.external_owners_known=false;
  auto missing_dir=domain;missing_dir.directory_capacity_upper=0;
  auto missing_text=domain;missing_text.string_request_upper=0;
  auto huge=domain;huge.sink_capacity_upper=huge.directory_capacity_upper=huge.string_request_upper=SIZE_MAX;
  auto unsupported=full;unsupported[static_cast<size_t>(F::Supported)]=0;
  const bool controls=!BoundGlogResources(removed,absent_history).supported && !BoundGlogResources(full,unknown).supported &&
    !BoundGlogResources(full,caller).supported && !BoundGlogResources(full,missing_dir).supported &&
    !BoundGlogResources(full,missing_text).supported && !BoundGlogResources(unsupported,domain).supported && BoundGlogResources(full,huge).overflow;
  if(!controls)++failed;
  const auto stable=c.live.load();c.peak=stable;google::InstallPrefixFormatter(Prefix,nullptr);
  const bool prefix_overlap=c.live==stable && c.peak==stable+Value(full,F::PrefixBytes);
  c.fail_after=c.attempts.load();bool refused_prefix=false;
  try{google::InstallPrefixFormatter(Prefix,nullptr);}catch(const std::bad_alloc &){refused_prefix=true;}
  const bool prefix_preserved=refused_prefix && Value(Inspect(),F::PrefixPresent)==1 && c.live==stable;
  c.fail_after=SIZE_MAX;google::InstallPrefixFormatter(Prefix,nullptr);
  if(!prefix_overlap || !prefix_preserved)++failed;
  struct Custom final:google::base::Logger {
    std::string text;bool *retired;
    explicit Custom(bool &r):text(8192,'x'),retired(&r){}
    ~Custom()override{*retired=true;}
    void Write(bool,const std::chrono::system_clock::time_point &,const char*,size_t)override{}
    void Flush()override{}google::uint32 LogSize()override{return 0;}
  };
  auto *original=google::base::GetLogger(google::GLOG_INFO);bool retired=false;
  google::base::SetLogger(google::GLOG_INFO,new Custom(retired));const auto custom=Inspect();
  auto known_custom=domain;known_custom.external_owner_requests=sizeof(Custom)+2*(text.size()+1)+95;
  const auto custom_bound=BoundGlogResources(custom,known_custom);
  const bool external=Value(custom,F::CustomLoggers)==1 && !BoundGlogResources(custom,domain).supported &&
    custom_bound.supported && Value(entry,F::RetainedRequests)+c.live<=custom_bound.retained_upper;
  google::base::SetLogger(google::GLOG_INFO,original);
  if(!external || !retired || c.live!=stable)++failed;
  const auto before_shutdown=c.attempts.load();c.fail_after=before_shutdown;google::ShutdownGoogleLogging();
  const auto after=Inspect();Active=nullptr;
  const bool cleanup=c.live==0 && c.blocks==0 && c.attempts==before_shutdown && Value(after,F::Destinations)==0 &&
    Value(after,F::SinkCapacity)==0 && Value(after,F::DirectoryCapacity)==0 && Value(after,F::PrefixPresent)==0 &&
    Value(after,F::RetainedRequests)==Value(entry,F::RetainedRequests);
  if(!cleanup)++failed;
  for(size_t prefix=0;prefix<normal_attempts;++prefix){
    Require(Used<Records.size(),"resource refusal record limit");auto &r=Records[Used++];r.fail_after=prefix;
    google::InitGoogleLogging("resource_probe");Active=&r;bool threw=false;
    try{build();}catch(const std::bad_alloc &){threw=true;}
    const auto partial=Inspect();const auto partial_bound=BoundGlogResources(partial,domain);
    const auto attempts=r.attempts.load();google::ShutdownGoogleLogging();Active=nullptr;
    const bool cleanup_zero=r.attempts==attempts;
    const bool clean=threw && partial_bound.supported && !partial_bound.overflow && Value(entry,F::RetainedRequests)+r.peak<=partial_bound.configuration_peak_upper &&
      r.live==0 && r.blocks==0 && r.attempts==attempts;
    if(!clean)++failed;
    google::InitGoogleLogging("resource_probe");Active=&r;r.fail_after=SIZE_MAX;build();
    const auto retry=Inspect();const bool retry_ok=Value(retry,F::Destinations)==4 && Value(retry,F::SinkCount)==64 && Value(retry,F::DirectoryCount)==1;
    google::ShutdownGoogleLogging();Active=nullptr;
    if(!retry_ok || r.live || r.blocks)++failed;
    report["refusal_cases"].push_back({{"prefix",prefix},{"threw",threw},{"shutdown_allocates0",cleanup_zero},
      {"clean",clean},{"fresh_exact_retry",retry_ok},{"final0",r.live==0&&r.blocks==0}});
  }
  report["full"]=full;report["removed"]=removed;report["after_shutdown"]=after;
  report["entry_retained"]=Value(entry,F::RetainedRequests);report["normal_observed_retained"]=normal_live;
  report["normal_attempts"]=normal_attempts;report["retained_upper"]=bound.retained_upper;report["configuration_peak_upper"]=bound.configuration_peak_upper;
  report["normal_observed_peak"]=normal_peak;
  report["query_allocates0"]=query_zero;report["retained_capacity_counted"]=capacity_kept;report["prefix_old_new_overlap"]=prefix_overlap;
  report["prefix_refusal_preserves_old"]=prefix_preserved;report["external_owner_profile"]=external;report["shutdown_allocates0_final0"]=cleanup;
  report["history_missing_unknown_and_overflow_controls"]=controls;report["exact_retained_delta"]=exact;
  report["process_string_roots_remain_bounded"]=Value(after,F::ProcessStringRequests);
  report["failed_cases"]=failed;report["status"]=failed?"FAIL":"PASS";
  report["scope"]="Actual selected glog retained C++ roots/flags/configuration. Directory retries stay in the same initialization;partial multi-call configuration retries start after shutdown,not batch rollback. Process strings persist within explicit capacity history;stdio/CRT/locale/cleanup scans,VLOG/symbolizer/signal/utility and full process remain separate.";
  return report;
}
} // namespace glog_resource_fixture

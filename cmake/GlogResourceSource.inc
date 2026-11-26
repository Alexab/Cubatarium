
// Cubatarium:resource census v1. Read-only selected retained C++ source graph.
#include "Core/Memory/GlogResourceSource.h"
#include "glog/flags.h"
namespace google {
struct CubatariumGlogResourceAccess {
  using F=cutum::GlogResourceField;
  static size_t &V(size_t *o,F f) noexcept {return o[static_cast<size_t>(f)];}
  static void Add(size_t *o,F f,size_t n) noexcept {
    auto &v=V(o,f);if(n>SIZE_MAX-v){v=SIZE_MAX;V(o,F::Overflow)=1;}else v+=n;
  }
  static size_t Request(size_t *o,size_t n) noexcept {
    if(n>=4096){if(n>SIZE_MAX-39){V(o,F::Overflow)=1;return SIZE_MAX;}n+=39;}return n;
  }
  static size_t Text(size_t *o,const std::string &s,F category) noexcept {
    Add(o,F::StringSlots,1);size_t n=0;
    const auto data=reinterpret_cast<uintptr_t>(s.data()),object=reinterpret_cast<uintptr_t>(&s);
    if(data<object || data-object>=sizeof(s)){
      if(s.capacity()==SIZE_MAX){V(o,F::Overflow)=1;n=SIZE_MAX;}else n=Request(o,s.capacity()+1);
    }
    Add(o,F::StringRequests,n);Add(o,F::RetainedRequests,n);Add(o,category,n);
    V(o,F::MaxStringRequest)=(std::max)(V(o,F::MaxStringRequest),n);return n;
  }
  static void ProcessText(size_t *o,const std::string &s) noexcept {
    Add(o,F::ProcessStringSlots,1);Text(o,s,F::ProcessStringRequests);
  }
  static void Flag(size_t *o,size_t bytes) noexcept {
    Add(o,F::FlagCount,1);Add(o,F::FlagStaticInline,bytes);Add(o,F::StaticInline,bytes);
  }
  static bool Inspect(size_t *o,size_t count) noexcept {
    if(!o || count!=static_cast<size_t>(F::Count))return false;
    for(size_t i=0;i<count;++i)o[i]=0;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0 && !defined(GLOG_USE_GFLAGS)
    V(o,F::Supported)=1;
#endif
    V(o,F::Version)=1;V(o,F::DestinationBytes)=sizeof(LogDestination);
    V(o,F::FileObjectBytes)=sizeof(LogFileObject);V(o,F::PrefixBytes)=sizeof(PrefixFormatter);
    V(o,F::SinkVectorBytes)=sizeof(std::vector<LogSink*>);V(o,F::DirectoryVectorBytes)=sizeof(std::vector<std::string>);
    V(o,F::StringObjectBytes)=sizeof(std::string);V(o,F::DestinationLimit)=NUM_SEVERITIES;
    V(o,F::StaticInline)=sizeof(LogDestination::log_destinations_)+sizeof(LogDestination::email_logging_severity_)+
      sizeof(LogDestination::addresses_)+sizeof(LogDestination::hostname_)+sizeof(LogDestination::terminal_supports_color_)+
      sizeof(LogDestination::sinks_)+sizeof(LogDestination::sink_mutex_)+sizeof(g_prefix_formatter)+sizeof(log_cleaner)+
      sizeof(logging_directories_list)+sizeof(g_application_fingerprint);
    std::lock_guard<std::mutex> log_lock(log_mutex);
    std::lock_guard<LogDestination::SinkMutex> sink_lock(LogDestination::sink_mutex_);
    for(const auto &p:LogDestination::log_destinations_)if(p){
      Add(o,F::Destinations,1);Add(o,F::RetainedRequests,sizeof(LogDestination));
      std::lock_guard<std::mutex> file_lock(p->fileobject_.mutex_);
      Text(o,p->fileobject_.base_filename_,F::DestinationStringRequests);
      Text(o,p->fileobject_.symlink_basename_,F::DestinationStringRequests);
      Text(o,p->fileobject_.filename_extension_,F::DestinationStringRequests);
      if(p->logger_!=&p->fileobject_)Add(o,F::CustomLoggers,1);
      if(p->fileobject_.file_)Add(o,F::OpenFiles,1);
    }
    ProcessText(o,LogDestination::addresses_);ProcessText(o,LogDestination::hostname_);ProcessText(o,g_application_fingerprint);
    if(LogDestination::sinks_){
      const auto &s=*LogDestination::sinks_;V(o,F::SinkCount)=s.size();V(o,F::SinkCapacity)=s.capacity();
      const auto n=Request(o,s.capacity()*sizeof(LogSink*));V(o,F::SinkArrayRequests)=n;
      Add(o,F::RetainedRequests,sizeof(s));Add(o,F::RetainedRequests,n);
    }
    if(logging_directories_list){
      const auto &d=*logging_directories_list;V(o,F::DirectoryCount)=d.size();V(o,F::DirectoryCapacity)=d.capacity();
      const auto n=Request(o,d.capacity()*sizeof(std::string));V(o,F::DirectoryArrayRequests)=n;
      Add(o,F::RetainedRequests,sizeof(d));Add(o,F::RetainedRequests,n);
      for(const auto &s:d)Text(o,s,F::DirectoryStringRequests);
    }
    if(g_prefix_formatter){V(o,F::PrefixPresent)=1;Add(o,F::RetainedRequests,sizeof(PrefixFormatter));}
@FLAG_ACCOUNTING@
    return true;
  }
};
} // namespace google
extern "C" bool CubatariumGlogResourceSource(size_t *out,size_t count) noexcept {
  return google::CubatariumGlogResourceAccess::Inspect(out,count);
}

# Local lifetime cleanup and allocation-free source layout query for glog0.7.1.
# Keep the original source copyright/license;the archive is SHA256 pinned.
set(_file "${SOURCE_DIR}/src/logging.cc")
file(READ "${_file}" _source)
string(FIND "${_source}" "Cubatarium: keep initialized data owned across Init failure" _patched)
if(_patched EQUAL -1)
  file(SHA256 "${_file}" _original_hash)
  if(NOT _original_hash STREQUAL "f953539658a4f4fe41b4fad6fedfc73e29b77065aa5375bfc8e32abbb857567c")
    message(FATAL_ERROR "Unexpected glog logging.cc;refusing to apply lifetime patch")
  endif()
  set(_before [=[  allocated_ = nullptr;
  if (severity != GLOG_FATAL || !exit_on_dfatal) {]=])
  set(_after [=[  allocated_ = nullptr;
  // Cubatarium: keep initialized data owned across Init failure. A failure
  // inside the placement constructor must also return the unconstructed slot.
  bool thread_claimed = false;
  bool data_constructed = false;
  auto discard_init = [this, &thread_claimed, &data_constructed](LogMessage*) noexcept {
#ifdef GLOG_THREAD_LOCAL_STORAGE
    if (thread_claimed) {
      if (data_constructed) data_->~LogMessageData();
      thread_data_available = true;
      return;
    }
#endif
    delete allocated_;
  };
  std::unique_ptr<LogMessage, decltype(discard_init)> init_owner(this, discard_init);
  if (severity != GLOG_FATAL || !exit_on_dfatal) {]=])
  string(REPLACE "${_before}" "${_after}" _source "${_source}")
  string(REPLACE "      thread_data_available = false;\n      data_ = new (&thread_msg_data)"
    "      thread_data_available = false;\n      thread_claimed = true;\n      data_ = new (&thread_msg_data)" _source "${_source}")
  string(REPLACE "  data_->preserved_errno_ = errno;"
    "  data_constructed = true;\n  data_->preserved_errno_ = errno;" _source "${_source}")
  set(_before [=[  }
}

LogSeverity LogMessage::severity() const noexcept]=])
  set(_after [=[  }
  init_owner.release();
}

LogSeverity LogMessage::severity() const noexcept]=])
  string(REPLACE "${_before}" "${_after}" _source "${_source}")
  set(_before [=[LogMessage::~LogMessage() noexcept(false) {
  Flush();
  bool fail = data_->severity_ == GLOG_FATAL && exit_on_dfatal;
#ifdef GLOG_THREAD_LOCAL_STORAGE
  if (data_ == static_cast<void*>(&thread_msg_data)) {
    data_->~LogMessageData();
    thread_data_available = true;
  } else {
    delete allocated_;
  }
#else   // !defined(GLOG_THREAD_LOCAL_STORAGE)
  delete allocated_;
#endif  // defined(GLOG_THREAD_LOCAL_STORAGE)]=])
  set(_after [=[LogMessage::~LogMessage() noexcept(false) {
  // Cubatarium: Flush may throw (for example from a caller sink). Retire data
  // and restore the TLS slot during that unwind,as on the successful path.
  auto retire_data = [this](LogMessage*) noexcept {
#ifdef GLOG_THREAD_LOCAL_STORAGE
    if (data_ == static_cast<void*>(&thread_msg_data)) {
      data_->~LogMessageData();
      thread_data_available = true;
    } else {
      delete allocated_;
    }
#else
    delete allocated_;
#endif
  };
  std::unique_ptr<LogMessage, decltype(retire_data)> data_owner(this, retire_data);
  Flush();
  bool fail = data_->severity_ == GLOG_FATAL && exit_on_dfatal;
  data_owner.reset();]=])
  string(REPLACE "${_before}" "${_after}" _source "${_source}")
  string(FIND "${_source}" "  init_owner.release();" _init_done)
  string(FIND "${_source}" "  data_owner.reset();" _dtor_done)
  if(_init_done EQUAL -1 OR _dtor_done EQUAL -1)
    message(FATAL_ERROR "glog lifetime patch anchors did not match")
  endif()
  file(WRITE "${_file}" "${_source}")
endif()
file(READ "${_file}" _source)
string(FIND "${_source}" "CubatariumGlogMessageSourceLayout" _has_layout)
if(_has_layout EQUAL -1)
  file(APPEND "${_file}" [=[

// Cubatarium:compiled source sizes only;no owner,counter,TLS or allocation.
extern "C" bool CubatariumGlogMessageSourceLayout(size_t *out,size_t count) noexcept {
  if(!out || count!=8)return false;
  using namespace google;
  using Data=google::logging::internal::LogMessageData;
  out[0]=sizeof(Data);out[1]=alignof(Data);
#ifdef GLOG_THREAD_LOCAL_STORAGE
  out[2]=sizeof(thread_msg_data);out[3]=sizeof(thread_data_available);
#else
  out[2]=0;out[3]=0;
#endif
  out[4]=sizeof(fatal_msg_data_exclusive)+sizeof(fatal_msg_data_shared);
  out[5]=sizeof(Data::message_text_);out[6]=sizeof(LogMessage::LogStream);
  out[7]=sizeof(log_mutex)+sizeof(fatal_msg_lock)+sizeof(crash_reason)+
    sizeof(fatal_msg_exclusive)+sizeof(stop_writing)+sizeof(exit_on_dfatal)+
    sizeof(fatal_time)+sizeof(fatal_message)+sizeof(LogSeverityNames)+NUM_SEVERITIES*sizeof(int64);
  return true;
}
]=])
endif()
file(READ "${_file}" _source)
string(FIND "${_source}" "\n// Cubatarium:resource census v1." _resource_start)
if(NOT _resource_start EQUAL -1)
  string(SUBSTRING "${_source}" 0 ${_resource_start} _source)
endif()
string(FIND "${_source}" "struct CubatariumGlogResourceAccess;" _friend_done)
if(_friend_done EQUAL -1)
  string(REPLACE "namespace google {\n" "namespace google {\nstruct CubatariumGlogResourceAccess;\n" _source "${_source}")
  foreach(_class "class LogFileObject : public base::Logger {" "class LogDestination {")
    string(REPLACE "${_class}\n public:" "${_class}\n public:\n  friend struct ::google::CubatariumGlogResourceAccess;" _source "${_source}")
  endforeach()
endif()
string(FIND "${_source}" "publish complete directory list" _directory_done)
if(_directory_done EQUAL -1)
  string(FIND "${_source}" "const vector<string>& GetLoggingDirectories() {" _dir_start)
  string(FIND "${_source}" "// Returns a set of existing temporary directories" _dir_end)
  math(EXPR _dir_len "${_dir_end}-${_dir_start}")
  string(SUBSTRING "${_source}" ${_dir_start} ${_dir_len} _old_dir)
  set(_new_dir "${_old_dir}")
  string(REPLACE "logging_directories_list = std::make_unique<std::vector<std::string>>();" "auto directories = std::make_unique<std::vector<std::string>>();" _new_dir "${_new_dir}")
  string(REPLACE "logging_directories_list->" "directories->" _new_dir "${_new_dir}")
  string(REPLACE "GetTempDirectories(*logging_directories_list)" "GetTempDirectories(*directories)" _new_dir "${_new_dir}")
  string(REPLACE "  }\n  return *logging_directories_list;" "    // Cubatarium:publish complete directory list after successful construction.\n    logging_directories_list = std::move(directories);\n  }\n  return *logging_directories_list;" _new_dir "${_new_dir}")
  string(REPLACE "${_old_dir}" "${_new_dir}" _source "${_source}")
endif()
file(READ "${SOURCE_DIR}/src/flags.cc" _flags)
string(REGEX MATCHALL "GLOG_DEFINE_(bool|int32|uint32|string)\\([ \t\r\n]*[A-Za-z_][A-Za-z_0-9]*" _flag_defs "${_flags}")
set(FLAG_ACCOUNTING "")
foreach(_definition IN LISTS _flag_defs)
  string(REGEX REPLACE "GLOG_DEFINE_([a-z0-9]+)\\([ \t\r\n]*([A-Za-z_][A-Za-z_0-9]*)" "\\1;\\2" _parts "${_definition}")
  list(GET _parts 0 _kind)
  list(GET _parts 1 _name)
  if(_name STREQUAL "drop_log_memory")
    string(APPEND FLAG_ACCOUNTING "#ifdef GLOG_OS_LINUX\n")
  endif()
  if(_kind STREQUAL "string")
    string(APPEND FLAG_ACCOUNTING "    Flag(o,sizeof(FLAGS_${_name})+sizeof(void*)+sizeof(char));ProcessText(o,FLAGS_${_name});\n")
  else()
    string(APPEND FLAG_ACCOUNTING "    Flag(o,sizeof(FLAGS_${_name})+sizeof(char));\n")
  endif()
  if(_name STREQUAL "drop_log_memory")
    string(APPEND FLAG_ACCOUNTING "#endif\n")
  endif()
endforeach()
file(READ "${CMAKE_CURRENT_LIST_DIR}/GlogResourceSource.inc" _resource_template)
string(CONFIGURE "${_resource_template}" _resource @ONLY)
string(CONCAT _candidate "${_source}" "${_resource}")
file(READ "${_file}" _current)
if(NOT _current STREQUAL _candidate)
  file(WRITE "${_file}" "${_candidate}")
endif()
file(SHA256 "${_file}" _patched_hash)
if(DEFINED EXPECTED_PATCH_HASH AND NOT _patched_hash STREQUAL EXPECTED_PATCH_HASH)
  message(FATAL_ERROR "glog lifetime source hash mismatch: ${_patched_hash}")
endif()
message(STATUS "glog lifetime source SHA256: ${_patched_hash}")

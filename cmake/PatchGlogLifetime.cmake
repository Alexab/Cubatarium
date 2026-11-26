# Local lifetime-only change to BSD-licensed upstream glog v0.7.1.
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
file(SHA256 "${_file}" _patched_hash)
if(DEFINED EXPECTED_PATCH_HASH AND NOT _patched_hash STREQUAL EXPECTED_PATCH_HASH)
  message(FATAL_ERROR "glog lifetime source hash mismatch: ${_patched_hash}")
endif()
message(STATUS "glog lifetime source SHA256: ${_patched_hash}")

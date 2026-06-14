#include "App/Platform/Log.h"

#include <iostream>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace cutum
{

void CubatariumLogInfo(const char *tag, const std::string &msg)
{
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_INFO, tag, "%s", msg.c_str());
#else
  std::cerr << '[' << tag << "] " << msg << std::endl;
#endif
}

void CubatariumLogError(const char *tag, const std::string &msg)
{
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_ERROR, tag, "%s", msg.c_str());
#else
  std::cerr << '[' << tag << "] ERROR: " << msg << std::endl;
#endif
}

} // namespace cutum

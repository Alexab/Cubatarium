#ifndef CUBATARIUM_LOG_H
#define CUBATARIUM_LOG_H

#include <string>

namespace cutum
{

void CubatariumInitLogging(const char *argv0, bool also_log_to_stderr = false);
void CubatariumShutdownLogging();

void CubatariumLogInfo(const char *tag, const std::string &msg);
void CubatariumLogError(const char *tag, const std::string &msg);
void CubatariumSetSuppressErrorDialogs(bool suppress);

/// Installs native crash logging (stack + minidump under bin/logs/).
void CubatariumInstallWindowsDiagnostics();

void CubatariumSetWorkerJobKind(const char *kind);
const char *CubatariumGetWorkerJobKind();

#ifdef _WIN32
void CubatariumAttachParentConsole();
void CubatariumAttachWindowsConsole();
#else
inline void CubatariumAttachParentConsole() {}
inline void CubatariumAttachWindowsConsole() {}
#endif

} // namespace cutum

#endif

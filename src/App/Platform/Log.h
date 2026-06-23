#ifndef CUBATARIUM_LOG_H
#define CUBATARIUM_LOG_H

#include <string>

namespace cutum
{

void CubatariumLogInfo(const char *tag, const std::string &msg);
void CubatariumLogError(const char *tag, const std::string &msg);

#ifdef _WIN32
void CubatariumInstallWindowsDiagnostics();
void CubatariumAttachParentConsole();
void CubatariumAttachWindowsConsole();
#else
inline void CubatariumInstallWindowsDiagnostics() {}
inline void CubatariumAttachParentConsole() {}
inline void CubatariumAttachWindowsConsole() {}
#endif

} // namespace cutum

#endif

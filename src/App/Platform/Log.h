#ifndef CUBATARIUM_LOG_H
#define CUBATARIUM_LOG_H

#include <string>

namespace cutum
{

void CubatariumLogInfo(const char *tag, const std::string &msg);
void CubatariumLogError(const char *tag, const std::string &msg);

} // namespace cutum

#endif

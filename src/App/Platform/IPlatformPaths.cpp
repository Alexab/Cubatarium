#include "App/Platform/IPlatformPaths.h"

#include <stdexcept>

namespace cutum
{

std::shared_ptr<IPlatformPaths> IPlatformPaths::Global;

void IPlatformPaths::SetGlobal(std::shared_ptr<IPlatformPaths> paths)
{
  Global = std::move(paths);
}

IPlatformPaths &IPlatformPaths::Current()
{
  if (!Global)
  {
    throw std::runtime_error(
        "IPlatformPaths::Current() called before SetGlobal");
  }
  return *Global;
}

IPlatformPaths *IPlatformPaths::TryGet() { return Global.get(); }

} // namespace cutum

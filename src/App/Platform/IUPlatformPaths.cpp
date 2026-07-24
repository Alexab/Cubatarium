#include "App/Platform/IUPlatformPaths.h"

#include <stdexcept>

namespace cutum
{

std::shared_ptr<IUPlatformPaths> IUPlatformPaths::Global;

void IUPlatformPaths::SetGlobal(std::shared_ptr<IUPlatformPaths> paths)
{
  Global = std::move(paths);
}

IUPlatformPaths &IUPlatformPaths::Current()
{
  if (!Global)
  {
    throw std::runtime_error(
        "IUPlatformPaths::Current() called before SetGlobal");
  }
  return *Global;
}

IUPlatformPaths *IUPlatformPaths::TryGet() { return Global.get(); }

} // namespace cutum

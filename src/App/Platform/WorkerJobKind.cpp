#include "App/Platform/Log.h"
#include "Core/Memory/RuntimeInlineSource.h"

namespace cutum
{

namespace
{
thread_local const char *gWorkerJobKind = "Main";
}

void CubatariumSetWorkerJobKind(const char *kind)
{
  gWorkerJobKind = kind ? kind : "Unknown";
}

const char *CubatariumGetWorkerJobKind()
{
  return gWorkerJobKind ? gWorkerJobKind : "Unknown";
}

size_t GetWorkerRoleRuntimeInlineBytes() noexcept {return sizeof(gWorkerJobKind);}

} // namespace cutum

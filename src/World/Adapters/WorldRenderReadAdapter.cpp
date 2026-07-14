#include "World/Adapters/WorldRenderReadAdapter.h"
#include "World/Core/World.h"
#include "World/Mesh/WorldMeshService.h"

namespace cutum
{

namespace
{
const std::string &EmptyString()
{
  static const std::string kEmpty;
  return kEmpty;
}
} // namespace

UWorldRenderReadAdapter::UWorldRenderReadAdapter(std::shared_ptr<UWorld> world)
    : World(std::move(world))
{
}

std::shared_ptr<UCamera> UWorldRenderReadAdapter::GetCurrentUserCamera() const
{
  return World ? World->GetCurrentUserCamera() : nullptr;
}

std::string UWorldRenderReadAdapter::GetWorldName() const
{
  return World ? World->GetWorldName() : std::string{};
}

const std::string &UWorldRenderReadAdapter::GetCurrentUserName() const
{
  return World ? World->GetCurrentUserName() : EmptyString();
}

UWorldMeshService *UWorldRenderReadAdapter::TryGetMeshService() const
{
  return World ? &World->GetMeshService() : nullptr;
}

} // namespace cutum

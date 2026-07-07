#ifndef WORLD_RENDER_READ_ADAPTER_H
#define WORLD_RENDER_READ_ADAPTER_H

#include "World/Interfaces/IUWorldRenderReadModel.h"
#include <memory>

namespace cutum
{

class UWorld;

class UWorldRenderReadAdapter : public IUWorldRenderReadModel
{
public:
  explicit UWorldRenderReadAdapter(std::shared_ptr<UWorld> world);

  std::shared_ptr<UCamera> GetCurrentUserCamera() const override;
  std::string GetWorldName() const override;
  const std::string &GetCurrentUserName() const override;
  const UWorldMeshService *TryGetMeshService() const override;

private:
  std::shared_ptr<UWorld> World;
};

} // namespace cutum

#endif

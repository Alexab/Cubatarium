#ifndef IU_WORLD_RENDER_READ_MODEL_H
#define IU_WORLD_RENDER_READ_MODEL_H

#include <memory>
#include <string>

namespace cutum
{

class UCamera;
class UWorldMeshService;

class IUWorldRenderReadModel
{
public:
  virtual ~IUWorldRenderReadModel() = default;
  virtual std::shared_ptr<UCamera> GetCurrentUserCamera() const = 0;
  virtual std::string GetWorldName() const = 0;
  virtual const std::string &GetCurrentUserName() const = 0;
  virtual const UWorldMeshService *TryGetMeshService() const = 0;
};

} // namespace cutum

#endif

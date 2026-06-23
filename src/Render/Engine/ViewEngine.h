#ifndef VIEWENGINE_H
#define VIEWENGINE_H

#include "Render/Engine/GeometryEngine.h"
#include "Render/Camera/Camera.h"
#include <chrono>

namespace cutum
{

class UViewEngine
{
public:
  UViewEngine();

  void GenerateSimpleCamera();

  bool AddCamera(std::shared_ptr<UCamera> camera);
  size_t AddCameraReturnId(std::shared_ptr<UCamera> camera);
  bool DelCamera(size_t index);

  std::shared_ptr<UCamera> GetActiveCamera() const;
  std::shared_ptr<UCamera> GetCamera(size_t index) const;
  bool SetActiveCamera(size_t index);

  void UpdateFrameTime();
  void ResetAllKeyStatus();

  uint64_t GetDurationUpdateMks() const;

private:
  std::map<size_t, std::shared_ptr<UCamera>> Cameras;

  size_t ActiveViewIndex;

  uint64_t DurationUpdateMks;
};

} // namespace cutum

#endif // VIEWENGINE_H

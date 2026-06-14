#include "ViewEngine.h"
#include <glm/glm.hpp>

namespace cutum
{

UViewEngine::UViewEngine()
{
  ActiveViewIndex = 0;
  GenerateSimpleCamera();
}

void UViewEngine::GenerateSimpleCamera()
{
  // Above default flat terrain (top layer y=3)
  std::shared_ptr<UCamera> view = std::make_shared<UCamera>(
      glm::vec3(0.0f, 6.0f, 5.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 0.0f);
  view->SetFreeMove(false); // Disable free movement to enable collisions
  AddCamera(view);
}

bool UViewEngine::AddCamera(std::shared_ptr<UCamera> camera)
{
  AddCameraReturnId(camera);
  return camera != nullptr;
}

size_t UViewEngine::AddCameraReturnId(std::shared_ptr<UCamera> camera)
{
  if (camera == nullptr)
    return 0;

  camera->SetViewEngine(this);
  size_t newId = 0;
  if (Cameras.empty())
  {
    newId = 0;
    Cameras[0] = camera;
  }
  else
  {
    newId = Cameras.crbegin()->first + 1;
    Cameras[newId] = camera;
  }
  return newId;
}

bool UViewEngine::DelCamera(size_t index)
{
  auto I = Cameras.find(index);
  if (I == Cameras.end())
    return true;

  I->second->SetViewEngine(nullptr);
  Cameras.erase(I);

  auto J = Cameras.find(ActiveViewIndex);

  if (J == Cameras.end())
    ActiveViewIndex = 0;

  return true;
}

std::shared_ptr<UCamera> UViewEngine::GetActiveCamera() const
{
  auto I = Cameras.find(ActiveViewIndex);
  if (I == Cameras.end())
    return nullptr;

  return I->second;
}

std::shared_ptr<UCamera> UViewEngine::GetCamera(size_t index) const
{
  auto I = Cameras.find(index);
  if (I == Cameras.end())
    return nullptr;
  return Cameras.at(index);
}

bool UViewEngine::SetActiveCamera(size_t index)
{
  auto I = Cameras.find(index);
  if (I == Cameras.end())
    return false;

  ActiveViewIndex = index;
  return true;
}

void UViewEngine::UpdateFrameTime()
{
  auto t_begin = std::chrono::high_resolution_clock::now();
  for (auto &camera_item : Cameras)
    camera_item.second->UpdateFrameTime();
  auto t_view_end = std::chrono::high_resolution_clock::now();
  DurationUpdateMks = static_cast<uint64_t>(
      std::chrono::duration<double, std::micro>(t_view_end - t_begin).count());
}

void UViewEngine::ResetAllKeyStatus()
{
  for (auto &camera_item : Cameras)
    camera_item.second->ResetAllKeyStatus();
}

uint64_t UViewEngine::GetDurationUpdateMks() const { return DurationUpdateMks; }

} // namespace cutum

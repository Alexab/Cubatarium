#include "ViewEngine.h"

namespace cutum {

ViewEngine::ViewEngine()
{
 ActiveViewIndex = 0;
 GenerateSimpleCamera();
}

void ViewEngine::GenerateSimpleCamera()
{
 std::shared_ptr<Camera> view = std::make_shared<Camera>(QVector3D(0.0f, 2.0f, 3.0f));
 AddCamera(view);
}

bool ViewEngine::AddCamera(std::shared_ptr<Camera> camera)
{
 if(camera == nullptr)
  return false;

 camera->SetViewEngine(this);
 if(Cameras.empty())
 {
  Cameras[0] = camera;
 }
 else
 {
  Cameras[Cameras.cbegin()->first+1] = camera;
 }
 return true;
}

bool ViewEngine::DelCamera(size_t index)
{
 auto I = Cameras.find(index);
 if(I == Cameras.end())
  return true;

 I->second->SetViewEngine(nullptr);
 Cameras.erase(I);

 auto J = Cameras.find(ActiveViewIndex);

 if(J == Cameras.end())
  ActiveViewIndex = 0;

 return true;
}

std::shared_ptr<Camera> ViewEngine::GetActiveCamera() const
{
 auto I = Cameras.find(ActiveViewIndex);
 if(I == Cameras.end())
  return nullptr;

 return I->second;
}

std::shared_ptr<Camera> ViewEngine::GetCamera(size_t index) const
{
 auto I = Cameras.find(index);
 if(I == Cameras.end())
  return nullptr;
 return Cameras.at(index);
}

bool ViewEngine::SetActiveCamera(size_t index)
{
 auto I = Cameras.find(index);
 if(I == Cameras.end())
  return false;

 ActiveViewIndex = index;
 return true;
}

void ViewEngine::UpdateFrameTime()
{
 for(auto & camera_item : Cameras)
  camera_item.second->UpdateFrameTime();
}

void ViewEngine::ResetAllKeyStatus()
{
 for(auto & camera_item : Cameras)
  camera_item.second->ResetAllKeyStatus();
}


}

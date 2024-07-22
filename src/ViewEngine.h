#ifndef VIEWENGINE_H
#define VIEWENGINE_H

#include <chrono>
#include "geometryengine.h"
#include "Camera.h"

namespace cutum {

class ViewEngine
{
public:
 ViewEngine();

 void GenerateSimpleCamera();

 bool AddCamera(std::shared_ptr<Camera> camera);
 bool DelCamera(size_t index);

 std::shared_ptr<Camera> GetActiveCamera() const;
 std::shared_ptr<Camera> GetCamera(size_t index) const;
 bool SetActiveCamera(size_t index);

 void UpdateFrameTime();
 void ResetAllKeyStatus();

private:
 std::map<size_t, std::shared_ptr<Camera>> Cameras;

 size_t ActiveViewIndex;
};

}

#endif // VIEWENGINE_H

#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
namespace cutum {
// Conservative box proxy contains a sphere containing all four near-plane corners.
// Analytic slab sweep prevents tunnelling independently of frame rate.
inline float SweepCameraAabb(glm::vec3 start, glm::vec3 end, glm::vec3 low, glm::vec3 high, float radius) {
 low-=glm::vec3(radius);high+=glm::vec3(radius);
 const auto delta=end-start;float enter=0.f,exit=1.f;
 for(int i=0;i<3;++i) {
  if(std::abs(delta[i])<1e-7f){if(start[i]<=low[i] || start[i]>=high[i])return 1.f;continue;}
  float a=(low[i]-start[i])/delta[i],b=(high[i]-start[i])/delta[i];if(a>b)std::swap(a,b);
  enter=std::max(enter,a);exit=std::min(exit,b);if(enter>exit)return 1.f;
 }
 return exit<0.f || enter>1.f?1.f:std::max(0.f,enter);
}
inline float CameraVolumeRadius(float nearPlane,float fovDegrees,float aspect) {
 const float halfHeight=nearPlane*std::tan(glm::radians(std::clamp(fovDegrees,1.f,170.f))*.5f);
 return std::max(.18f,std::sqrt(nearPlane*nearPlane+halfHeight*halfHeight*(1.f+aspect*aspect)))+.02f;
}
}

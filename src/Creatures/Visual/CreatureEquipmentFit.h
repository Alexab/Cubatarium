#pragma once
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstddef>
#include <utility>

namespace cutum
{
// Static fitting regions in the attachment's rest-local coordinates. Motion
// remains the unmodified bone transform. Shoes extend upward from the sole.
inline std::pair<glm::vec3,glm::vec3> CreatureWearRegion(
    size_t slot,const glm::vec3 &low,const glm::vec3 &high)
{
  const glm::vec3 extent=glm::max(high-low,glm::vec3(.04f));
  glm::vec3 center=(low+high)*.5f,size=extent;
  switch(slot)
  {
  case 0: center.y=high.y-extent.y*.12f;size+=glm::vec3(.10f,0.f,.10f);size.y=extent.y*.65f;break;
  case 1: size+=glm::vec3(.06f,.02f,.06f);break;
  case 2: size.y=extent.y*.65f;center.y=high.y-size.y*.5f;size+=glm::vec3(.06f,.02f,.06f);break;
  case 3: size.y=extent.y*.25f;center.y=low.y+size.y*.5f;size+=glm::vec3(.06f,.02f,.06f);break;
  case 4: size.y=extent.y*.70f;center.y=high.y-size.y*.5f;size+=glm::vec3(.06f,0.f,.06f);break;
  // Keep the moving sole within 2 mm of the body's foot envelope. A large
  // forward toe pad would enter the floor when the whole rigid leg swings.
  case 5: size.y=extent.y*.30f;center.y=low.y+size.y*.5f;size+=glm::vec3(.004f,0.f,.004f);break;
  }
  return {center-size*.5f,center+size*.5f};
}

inline glm::mat4 FitCreatureWearToRegion(const glm::vec3 &itemLow,
    const glm::vec3 &itemHigh,const std::pair<glm::vec3,glm::vec3> &region)
{
  const glm::vec3 scale=(region.second-region.first)/glm::max(itemHigh-itemLow,glm::vec3(.0001f));
  return glm::translate(glm::mat4(1.f),(region.first+region.second)*.5f)*
      glm::scale(glm::mat4(1.f),scale)*glm::translate(glm::mat4(1.f),-(itemLow+itemHigh)*.5f);
}

inline glm::vec3 CreaturePalmGripPosition(const glm::vec3 &low,const glm::vec3 &high)
{
  return glm::vec3((low.x+high.x)*.5f,low.y+(high.y-low.y)*.15f,low.z-.04f);
}
} // namespace cutum

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
  // Current four-slot inventory uses 2=legs,3=feet. Keep the historical
  // region aliases for callers with six-slot authoring metadata.
  case 2:
  case 4: size.y=extent.y*.70f;center.y=high.y-size.y*.5f;size+=glm::vec3(.06f,0.f,.06f);break;
  case 3:
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

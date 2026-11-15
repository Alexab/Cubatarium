#ifndef ITEM_PRESENTATION_MATH_H
#define ITEM_PRESENTATION_MATH_H

#include "Items/ItemRenderAsset.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace cutum
{
inline glm::mat4 ItemEulerMatrix(glm::vec3 degrees)
{
  const auto a = glm::radians(degrees);
  return glm::rotate(glm::mat4(1), a.x, glm::vec3(1, 0, 0)) *
         glm::rotate(glm::mat4(1), a.y, glm::vec3(0, 1, 0)) *
         glm::rotate(glm::mat4(1), a.z, glm::vec3(0, 0, 1));
}
inline glm::mat4 ItemAimRotation(const glm::mat4 &frame)
{
  glm::mat4 result(1);
  for (int column = 0; column < 3; ++column)
    result[column] = glm::vec4(glm::normalize(glm::vec3(frame[column])), 0);
  return result;
}
inline glm::quat ItemTurnBetween(glm::vec3 from, glm::vec3 to)
{
  from = glm::normalize(from);
  to = glm::normalize(to);
  const float d = std::clamp(glm::dot(from, to), -1.f, 1.f);
  if (d < -.99999f)
  {
    const auto other =
        std::abs(from.x) < .9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    return glm::angleAxis(3.14159265f, glm::normalize(glm::cross(from, other)));
  }
  return glm::normalize(glm::quat(1.f + d, glm::cross(from, to)));
}
inline glm::mat4 ItemBowModelMatrix(const ItemRenderAsset &asset,
                                    glm::vec3 palm, const glm::mat4 &aim,
                                    float scale)
{
  return glm::translate(glm::mat4(1), palm) * ItemAimRotation(aim) *
         ItemEulerMatrix(asset.Grip.TpAimEulerDeg) *
         glm::scale(glm::mat4(1), glm::vec3(scale * asset.Grip.TpScale *
                                            glm::length(glm::vec3(aim[0])))) *
         glm::translate(glm::mat4(1), -asset.Grip.RightGrip);
}
inline bool ItemHasSupportGrip(const ItemRenderAsset &asset)
{
  return !asset.Grip.SupportGripNode.empty() &&
         asset.Grip.SupportGripIndex < asset.Nodes.size();
}
inline glm::mat4 ItemWearSectionMatrix(const ItemWearSection &section,
                                       glm::vec3 low, glm::vec3 high)
{
  return glm::translate(glm::mat4(1), (low + high) * .5f) *
         ItemEulerMatrix(section.EulerDeg) *
         glm::scale(glm::mat4(1),
                    (high - low) / glm::max(section.High - section.Low,
                                            glm::vec3(.0001f))) *
         glm::translate(glm::mat4(1), -(section.Low + section.High) * .5f);
}
inline glm::mat4 ItemWieldLocalMatrix(const ItemRenderAsset &asset,
                                      glm::vec3 palm, bool left, float scale)
{
  auto angle = asset.Grip.TpEulerDeg;
  if (left)
  {
    angle.y = -angle.y;
    angle.z = -angle.z;
  }
  const bool anchored = left ? asset.Grip.HasLeftGrip : asset.Grip.HasRightGrip;
  const auto anchor = anchored
                          ? (left ? asset.Grip.LeftGrip : asset.Grip.RightGrip)
                          : (asset.Low + asset.High) * .5f;
  return glm::translate(glm::mat4(1), palm) * ItemEulerMatrix(angle) *
         glm::scale(glm::mat4(1), glm::vec3(scale * asset.Grip.TpScale)) *
         glm::translate(glm::mat4(1), -anchor);
}
/// Authored geometry is already in physical units. Only position it inside the
/// collision proxy; never expand its longest axis to the proxy's longest axis.
inline glm::mat4 ItemLooseLocalMatrix(const ItemRenderAsset &asset,
                                      glm::vec3 physicalSize, bool onSupport)
{
  const auto rotation =
      onSupport ? ItemEulerMatrix(asset.Grip.GroundEulerDeg) : glm::mat4(1);
  glm::vec3 low(1e9f), high(-1e9f);
  for (int x : {0, 1})
    for (int y : {0, 1})
      for (int z : {0, 1})
      {
        const auto p =
            glm::vec3(rotation * glm::vec4(x ? asset.High.x : asset.Low.x,
                                           y ? asset.High.y : asset.Low.y,
                                           z ? asset.High.z : asset.Low.z, 1));
        low = glm::min(low, p);
        high = glm::max(high, p);
      }
  auto center = (low + high) * .5f;
  if (onSupport)
    center.y = low.y + physicalSize.y * .5f;
  return glm::translate(glm::mat4(1), -center) * rotation;
}
/// Creature/model yaw uses local +Z forward (camera yaw instead uses +X).
inline glm::vec3 ItemCarrierRight(float modelYawDegrees)
{
  const float yaw = glm::radians(modelYawDegrees);
  return {-std::cos(yaw), 0, std::sin(yaw)};
}
/// Carried models follow the carrier's horizontal frame. Contact coordinates
/// share this exact transform; physical body dimensions and position stay owned
/// by the simulation. Released/flying models retain their existing transforms.
inline glm::mat4 ItemCarryLocalMatrix(const ItemRenderAsset &asset,
                                      glm::vec3 right)
{
  right.y = 0;
  right =
      glm::length(right) > .0001f ? glm::normalize(right) : glm::vec3(1, 0, 0);
  glm::mat4 frame(1);
  frame[0] = glm::vec4(right, 0);
  frame[2] = glm::vec4(glm::cross(right, glm::vec3(0, 1, 0)), 0);
  const auto rotation = frame * ItemEulerMatrix(asset.Grip.CarryEulerDeg);
  const auto center =
      glm::vec3(rotation * glm::vec4((asset.Low + asset.High) * .5f, 1));
  return glm::translate(glm::mat4(1), -center) * rotation;
}
/// Terrain/creature sky-block curve, converted to linear intensity for items.
inline float ItemWorldLightIntensity(glm::vec2 light, float day, float night,
                                     float skyScale, float minAmbient)
{
  const float daySky = std::clamp(day * skyScale, 0.f, 1.f);
  const float nightSky = std::clamp(night * (1 - daySky), 0.f, 1.f);
  const float ambient = (minAmbient * (.18f + .32f * nightSky)) * (1 - daySky) +
                        (minAmbient * (.42f + .38f * daySky)) * daySky;
  const float skyMix =
      std::clamp(light.x * daySky + light.x * .18f * (1 - daySky), 0.f, 1.f);
  const float sky = ambient + (1 - ambient) * skyMix;
  const float floor = std::clamp(minAmbient * .04f, .005f, .1f);
  const float blockStrength = .9f * (1 - daySky) + .35f * daySky;
  const float block =
      floor + (blockStrength - floor) * std::clamp(light.y, 0.f, 1.f);
  return std::pow(std::clamp(std::max(sky, block), 0.f, 1.f), 2.2f);
}
} // namespace cutum
#endif

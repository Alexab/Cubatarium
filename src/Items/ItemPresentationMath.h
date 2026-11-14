#ifndef ITEM_PRESENTATION_MATH_H
#define ITEM_PRESENTATION_MATH_H

#include "Items/ItemRenderAsset.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace cutum
{
inline glm::mat4 ItemEulerMatrix(glm::vec3 degrees)
{
  const auto a = glm::radians(degrees);
  return glm::rotate(glm::mat4(1), a.x, glm::vec3(1, 0, 0)) *
         glm::rotate(glm::mat4(1), a.y, glm::vec3(0, 1, 0)) *
         glm::rotate(glm::mat4(1), a.z, glm::vec3(0, 0, 1));
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
         glm::scale(glm::mat4(1), glm::vec3(scale)) *
         glm::translate(glm::mat4(1), -anchor);
}
/// Authored geometry is already in physical units. Only position it inside the
/// collision proxy; never expand its longest axis to the proxy's longest axis.
inline glm::mat4 ItemLooseLocalMatrix(const ItemRenderAsset &asset,
                                      glm::vec3 physicalSize, bool onSupport)
{
  auto center = (asset.Low + asset.High) * .5f;
  if (onSupport)
    center.y = asset.Low.y + physicalSize.y * .5f;
  return glm::translate(glm::mat4(1), -center);
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

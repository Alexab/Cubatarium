#ifndef CREATUREFOOTPRINT_H

#define CREATUREFOOTPRINT_H



#include <algorithm>

#include <cmath>

#include <cstdint>

#include <glm/glm.hpp>



namespace cutum

{



using CreatureId = uint64_t;



class UWorld;



struct FootprintSample

{

  bool centerSolid{false};

  int solidSamples{0};

  int totalSamples{9};

  bool hasGroundSupport{false};

};



inline float CreatureFootprintHalfWidth(const glm::vec3 &sizeBlocks)

{

  return std::max(sizeBlocks.x, sizeBlocks.z) * 0.5f;

}



inline int MinSolidSamplesForFootprint(float footprintWidth)

{

  constexpr float kReferenceFootprintWidth = 0.6f;

  constexpr float kSmallFootprintWidth = 0.55f;

  if (footprintWidth <= kSmallFootprintWidth)

  {

    return 1;

  }

  const float ratio = footprintWidth / kReferenceFootprintWidth;

  const int scaled = static_cast<int>(std::ceil(4.0f * ratio * ratio));

  return std::clamp(scaled, 1, 9);

}



inline bool EvaluateGroundSupport(bool centerSolid, int solidSamples,

                                  float footprintWidth)

{

  if (!centerSolid)

  {

    return false;

  }

  return solidSamples >= MinSolidSamplesForFootprint(footprintWidth);

}



inline bool CreatureHasGroundSupport(const FootprintSample &sample)

{

  return sample.hasGroundSupport;

}



FootprintSample SampleCreatureFootprint(const UWorld &world,

                                        const glm::vec3 &bodyOrigin,

                                        const glm::vec3 &sizeBlocks);



void DepenetrateCreatureFromGround(const UWorld &world, glm::vec3 &bodyOrigin,

                                   const glm::vec3 &sizeBlocks,

                                   CreatureId skipCreatureId = 0);

bool IsOnRaisedFooting(const UWorld &world, const glm::vec3 &bodyOrigin);

bool IsInDepression(const UWorld &world, const glm::vec3 &bodyOrigin);

glm::vec3 SnapBodyToColumnGround(const UWorld &world, glm::vec3 bodyOrigin,

                                 const glm::vec3 &sizeBlocks,

                                 CreatureId skipCreatureId,

                                 float maxDropBlocks = 1.05f);

bool TryCreatureLedgeDrop(const UWorld &world, CreatureId skipCreatureId,

                          glm::vec3 &bodyOrigin, const glm::vec3 &horizDelta,

                          const glm::vec3 &sizeBlocks);



} // namespace cutum



#endif


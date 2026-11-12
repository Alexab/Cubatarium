#pragma once
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cutum
{
inline bool UsesGltfTransitionGroundClearance(const CreatureLocomotionFacts &facts,
    const CreatureDefinition &definition)
{
  if(facts.mode==CreatureMovementMode::Flying || facts.state==LocomotionState::Jump ||
      facts.state==LocomotionState::Fall || facts.state==LocomotionState::Fly ||
      facts.state==LocomotionState::Glide || facts.state==LocomotionState::Hover)return false;
  if(facts.inFluid)
    return facts.onFluidBottom && (definition.habitat==CreatureHabitat::Aquatic ||
        definition.habitat==CreatureHabitat::Amphibious);
  return facts.onGround && (definition.habitat==CreatureHabitat::Terrestrial ||
      definition.habitat==CreatureHabitat::Amphibious);
}

// Local-TRS crossfades can put an intermediate foot below both source clips'
// corrected floors. Evaluate the actual shared palette, never change its joints.
// Only the Y row is needed; the caller restricts this vertex pass to crossfades.
inline float ComputeGltfTransitionGroundClearance(const CreatureGltfMeshAsset &asset,
    const std::vector<glm::mat4> &palette,const glm::mat4 &rootAnimation,
    float modelScale,float modelFeetOffsetY)
{
  float lowest=std::numeric_limits<float>::infinity();
  for(const auto &primitive:asset.primitives)
  {
    if(primitive.skinned && palette.empty())continue;
    const auto &vertices=primitive.mesh.interleavedPosUv;
    for(size_t v=0;v+4<vertices.size();v+=5)
    {
      const auto rowY=[&](const glm::mat4 &m){return m[0].y*vertices[v]+
          m[1].y*vertices[v+1]+m[2].y*vertices[v+2]+m[3].y;};
      float y=rowY(rootAnimation);
      const size_t offset=v/5*4;
      if(primitive.skinned)
      {
        if(offset+3>=primitive.jointIndices.size() || offset+3>=primitive.jointWeights.size())continue;
        y=0.f;
        for(size_t influence=0;influence<4;++influence)
        {
          const auto joint=primitive.jointIndices[offset+influence];
          const float weight=primitive.jointWeights[offset+influence];
          if(weight>0.f && joint<palette.size())y+=rowY(palette[joint])*weight;
        }
      }
      lowest=std::min(lowest,modelFeetOffsetY+modelScale*y);
    }
  }
  // A tiny positive margin covers float accumulation, without pulling down a
  // deliberately hovering mesh or altering the physical body origin.
  return std::isfinite(lowest) && lowest<0.f ? -lowest+.00001f : 0.f;
}
}

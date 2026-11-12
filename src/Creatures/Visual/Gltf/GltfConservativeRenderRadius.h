#pragma once
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cutum
{
// A load-time bound, not a sampled animation AABB. For each node, bound
// ||world translation|| and the linear operator norm over every clip and
// local-TRS crossfade. A joint's inverse-bind vertex then lies in that ball.
// Nonnegative normalized skin weights preserve the ball by convexity.
inline float ComputeGltfConservativeRenderRadius(const CreatureGltfMeshAsset &asset)
{
  constexpr double infinity=std::numeric_limits<double>::infinity();
  struct Bound { double translation{0},scale{1}; };
  std::vector<Bound> local(asset.nodes.size()),global(asset.nodes.size());
  std::vector<double> rotationNorm(asset.nodes.size(),1.);
  const auto length=[&](const glm::vec3 &v){const double n=glm::length(glm::dvec3(v));return std::isfinite(n)?n:infinity;};
  const auto scale=[&](const glm::vec3 &v){const auto a=glm::abs(glm::dvec3(v));
    return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z)?std::max({a.x,a.y,a.z}):infinity;};
  const auto norm=[&](const glm::quat &q){const double n=double(glm::dot(q,q));return std::isfinite(n)?std::max(1.,n):infinity;};
  for(size_t i=0;i<asset.nodes.size();++i)
  {
    local[i]={length(asset.nodes[i].translation),scale(asset.nodes[i].scale)};
    const auto &q=asset.nodes[i].rotation;
    rotationNorm[i]=norm(q);
  }
  Bound root;
  double rootRotationNorm=1.;
  bool rootSafe=true;
  for(const auto &animation:asset.animations)for(const auto &channel:animation.channels)
  {
    if(channel.keyTimes.empty())continue;
    const int node=channel.nodeIndex;
    if(node>=0 && size_t(node)<local.size())
    {
      auto &b=local[node];
      if(channel.path=="translation")for(const auto &v:channel.keyVec3)b.translation=std::max(b.translation,length(v));
      if(channel.path=="scale")for(const auto &v:channel.keyVec3)b.scale=std::max(b.scale,scale(v));
      if(channel.path=="rotation")for(const auto &q:channel.keyQuat)rotationNorm[node]=std::max(rotationNorm[node],norm(q));
    }
    // The legacy root-only player consumes all channels (not the node tree),
    // and does not clamp negative alpha before the first key. Leave unusual
    // delayed-key assets visible rather than invent an extrapolation bound.
    if(channel.keyTimes.front()>0.f)rootSafe=false;
    if(channel.path=="translation")for(const auto &v:channel.keyVec3)root.translation=std::max(root.translation,length(v));
    if(channel.path=="scale")for(const auto &v:channel.keyVec3)root.scale=std::max(root.scale,scale(v));
    if(channel.path=="rotation")for(const auto &q:channel.keyQuat)rootRotationNorm=std::max(rootRotationNorm,norm(q));
  }
  // Shortest-arc slerp between quaternions of norm <=M (M>=1) also has
  // norm <=M. quat-to-matrix has operator norm <=max(1,2*||q||^2-1).
  for(size_t i=0;i<local.size();++i)local[i].scale*=std::max(1.,2.*rotationNorm[i]-1.)*1.000001;
  root.scale*=std::max(1.,2.*rootRotationNorm-1.)*1.000001;
  std::vector<unsigned char> state(local.size(),0);
  const auto resolve=[&](int node,const auto &self)->Bound
  {
    if(node<0 || size_t(node)>=local.size())return {infinity,infinity};
    if(state[node]==2)return global[node];
    if(state[node]==1)return {infinity,infinity};
    state[node]=1;
    const int parent=asset.nodes[node].parent;
    const Bound p=parent<0?Bound{}:self(parent,self);
    global[node]={p.translation+p.scale*local[node].translation,p.scale*local[node].scale};
    state[node]=2;
    return global[node];
  };
  double radius=0.;
  for(const auto &primitive:asset.primitives)
  {
    const auto &vertices=primitive.mesh.interleavedPosUv;
    for(size_t v=0;v+4<vertices.size();v+=5)
    {
      const glm::dvec4 point(vertices[v],vertices[v+1],vertices[v+2],1.);
      if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z))return float(infinity);
      double vertexRadius=0.;
      if(primitive.skinned && asset.hasSkin)
      {
        const size_t offset=v/5*4;
        if(offset+3>=primitive.jointWeights.size() || offset+3>=primitive.jointIndices.size())return float(infinity);
        double weightSum=0.;
        for(size_t influence=0;influence<4;++influence)
        {
          const float weight=primitive.jointWeights[offset+influence];
          if(!std::isfinite(weight) || weight<0.f)return float(infinity);
          weightSum+=weight;
          if(weight==0.f)continue;
          const size_t joint=primitive.jointIndices[offset+influence];
          if(joint>=asset.skin.jointNodes.size())return float(infinity);
          const auto b=resolve(asset.skin.jointNodes[joint],resolve);
          glm::dvec4 bind=point;
          if(joint<asset.skin.inverseBindMatrices.size())
          {
            const glm::dmat4 ibm(asset.skin.inverseBindMatrices[joint]);
            for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(!std::isfinite(ibm[c][r]))return float(infinity);
            if(ibm[0].w!=0. || ibm[1].w!=0. || ibm[2].w!=0. || ibm[3].w!=1.)return float(infinity);
            bind=ibm*point;
          }
          const double influenceRadius=b.translation+b.scale*glm::length(glm::dvec3(bind));
          if(!std::isfinite(influenceRadius))return float(infinity);
          vertexRadius=std::max(vertexRadius,influenceRadius);
        }
        // Loader/export contracts use normalized weights. Fail open for a
        // malformed palette instead of letting its model be falsely culled.
        if(std::abs(weightSum-1.)>.000001)return float(infinity);
        vertexRadius*=std::max(1.,weightSum);
      }
      else
      {
        const Bound b=asset.hasSkin?Bound{}:root;
        if(!asset.hasSkin && !rootSafe)return float(infinity);
        vertexRadius=b.translation+b.scale*glm::length(glm::dvec3(point));
      }
      if(!std::isfinite(vertexRadius))return float(infinity);
      radius=std::max(radius,vertexRadius);
    }
  }
  return std::nextafter(float(radius*1.00001+.0001),std::numeric_limits<float>::infinity());
}
} // namespace cutum

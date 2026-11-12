#pragma once
#include "Creatures/Visual/CreatureDrawRequest.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace cutum
{
inline bool CreatureMeshLocalClipBounds(const BoneSkeletonCubeMeshCpu &mesh,
    glm::vec3 &low,glm::vec3 &high)
{
  if(!mesh.GpuHash || !mesh.ClipBoundsReady || mesh.ClipBoundsGpuHash!=mesh.GpuHash ||
      mesh.ClipBoundsFloatCount!=mesh.interleavedPosUv.size())
  {
    mesh.ClipBoundsReady=true;mesh.ClipBoundsFinite=false;
    mesh.ClipBoundsGpuHash=mesh.GpuHash;mesh.ClipBoundsFloatCount=mesh.interleavedPosUv.size();
    glm::vec3 a(std::numeric_limits<float>::max()),b(-std::numeric_limits<float>::max());
    const auto &vertices=mesh.interleavedPosUv;
    if(vertices.empty() || vertices.size()%5!=0)return false;
    for(size_t i=0;i<vertices.size();i+=5)
    {
      const glm::vec3 point(vertices[i],vertices[i+1],vertices[i+2]);
      if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z))return false;
      a=glm::min(a,point);b=glm::max(b,point);
    }
    mesh.ClipBoundsLow=a;mesh.ClipBoundsHigh=b;mesh.ClipBoundsFinite=true;
  }
  low=mesh.ClipBoundsLow;high=mesh.ClipBoundsHigh;
  return mesh.ClipBoundsFinite;
}

// Homogeneous clip-plane maxima avoid inverse VP/camera precision loss.
// Transforming a local AABB is conservative for every vertex and triangle of
// its static mesh. Include every request of one actor, including attachments.
class CreatureDrawClipBounds
{
public:
  void Add(const CreatureDrawRequest &request)
  {
    if(Unbounded)return;
    glm::vec3 low(-.5f),high(.5f);
    switch(request.Kind)
    {
    case CreatureDrawKind::SkeletalMesh:
      if(!request.SkeletalMesh || !CreatureMeshLocalClipBounds(*request.SkeletalMesh,low,high))
      {Unbounded=true;return;}
      break;
    case CreatureDrawKind::SkinnedMesh:
      // Animated glTF uses the separate all-clip hierarchy envelope. Never
      // substitute its bind AABB if an unknown skinned attachment appears.
      Unbounded=true;return;
    case CreatureDrawKind::Billboard:low.z=high.z=0.f;break;
    case CreatureDrawKind::TexturedPart:
    case CreatureDrawKind::WireframeBox:break;
    default:Unbounded=true;return;
    }
    for(int x:{0,1})for(int y:{0,1})for(int z:{0,1})
    {
      const glm::vec4 p=request.Mvp*glm::vec4(x?high.x:low.x,y?high.y:low.y,z?high.z:low.z,1.f);
      if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||!std::isfinite(p.w))
      {Unbounded=true;return;}
      const float tolerance=.00002f*std::max({1.f,std::abs(p.x),std::abs(p.y),std::abs(p.z),std::abs(p.w)});
      const std::array<float,6> distances={p.w+p.x,p.w-p.x,p.w+p.y,p.w-p.y,p.w+p.z,p.w-p.z};
      for(size_t plane=0;plane<6;++plane)Maxima[plane]=std::max(Maxima[plane],distances[plane]+tolerance);
      Any=true;
    }
  }
  bool Intersects()const
  {
    if(Unbounded || !Any)return true;
    return std::none_of(Maxima.begin(),Maxima.end(),[](float value){return value<0.f;});
  }
private:
  std::array<float,6> Maxima={-INFINITY,-INFINITY,-INFINITY,-INFINITY,-INFINITY,-INFINITY};
  bool Any{false},Unbounded{false};
};
} // namespace cutum

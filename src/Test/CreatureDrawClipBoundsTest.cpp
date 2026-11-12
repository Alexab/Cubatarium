#include "Creatures/Visual/CreatureDrawQueue.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cstdlib>
#include <iostream>

using namespace cutum;
namespace
{
void Expect(bool value,const char *why){if(!value){std::cerr<<why<<'\n';std::exit(1);}}
CreatureDrawRequest Box(const glm::mat4 &matrix)
{CreatureDrawRequest r;r.Kind=CreatureDrawKind::TexturedPart;r.Mvp=matrix;return r;}
}
int main()
{
  const glm::mat4 identity(1.f);
  for(int axis=0;axis<3;++axis)for(float side:{-1.f,1.f})
  {
    glm::vec3 outside(0);outside[axis]=2.f*side;
    CreatureDrawQueue queue;queue.Push(Box(glm::translate(identity,outside)));
    Expect(!queue.RangeIntersectsClip(0),"all six clip planes reject a fully exterior body");
    glm::vec3 limb(0);limb[axis]=.75f*side;
    queue.Push(Box(glm::translate(identity,limb)));
    Expect(queue.RangeIntersectsClip(0),"visible limb/equipment keeps an exterior actor alive");
    queue.DiscardSince(1);
    Expect(queue.Size()==1 && !queue.RangeIntersectsClip(0),"tail rollback preserves previous actor requests");
  }
  CreatureDrawQueue queue;queue.Push(Box(identity));const size_t next=queue.Size();
  queue.Push(Box(glm::translate(identity,glm::vec3(5,0,0))));
  Expect(!queue.RangeIntersectsClip(next),"previous visible actors cannot hide rejection of the current range");
  Expect(queue.RangeIntersectsClip(0),"combined geometry stays conservative");
  queue.DiscardSince(next);Expect(queue.Size()==1,"discard removes only the culled actor");
  queue.DiscardSince(100);Expect(queue.Size()==1,"invalid discard index cannot append phantom draws");
  CreatureDrawQueue separated;
  separated.Push(Box(glm::translate(identity,glm::vec3(-3,0,0))));
  separated.Push(Box(glm::translate(identity,glm::vec3(3,0,0))));
  Expect(!separated.RangeIntersectsClip(0),"a union spanning the camera does not keep two entirely exterior parts alive");
  BoneSkeletonCubeMeshCpu mesh;mesh.interleavedPosUv={-4,0,0,0,0, 4,1,0,0,0};mesh.GpuHash=17;
  CreatureDrawRequest part;part.Kind=CreatureDrawKind::SkeletalMesh;part.SkeletalMesh=&mesh;
  part.Mvp=glm::translate(identity,glm::vec3(3,0,0));
  CreatureDrawClipBounds bounds;bounds.Add(part);
  Expect(bounds.Intersects(),"mesh vertices outside a unit box remain part of the envelope");
  glm::vec3 low,high;Expect(CreatureMeshLocalClipBounds(mesh,low,high)&&low.x==-4.f,"cache computed actual static vertices");
  mesh.interleavedPosUv[0]=-8;mesh.GpuHash=18;
  Expect(CreatureMeshLocalClipBounds(mesh,low,high)&&low.x==-8.f,"GPU content change invalidates local bounds");
  mesh.GpuHash=0;mesh.interleavedPosUv[0]=-9;
  CreatureMeshLocalClipBounds(mesh,low,high);mesh.interleavedPosUv[0]=-10;
  Expect(CreatureMeshLocalClipBounds(mesh,low,high)&&low.x==-10.f,"unkeyed custom meshes never reuse a stale vertex bound");
  CreatureDrawClipBounds skin;part.Kind=CreatureDrawKind::SkinnedMesh;skin.Add(part);
  Expect(skin.Intersects(),"an unsupported skin never substitutes bind bounds for an animated attachment");
  mesh.GpuHash=19;mesh.interleavedPosUv[0]=std::numeric_limits<float>::quiet_NaN();
  part.Kind=CreatureDrawKind::SkeletalMesh;CreatureDrawClipBounds invalid;invalid.Add(part);
  Expect(invalid.Intersects(),"nonfinite data fails open");
  const auto vp=glm::perspective(glm::radians(60.f),1.f,.1f,100.f);
  CreatureDrawClipBounds behind;behind.Add(Box(vp*glm::translate(identity,glm::vec3(0,0,5))));
  Expect(!behind.Intersects(),"negative homogeneous W behind a perspective camera is rejected");
  CreatureDrawClipBounds near;near.Add(Box(vp*glm::translate(identity,glm::vec3(0,0,-.2f))));
  Expect(near.Intersects(),"an actor crossing the near plane stays visible");
  // Camera-facing sprite has no depth: its transformed quad can remain
  // visible after its narrow physical Z box leaves a side plane.
  CreatureDrawRequest sprite;sprite.Kind=CreatureDrawKind::Billboard;
  sprite.Mvp=glm::translate(identity,glm::vec3(1.2f,0,0))*glm::scale(identity,glm::vec3(1,2,1));
  CreatureDrawClipBounds billboard;billboard.Add(sprite);Expect(billboard.Intersects(),"actual sprite quad included");
  std::cout<<"creature_draw_clip_bounds_test: passed\n";
}

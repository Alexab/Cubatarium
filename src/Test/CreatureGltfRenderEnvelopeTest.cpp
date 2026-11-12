#include "Creatures/Visual/Gltf/GltfConservativeRenderRadius.h"
#include "Creatures/Visual/Gltf/CreatureGltfBonePalette.h"
#include "Creatures/Visual/Gltf/CreatureGltfAnimPlayer.h"
#include "Creatures/Visual/CreatureVisibility.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cstdlib>
#include <iostream>

using namespace cutum;
namespace
{
void Expect(bool value,const char *why){if(!value){std::cerr<<why<<'\n';std::exit(1);}}
}
int main()
{
  CreatureGltfMeshAsset asset;asset.hasSkin=true;asset.nodes.resize(3);
  asset.nodes[0].translation={.2f,.3f,0.f};
  asset.nodes[1].parent=0;asset.nodes[1].translation={0.f,2.f,0.f};
  asset.nodes[2].parent=1;asset.nodes[2].translation={1.f,0.f,0.f};
  asset.skin.jointNodes={1,2};
  asset.skin.inverseBindMatrices={glm::translate(glm::mat4(1),glm::vec3(0,-2,0)),
    glm::translate(glm::mat4(1),glm::vec3(-1,-2,0))};
  GltfPrimitiveCpu prim;prim.skinned=true;
  prim.mesh.interleavedPosUv={0,0,0,0,0, 2,3,-1,0,0};
  prim.jointIndices={0,1,0,0, 0,1,0,0};prim.jointWeights={.3f,.7f,0,0, .6f,.4f,0,0};
  asset.primitives={prim};
  for(int clip=0;clip<2;++clip)
  {
    GltfAnimationCpu animation;
    GltfAnimationChannelCpu c;c.nodeIndex=0;c.keyTimes={0,1};c.path="translation";
    c.keyVec3={{-1,0,0},{float(clip+2),1,0}};animation.channels.push_back(c);
    c.nodeIndex=1;c.path="scale";c.keyVec3={{1,.5f,1},{-2,1.5f,1}};animation.channels.push_back(c);
    c.nodeIndex=2;c.path="rotation";c.keyVec3.clear();
    c.keyQuat={glm::angleAxis(glm::radians(-110.f),glm::vec3(0,0,1)),
      glm::angleAxis(glm::radians(clip?160.f:90.f),glm::vec3(1,0,0))};
    animation.channels.push_back(c);asset.animations.push_back(animation);
  }
  const float radius=ComputeGltfConservativeRenderRadius(asset);
  Expect(std::isfinite(radius),"ordinary normalized hierarchical skin receives a finite envelope");
  size_t poses=0;
  for(int a=0;a<=20;++a)for(int b=0;b<=20;++b)for(int fade=0;fade<=10;++fade)
  {
    const auto palette=ComputeGltfSkinMatricesBlended(asset,&asset.animations[0],a/20.f,
      &asset.animations[1],b/20.f,fade/10.f);
    for(size_t v=0;v<2;++v)
    {
      const auto &p=prim.mesh.interleavedPosUv;
      const glm::vec4 point(p[v*5],p[v*5+1],p[v*5+2],1.f);
      const auto posed=palette[0]*point*prim.jointWeights[v*4]+palette[1]*point*prim.jointWeights[v*4+1];
      Expect(glm::length(glm::vec3(posed))<=radius,"envelope covers all sampled hierarchy/scale/rotation/local-TRS crossfades");
    }
    ++poses;
  }
  auto malformed=asset;malformed.nodes[0].parent=2;
  Expect(!std::isfinite(ComputeGltfConservativeRenderRadius(malformed)),"cycles fail open");
  malformed=asset;malformed.primitives[0].jointWeights[0]=-.1f;
  Expect(!std::isfinite(ComputeGltfConservativeRenderRadius(malformed)),"negative weights do not invent a convex bound");
  malformed=asset;malformed.primitives[0].jointIndices[0]=9;
  Expect(!std::isfinite(ComputeGltfConservativeRenderRadius(malformed)),"invalid joint fails open");
  malformed=asset;malformed.nodes[0].translation.x=std::numeric_limits<float>::quiet_NaN();
  Expect(!std::isfinite(ComputeGltfConservativeRenderRadius(malformed)),"nonfinite node fails open");
  CreatureGltfMeshAsset root;prim.skinned=false;root.primitives={prim};root.animations={asset.animations[0]};
  const float rootRadius=ComputeGltfConservativeRenderRadius(root);
  for(int t=0;t<=100;++t)
  {
    const auto matrix=SampleGltfRootTransform(root.animations[0],t/100.f,false);
    Expect(glm::length(glm::vec3(matrix*glm::vec4(2,3,-1,1)))<=rootRadius,"legacy root-only transform is covered");
  }
  root.animations[0].channels[0].keyTimes[0]=.2f;
  Expect(!std::isfinite(ComputeGltfConservativeRenderRadius(root)),"unsupported root extrapolation disables rejection");
  Frustum f;f.planes={glm::vec4(1,0,0,-1),glm::vec4(-1,0,0,4),glm::vec4(0,1,0,10),
    glm::vec4(0,-1,0,10),glm::vec4(0,0,1,10),glm::vec4(0,0,-1,10)};
  Expect(!CreatureAabbIntersectsFrustum(f,glm::vec3(-.5f),glm::vec3(.5f)),"small physical body is outside edge frustum");
  Expect(CreatureAabbIntersectsFrustum(f,glm::vec3(-radius),glm::vec3(radius)),"visible appendage envelope reaches the edge");
  Expect(CreatureAabbIntersectsFrustum(f,glm::vec3(-INFINITY),glm::vec3(INFINITY)),"unknown envelope stays visible");
  std::cout<<"creature_gltf_render_envelope_test: "<<poses<<" blended poses passed\n";
}

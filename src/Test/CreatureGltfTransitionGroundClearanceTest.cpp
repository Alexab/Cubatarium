#include "Creatures/Visual/Gltf/GltfTransitionGroundClearance.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cstdlib>
#include <iostream>

using namespace cutum;
namespace
{
void Expect(bool value,const char *message)
{if(!value){std::cerr<<message<<'\n';std::exit(1);}}
bool Near(float a,float b){return std::abs(a-b)<.00002f;}
}
int main()
{
  CreatureGltfMeshAsset mesh;GltfPrimitiveCpu primitive;
  primitive.mesh.interleavedPosUv={0.f,.1f,0.f,0.f,0.f};
  primitive.skinned=true;primitive.jointIndices={0,1,0,0};primitive.jointWeights={.75f,.25f,0.f,0.f};
  mesh.primitives.push_back(primitive);
  std::vector<glm::mat4> palette={glm::translate(glm::mat4(1.f),glm::vec3(0,-.3f,0)),
      glm::translate(glm::mat4(1.f),glm::vec3(0,-.1f,0))};
  const auto before=palette;
  Expect(Near(ComputeGltfTransitionGroundClearance(mesh,palette,glm::mat4(1.f),1.f,0.f),.15f),
      "all nonzero influences define the actual blended surface floor");
  Expect(palette==before,"render floor correction never rewrites a bone palette");
  Expect(Near(ComputeGltfTransitionGroundClearance(mesh,palette,glm::mat4(1.f),1.f,.3f),0.f),
      "positive intentional clearance is not pulled down");
  Expect(Near(ComputeGltfTransitionGroundClearance(mesh,{},glm::mat4(1.f),1.f,0.f),0.f),
      "a missing skin pose is not treated as a ground envelope");
  mesh.primitives[0].skinned=false;mesh.primitives[0].mesh.interleavedPosUv={0.f,0.f,1.f,0.f,0.f};
  const auto root=glm::rotate(glm::mat4(1.f),glm::radians(90.f),glm::vec3(1,0,0));
  Expect(Near(ComputeGltfTransitionGroundClearance(mesh,{},root,.5f,.2f),.3f),
      "root-only animation, model scale and feet offset use world block units");
  mesh.primitives[0].mesh.interleavedPosUv={0.f,2.f,0.f,0.f,0.f};
  Expect(Near(ComputeGltfTransitionGroundClearance(mesh,{},glm::mat4(1.f),-2.f,1.f),3.f),
      "negative model scale selects the actual world minimum");
  Expect(Near(ComputeGltfTransitionGroundClearance(CreatureGltfMeshAsset{}, {},glm::mat4(1.f),1.f,0.f),0.f),
      "empty geometry produces no guessed lift");
  CreatureDefinition definition;CreatureLocomotionFacts facts;facts.state=LocomotionState::Walk;
  Expect(UsesGltfTransitionGroundClearance(facts,definition),"grounded walking is supported");
  for(auto state:{LocomotionState::Jump,LocomotionState::Fall,LocomotionState::Fly,LocomotionState::Glide,LocomotionState::Hover})
  {facts.state=state;Expect(!UsesGltfTransitionGroundClearance(facts,definition),"air/jump poses are not ground clamped");}
  facts.state=LocomotionState::Action;facts.onGround=false;
  Expect(!UsesGltfTransitionGroundClearance(facts,definition),"airborne action remains free");
  definition.habitat=CreatureHabitat::Aquatic;facts.inFluid=true;facts.onGround=true;
  Expect(!UsesGltfTransitionGroundClearance(facts,definition),"a free swimmer is not clamped to its origin");
  facts.onFluidBottom=true;
  Expect(UsesGltfTransitionGroundClearance(facts,definition),"benthic support uses the actual fluid-bottom fact");
  definition.habitat=CreatureHabitat::Aerial;
  Expect(!UsesGltfTransitionGroundClearance(facts,definition),"aerial habitat remains free even with stale support flags");
  std::cout<<"creature_gltf_transition_ground_clearance_test: passed\n";
}

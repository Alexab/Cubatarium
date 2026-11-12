#include "Creatures/Visual/BoneSkeleton/BoneSkeletonGroundClearance.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cstdlib>
#include <iostream>

using namespace cutum;
namespace {
void Expect(bool value, const char *message) {
  if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
bool Near(float a,float b) {return std::abs(a-b)<.000001f;}
}
int main() {
  CreatureBoneSkeletonMeshAsset mesh;
  mesh.boneMeshes.resize(2);
  BoneSkeletonCubeMeshCpu foot;
  foot.interleavedPosUv={-.1f,0.f,-.1f,0.f,0.f, .1f,.3f,.1f,1.f,1.f};
  mesh.boneMeshes[0].cubes.push_back(foot);
  mesh.boneMeshes[1].cubes.push_back(foot);
  std::vector<glm::mat4> bones={glm::translate(glm::mat4(1.f),glm::vec3(0.f,-.02f,0.f)),
      glm::translate(glm::mat4(1.f),glm::vec3(0.f,-.04f,0.f))};
  const auto original=bones;
  Expect(Near(ComputeBoneGroundClearance(mesh,bones),.04f),"deepest actual bone selects shared lift");
  Expect(bones==original,"clearance never rewrites joint matrices");
  bones[0]=bones[1]=glm::translate(glm::mat4(1.f),glm::vec3(0.f,.2f,0.f));
  Expect(Near(ComputeBoneGroundClearance(mesh,bones),0.f),"intentional positive clearance is retained");
  Expect(Near(ComputeBoneGroundClearance(mesh,{}),0.f),"missing pose is harmless");
  CreatureDefinition def;CreatureLocomotionFacts facts;
  facts.state=LocomotionState::Walk;
  Expect(UsesBoneGroundClearance(facts,def,"quadruped"),"walking quadruped gets clearance");
  Expect(!UsesBoneGroundClearance(facts,def,"rabbit"),"rabbit hop is preserved");
  Expect(!UsesBoneGroundClearance(facts,def,"aquatic"),"aquatic profile is preserved");
  def.habitat=CreatureHabitat::Aquatic;
  Expect(!UsesBoneGroundClearance(facts,def,"quadruped"),"aquatic habitat is not ground clamped");
  def.habitat=CreatureHabitat::Terrestrial;
  for(auto state:{LocomotionState::Fly,LocomotionState::Jump,LocomotionState::Fall,LocomotionState::Swim}) {
    facts.state=state;
    Expect(!UsesBoneGroundClearance(facts,def,"quadruped"),"air/water states are not grounded");
  }
  facts.state=LocomotionState::Walk;facts.onGround=false;
  Expect(!UsesBoneGroundClearance(facts,def,"quadruped"),"physics onGround gates correction");
  BoneGroundClearanceState blend;
  Expect(Near(blend.Update(.04f,true,0.f),.04f),"first grounded pose has exact clearance");
  float previous=.04f;
  for(int frame=0;frame<60;++frame) {
    const float value=blend.Update(0.f,false,1.f/60.f);
    Expect(value>=0.f && value<previous,"takeoff offset decays monotonically");previous=value;
  }
  Expect(previous<.000001f,"takeoff offset returns to unmodified flight pose");
  Expect(Near(blend.Update(.02f,true,1.f/60.f),.02f),"landing never blends through floor");
  std::cout<<"creature_bone_ground_clearance_test: passed\n";
}

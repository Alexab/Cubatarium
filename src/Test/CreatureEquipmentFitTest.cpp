#include "Creatures/Visual/CreatureEquipmentFit.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void Expect(bool value,const char *message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
bool Near(float a,float b){return std::abs(a-b)<1e-5f;}
}
int main()
{
  using namespace cutum;
  const glm::vec3 headLow(-.25f,0.f,-.25f),headHigh(.25f,.5f,.25f);
  const auto helmet=CreatureWearRegion(0,headLow,headHigh);
  Expect(helmet.first.y>.25f && helmet.second.y>.5f,"helmet covers crown rather than neck pivot");
  const auto chest=CreatureWearRegion(1,glm::vec3(-.25f,-.75f,-.125f),glm::vec3(.25f,0.f,.125f));
  Expect(Near((chest.first.y+chest.second.y)*.5f,-.375f),"chest centered on torso surface below shoulder pivot");
  const glm::vec3 legLow(-.125f,-.75f,-.125f),legHigh(.125f,0.f,.125f);
  const auto shoes=CreatureWearRegion(3,legLow,legHigh);
  Expect(Near(shoes.first.y,legLow.y),"shoe sole never extends below body sole");
  Expect(shoes.first.z>=legLow.z-.00201f && shoes.second.z<=legHigh.z+.00201f,
      "shoe toe envelope does not introduce a large rotated-floor penetration");
  const auto greaves=CreatureWearRegion(2,legLow,legHigh);
  Expect(Near(greaves.first.y,shoes.second.y),"greaves and shoes divide limb without joint-centered pileup");
  const glm::vec3 sourceLow(-.8f,-.6f,-.4f),sourceHigh(.8f,.65f,.45f);
  const auto fit=FitCreatureWearToRegion(sourceLow,sourceHigh,chest);
  for(int corner=0;corner<8;++corner)
  {
    const glm::vec3 p(corner&1?sourceHigh.x:sourceLow.x,corner&2?sourceHigh.y:sourceLow.y,corner&4?sourceHigh.z:sourceLow.z);
    const glm::vec3 mapped(fit*glm::vec4(p,1.f));
    Expect(Near(mapped.x,corner&1?chest.second.x:chest.first.x) && Near(mapped.y,corner&2?chest.second.y:chest.first.y) && Near(mapped.z,corner&4?chest.second.z:chest.first.z),"item bounds map to anatomical region");
  }
  const glm::vec3 handLow(-.125f,-.1875f,-.1875f),handHigh(.125f,.5625f,.0625f);
  const auto grip=CreaturePalmGripPosition(handLow,handHigh);
  Expect(grip.y<0.f && grip.z<handLow.z,"grip targets lower palm in front of arm, not forearm center");
  std::cout<<"creature_equipment_fit_test: passed\n";
}

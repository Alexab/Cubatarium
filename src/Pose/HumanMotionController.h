#pragma once
#include "Pose/HumanMotionTypes.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonTypes.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonModelSpace.h"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <array>

namespace cutum {
// Human v2 alone uses quaternions; no global locomotion enum or creature phase changes.
class HumanMotionController {
 static glm::quat FromDown(glm::vec3 direction) {
  const glm::vec3 down(0,-1,0);
  const float dot=glm::dot(down,direction);
  if(dot < -.9999f)return glm::angleAxis(3.14159265f,glm::vec3(1,0,0));
  const auto cross=glm::cross(down,direction);
  return glm::normalize(glm::quat(1.f+dot,cross.x,cross.y,cross.z));
 }
public:
 static glm::vec3 MeasureVelocity(glm::vec3 displacement,float simulatedDt,glm::vec3 previous) {
  if(glm::length(displacement)>std::max(1.f,simulatedDt*30.f))return glm::vec3(0.f);
  if(simulatedDt<=.00001f)return glm::length(displacement)<.0001f?previous:glm::vec3(0.f);
  return displacement/simulatedDt;
 }
 static BoneSkeletonPose Compute(const HumanMotionInput &in, HumanMotionState &s, float dt,bool useMotorPhase=false) {
  const auto &f=in.facts;
  dt=std::clamp(dt,0.f,.1f);
  const bool teleport=s.initialized && glm::distance(in.feet,s.previousFeet)>2.f;
  if(teleport) {const float audio=s.audioPhase;s=HumanMotionState{};s.audioPhase=audio;}
  // Reloads and inspection redraws have no elapsed time. Re-normalizing an
  // unchanged pose here introduced bit drift and broke retained/fresh parity.
  if(s.initialized && dt<=0.f)return s.blended;
  const float travelled=s.initialized && !teleport ? glm::length(glm::vec2(in.feet.x-s.previousFeet.x,in.feet.z-s.previousFeet.z)) : f.horizontalSpeed*dt;
  s.previousFeet=in.feet;
  const bool ground=f.onGround && f.mode!=CreatureMovementMode::Flying && !f.swimming;
  const float speed=f.horizontalSpeed;
  const float stride=(f.state==LocomotionState::Run ? 1.35f : 1.15f)*(1.f-.55f*f.stanceBlend);
  // One measured travel phase drives both contact audio and a possessed
  // player's pose, including switching back from first person. Previews and
  // unpossessed actors retain their independently seeded distance phase.
  s.distancePhase=useMotorPhase?std::fmod(s.audioPhase,6.2831853f):
    std::fmod(s.distancePhase+(ground && speed>.05f?travelled:0.f)*6.2831853f/stride,6.2831853f);
  s.idlePhase=std::fmod(s.idlePhase+dt*2.5f,6.2831853f);
  if(s.initialized && ground && !s.wasGrounded && s.previousVerticalSpeed<-.5f) {
    s.landing=std::clamp(-s.previousVerticalSpeed*.012f,.025f,.11f);++s.landingEvents;
  }
  s.landing*=std::exp(-dt/ .10f);
  s.wasGrounded=ground;s.previousVerticalSpeed=f.verticalSpeed;
  s.mode=f.mode==CreatureMovementMode::Flying ? HumanMotionMode::Flight :
    f.swimming ? (f.headSubmerged?HumanMotionMode::Submerged:HumanMotionMode::Surface) :
    ground?HumanMotionMode::Ground:f.verticalSpeed>.2f?HumanMotionMode::Jump:HumanMotionMode::Fall;
  BoneSkeletonPose target;
  auto set=[&](const std::string &b,glm::vec3 degrees) {
    auto &p=target.bones[b];p.rotation=glm::quat_cast(BoneSkeletonEulerDegToMat(degrees));p.quaternionRotation=true;
  };
  for(const char *name:{"waist","spine","body","head","rightArm","leftArm","rightForearm","leftForearm","rightLeg","leftLeg","rightShin","leftShin","rightFoot","leftFoot"})set(name,glm::vec3(0.f));
  const float idle=std::sin(s.idlePhase),cycle=std::sin(s.distancePhase);
  const float move=std::clamp(speed/.6f,0.f,1.f);
  float drop=0.f;
  if(s.mode==HumanMotionMode::Ground) {
    const float crouch=std::clamp(f.stanceBlend,0.f,1.f);
    const float gaitDrop=.01f+(f.state==LocomotionState::Run?.12f:.09f)*std::abs(cycle);
    drop=.225f*crouch+s.landing+gaitDrop*move*(1.f-crouch);
    set("spine",{-27.f*crouch-(f.state==LocomotionState::Run?9.f:0.f),0,0});
    set("head",{27.f*crouch+(f.state==LocomotionState::Run?9.f:0.f),0,0});
    float arm=(f.state==LocomotionState::Run?38.f:24.f)*move*(1.f-.5f*crouch);
    const auto localVelocity=glm::inverse(glm::quat(glm::vec3(0,glm::radians(in.yawDeg),0)))*f.actualVelocity;
    const float armDirection=localVelocity.z<-.05f?-1.f:1.f;
    set("rightArm",{27.f*crouch-cycle*arm*armDirection,0,-2});set("leftArm",{27.f*crouch+cycle*arm*armDirection,0,2});
    set("rightForearm",{f.state==LocomotionState::Run?60.f:10.f,0,0});
    set("leftForearm",{f.state==LocomotionState::Run?60.f:10.f,0,0});
    set("rightLeg",{cycle*25.f*move-35.f*crouch,0,0});
    set("leftLeg",{-cycle*25.f*move-35.f*crouch,0,0});
    set("rightShin",{std::max(0.f,std::cos(s.distancePhase))*45.f*move+70.f*crouch,0,0});
    set("leftShin",{std::max(0.f,-std::cos(s.distancePhase))*45.f*move+70.f*crouch,0,0});
  } else if(s.mode==HumanMotionMode::Flight) {
    const glm::vec3 local=glm::inverse(glm::quat(glm::vec3(0,glm::radians(in.yawDeg),0)))*f.actualVelocity;
    set("spine",{std::clamp(-local.z*3.f,-20.f,20.f),0,std::clamp(-local.x*2.f,-8.f,8.f)});
    const float up=std::clamp(f.verticalSpeed/3.f,-1.f,1.f);
    set("rightArm",{4.f+idle*5.f,0,-17.f+up*5.f});set("leftArm",{4.f-idle*5.f,0,17.f-up*5.f});
    set("rightForearm",{16.f+idle*4.f,0,0});set("leftForearm",{16.f-idle*4.f,0,0});
    set("rightLeg",{-8.f+idle*4.f,0,-3});set("leftLeg",{-8.f-idle*4.f,0,3});
    set("rightShin",{12.f+std::max(0.f,up)*20.f,0,0});set("leftShin",{12.f+std::max(0.f,up)*20.f,0,0});
  } else if(f.swimming) {
    const float swim=std::clamp(speed/1.5f,0.f,1.f);
    const float room=std::clamp(f.swimClearance,0.f,1.f);
    // Rotate about pelvis; compensate its translation so physical eye height remains the head reference.
    float pitch=-70.f*swim+std::clamp(f.verticalSpeed*8.f,-25.f,25.f);
    pitch=std::clamp(pitch,-80.f,25.f)*f.swimClearance;
    set("waist",{pitch,0,0});
    float angle=glm::radians(pitch);
    target.bones["waist"].offsetBlocks.y=(1.575f-.9f*(.75f+std::cos(angle)*1.f))/.9f;
    const float stroke=s.idlePhase*2.f;
    set("rightArm",{35.f+55.f*swim*room+std::sin(stroke)*110.f*swim*room+idle*9.f*(1.f-swim),0,-25.f*(1.f-swim)*room});
    set("leftArm",{35.f+55.f*swim*room-std::sin(stroke)*110.f*swim*room-idle*9.f*(1.f-swim),0,25.f*(1.f-swim)*room});
    set("rightForearm",{25.f+35.f*std::max(0.f,std::cos(stroke))*swim,0,0});
    set("leftForearm",{25.f+35.f*std::max(0.f,-std::cos(stroke))*swim,0,0});
    set("rightLeg",{std::sin(stroke*2.f)*16.f*room,0,-4.f*room});set("leftLeg",{-std::sin(stroke*2.f)*16.f*room,0,4.f*room});
    set("rightShin",{18.f+idle*10.f,0,0});set("leftShin",{18.f-idle*10.f,0,0});
    set("head",{-pitch*.55f,0,0});
  } else {
    const bool jump=s.mode==HumanMotionMode::Jump;
    const float fall=std::clamp(-f.verticalSpeed/12.f,0.f,1.f);
    set("rightArm",{jump?-18.f:8.f,0,-(15.f+fall*25.f)});set("leftArm",{jump?-18.f:8.f,0,15.f+fall*25.f});
    set("rightForearm",{25,0,0});set("leftForearm",{25,0,0});
    set("rightLeg",{jump?-22.f:-8.f,0,0});set("leftLeg",{jump?-12.f:8.f,0,0});
    set("rightShin",{jump?42.f:20.f,0,0});set("leftShin",{jump?28.f:15.f,0,0});
  }
  if(ground)target.bones["waist"].offsetBlocks.y=-drop/in.scale;
  target.bones["head"].rotation=target.bones["head"].rotation*glm::angleAxis(glm::radians(f.bodyPitch),glm::vec3(1,0,0));
  // Partial upper-body layer. Free limbs retain locomotion.
  if(in.rightHandBusy){set("rightArm",{25,0,-6});set("rightForearm",{40,0,0});}
  if(in.leftHandBusy){set("leftArm",{25,0,6});set("leftForearm",{40,0,0});}
  if(s.actionRemaining>0.f) {
    const float strike=std::sin(3.14159265f*(1.f-std::clamp(s.actionRemaining/.28f,0.f,1.f)));
    set("rightArm",{35.f+70.f*strike,0,-6.f-12.f*strike});
    set("rightForearm",{40.f-30.f*strike,0,0});
  }
  const float a=s.initialized?1.f-std::exp(-dt/.06f):1.f; // 95% in 180ms
  for(auto &[name,p]:target.bones) {
    auto &prev=s.blended.bones[name];
    prev.rotation=glm::normalize(glm::slerp(prev.rotation,p.rotation,a));prev.quaternionRotation=true;
    // Stance follows the real motor blend, including blocked stand-up.
    prev.offsetBlocks=name=="waist" && ground?p.offsetBlocks:glm::mix(prev.offsetBlocks,p.offsetBlocks,a);
  }
  BoneSkeletonPose result=s.blended;
  if(f.swimming) {
    // Keep the actual face-eye landmark at the breathing controller's eye
    // height even while the torso and head finish their separate blends.
    const auto torso=result.bones["waist"].rotation*result.bones["spine"].rotation;
    const auto head=torso*result.bones["head"].rotation;
    const glm::vec3 eye=glm::vec3(0,.75f,0)+torso*glm::vec3(0,.75f,0)+head*glm::vec3(0,4.5f/16.f,-.25f);
    result.bones["waist"].offsetBlocks.y=(1.62f-.35f*f.stanceBlend)/in.scale-eye.y;
  }
  if(ground) {
    const auto yaw=glm::quat(glm::vec3(0,glm::radians(in.yawDeg),0));
    glm::vec3 travel=glm::inverse(yaw)*f.actualVelocity;travel.y=0.f;travel.z=-travel.z;
    travel=glm::length(travel)>.001f?glm::normalize(travel):glm::vec3(0,0,-1);
    auto toWorld=[&](glm::vec3 v){v.z=-v.z;return in.feet+yaw*(v*in.scale);};
    auto toLocal=[&](glm::vec3 v){v=glm::inverse(yaw)*(v-in.feet)/in.scale;v.z=-v.z;return v;};
    for(int side=0;side<2;++side) {
      const std::string prefix=side?"left":"right";
      float phase=s.distancePhase+(side?3.14159265f:0.f);
      float wave=std::sin(phase);
      bool stance=(speed<.05f || std::cos(phase)<0.f) && f.supportValid[side];
      glm::vec3 foot=glm::vec3(side?.125f:-.125f,.125f,0.f)+travel*(wave*stride*.25f/in.scale*move);
      if(!stance)foot.y+=.12f*std::max(0.f,std::cos(phase))*move;
      if(stance && (!s.stanceLast[side] || speed<.05f)) {
        s.contacts[side]=toWorld(foot);
        if(f.supportValid[side])s.contacts[side].y=f.supportY[side]+.125f*in.scale;
        if(!s.stanceLast[side]){s.unreachable[side]=false;s.ikWeights[side]=1.f;}
        else if(speed<.05f)s.unreachable[side]=false;
      }
      s.stanceLast[side]=stance;
      s.planted[side]=stance;
      if(stance)foot=toLocal(s.contacts[side]);
      // Solve in the blended pelvis frame. Shore/landing transitions retain
      // some whole-body pitch, so solving in the unrotated model frame would
      // rotate the already corrected sole back off the floor.
      const auto pelvisRotation=result.bones["waist"].rotation;
      const auto inversePelvis=glm::inverse(pelvisRotation);
      const glm::vec3 pelvis=glm::vec3(0,.75f,0)+result.bones["waist"].offsetBlocks;
      foot=inversePelvis*(foot-pelvis);
      glm::vec3 hip(side?.125f:-.125f,0.f,0.f);
      glm::vec3 delta=foot-hip;
      float distance=glm::length(delta);
      const float upperLength=.375f,lowerLength=.25f;
      const float maxReach=upperLength+lowerLength-.00001f;
      const float minReach=std::abs(upperLength-lowerLength)+.00001f;
      if(distance>maxReach){
        if(distance>upperLength+lowerLength+.001f)s.unreachable[side]=true;
        delta*=maxReach/std::max(distance,.001f);distance=maxReach;
      }
      if(distance<minReach) {
        if(distance<std::abs(upperLength-lowerLength)-.001f)s.unreachable[side]=true;
        delta=(distance>.00001f?delta/distance:glm::vec3(0,-1,0))*minReach;distance=minReach;
      }
      if(!stance)s.unreachable[side]=false;
      if(s.unreachable[side])s.planted[side]=false;
      s.ikWeights[side]=glm::mix(s.ikWeights[side],s.unreachable[side]?0.f:1.f,1.f-std::exp(-dt/.06f));
      const glm::vec3 axis=delta/distance;
      glm::vec3 pole=inversePelvis*glm::vec3(0,0,-1);
      pole-=axis*glm::dot(pole,axis);
      if(glm::length(pole)<.00001f) {
        pole=inversePelvis*glm::vec3(0,-1,0);pole-=axis*glm::dot(pole,axis);
      }
      pole=glm::normalize(pole);
      const float along=(upperLength*upperLength-lowerLength*lowerLength+distance*distance)/(2.f*distance);
      const glm::vec3 knee=hip+axis*along+pole*std::sqrt(std::max(0.f,upperLength*upperLength-along*along));
      const auto upper=FromDown(glm::normalize(knee-hip));
      const auto lower=FromDown(glm::normalize(hip+delta-knee));
      auto correct=[&](const std::string &bone,glm::quat rotation) {
        auto &value=result.bones[bone].rotation;value=glm::normalize(glm::slerp(value,rotation,s.ikWeights[side]));
      };
      correct(prefix+"Leg",upper);correct(prefix+"Shin",glm::inverse(upper)*lower);correct(prefix+"Foot",glm::inverse(lower)*inversePelvis);
    }
  } else {s.planted={false,false};s.stanceLast={false,false};}
  s.blended=result; // A mode transition starts from the final corrected pose.
  s.initialized=true;
  return result;
 }
};
}

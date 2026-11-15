#include "Creatures/Visual/BoneSkeleton/BoneSkeletonCubeMeshBuilder.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonHierarchy.h"
#include "Creatures/Visual/BoneSkeleton/CreatureAttachmentBounds.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonLoader.h"
#include "Creatures/Visual/CreatureEquipmentFit.h"
#include "Pose/HumanBowPose.h"
#include "Pose/HumanMotionController.h"
#include "Render/Camera/CameraVolumeSweep.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>

using namespace cutum;
void Require(bool value, const char *message)
{
  if (!value)
  {
    std::cerr << message << '\n';
    std::exit(1);
  }
}
glm::vec3 Point(const BoneSkeletonHierarchy &rig,
                const CreatureBoneSkeletonGeometry &g,
                const BoneSkeletonPose &pose, const char *bone, glm::vec3 local)
{
  return glm::vec3(rig.ComputeBoneMatrix(g.boneIndexByName.at(bone), pose) *
                   glm::vec4(local, 1));
}
void CheckConsumePose(const CreatureBoneSkeletonGeometry &g)
{
  BoneSkeletonHierarchy rig(g);
  const auto mesh = BoneSkeletonCubeMeshBuilder::BuildMeshAsset(g);
  glm::vec3 low, high;
  Require(ResolveCreatureAttachmentBounds(mesh, rig, "rightItem", low, high),
          "derive real palm from the drawable hand ancestor");
  const auto grip = CreaturePalmGripPosition(low, high);
  Require(glm::length(grip) > .1f,
          "fixture must not substitute the item pivot for the palm");
  for (int fps : {30, 60, 144})
  {
    HumanMotionInput input;
    input.facts.onGround = true;
    input.rightHandBusy = true;
    HumanMotionState restState, heldState;
    const auto rest = UHumanMotionController::Compute(input, restState, 0);
    input.consumePhase = .5f;
    const auto held = UHumanMotionController::Compute(input, heldState, 0);
    const auto palm = Point(rig, g, held, "rightItem", grip);
    const auto restingPalm = Point(rig, g, rest, "rightItem", grip);
    const auto mouth = Point(rig, g, held, "head", {0, .16f, -.28f});
    Require(palm.y > restingPalm.y + .2f,
            "consumption raises the actual item socket");
    Require(glm::distance(palm, mouth) < .35f && palm.z < mouth.z,
            "consumption socket approaches the face from outside");
    Require(std::abs(glm::dot(held.bones.at("leftArm").rotation,
                              rest.bones.at("leftArm").rotation)) > .9999f,
            "consumption leaves the free arm alone");
    input.consumePhase = 1.f;
    HumanMotionState fullState;
    const auto full = UHumanMotionController::Compute(input, fullState, 0);
    Require(glm::distance(Point(rig, g, full, "rightItem", grip), palm) <
                .0001f,
            "ready consumption holds the same contact");
    input.consumePhase = 0;
    BoneSkeletonPose canceled;
    for (int frame = 0; frame < fps; ++frame)
      canceled = UHumanMotionController::Compute(input, heldState, 1.f / fps);
    Require(glm::distance(Point(rig, g, canceled, "rightItem", grip),
                          restingPalm) < .001f,
            "cancellation blends back to the correct wield pose");
    input.carryHands = 2;
    HumanMotionState carrying, conflict;
    const auto carry = UHumanMotionController::Compute(input, carrying, 0);
    input.consumePhase = 1;
    const auto occupied = UHumanMotionController::Compute(input, conflict, 0);
    Require(glm::distance(Point(rig, g, occupied, "rightItem", grip),
                          Point(rig, g, carry, "rightItem", grip)) < .0001f,
            "cargo occupancy wins over stale consumption input");
  }
}
void CheckBowPose(const CreatureBoneSkeletonGeometry &g)
{
  BoneSkeletonHierarchy rig(g);
  const auto mesh = BoneSkeletonCubeMeshBuilder::BuildMeshAsset(g);
  ItemRenderAsset bow;
  bow.Grip.SupportGripNode = "pull";
  bow.Grip.SupportGripIndex = 1;
  bow.Grip.HasRightGrip = true;
  bow.Nodes.resize(2);
  bow.Nodes[0].Name = "bow";
  bow.Nodes[1].Name = "pull";
  bow.Nodes[1].Parent = 0;
  bow.Nodes[1].Local = glm::translate(glm::mat4(1), glm::vec3(0, 0, -.025f));
  bow.Nodes[1].Transform = bow.Nodes[1].Local;
  ItemNodeBinding binding;
  binding.Node = 1;
  binding.TranslationTo.z = .30f;
  bow.Bindings.push_back(binding);
  glm::vec3 lowR, highR, lowL, highL;
  Require(
      ResolveCreatureAttachmentBounds(mesh, rig, "rightItem", lowR, highR) &&
          ResolveCreatureAttachmentBounds(mesh, rig, "leftItem", lowL, highL),
      "bow uses actual drawable palm bounds");
  const auto palmR = CreaturePalmGripPosition(lowR, highR);
  const auto palmL = CreaturePalmGripPosition(lowL, highL);
  for (float pitch : {-80.f, 0.f, 80.f})
    for (float crouch : {0.f, 1.f})
      for (float charge : {.01f, .5f, 1.f})
      {
        HumanMotionInput input;
        input.facts.onGround = true;
        input.facts.bodyPitch = pitch;
        input.facts.stanceBlend = crouch;
        input.rightHandBusy = input.leftHandBusy = input.bowEquipped = true;
        input.bowCharge = charge;
        HumanMotionState state;
        auto pose = UHumanMotionController::Compute(input, state, 0);
        ItemVisualInputs visual;
        visual.BowCharge = charge;
        visual.HasAmmo = true;
        Require(UHumanBowPose::Apply(mesh, rig, pose, bow, visual, 1,
                                     state.BowPresentationWeight),
                "human bow contact solve succeeds");
        const auto main = Point(rig, g, pose, "rightItem", palmR);
        const auto head =
            rig.ComputeBoneMatrix(g.boneIndexByName.at("head"), pose);
        const auto model = ItemBowModelMatrix(bow, main, head, 1);
        const auto nodes = ResolveItemNodeTransforms(
            bow, EvaluateItemVisualState(bow, visual));
        const auto support = glm::vec3(model * nodes[1][3]);
        Require(glm::distance(Point(rig, g, pose, "leftItem", palmL), support) <
                    .0001f,
                "support palm touches the actual string through "
                "pitch/crouch/charge");
        if (pitch == 0 && charge == 1)
        {
          Require(glm::distance(support, Point(rig, g, pose, "head",
                                               {0, .16f, -.28f})) < .15f,
                  "full draw brings the string hand near the face");
        }
        const auto aim =
            glm::normalize(glm::vec3(head * glm::vec4(0, 0, -1, 0)));
        const auto arrow =
            glm::normalize(glm::vec3(model * glm::vec4(0, 0, -1, 0)));
        Require(glm::dot(aim, arrow) > .99999f,
                "bow aims with the rendered head");
        state.blended = pose;
        const auto retained = UHumanMotionController::Compute(input, state, 0);
        Require(glm::distance(Point(rig, g, retained, "leftItem", palmL),
                              support) < .0001f,
                "zero elapsed time retains the solved contacts");
        for (int fps : {30, 60, 144})
        {
          auto released = state;
          input.bowCharge = 0;
          for (int frame = 0; frame < fps; ++frame)
            UHumanMotionController::Compute(input, released, 1.f / fps);
          Require(released.BowPresentationWeight == 0,
                  "release finishes visual retraction");
        }
        input.carryHands = 2;
        UHumanMotionController::Compute(input, state, 1.f / 60);
        Require(state.BowPresentationWeight == 0,
                "cargo immediately owns both hands");
      }
}
int main(int argc, char **argv)
{
  Require(argc == 2, "geometry path required");
  Require(std::abs(UHumanMotionController::MeasureVelocity(
                       {.05f, 0, 0}, 1.f / 60.f, {0, 0, 0})
                       .x -
                   3.f) < .0001f,
          "actual speed uses simulated time");
  Require(
      UHumanMotionController::MeasureVelocity({0, 0, 0}, 0.f, {3, 0, 0}).x ==
          3.f,
      "no-step render frame preserves measured speed");
  Require(
      UHumanMotionController::MeasureVelocity({0, 0, 0}, 1.f / 60.f, {3, 0, 0})
              .x == 0.f,
      "blocked physical step stops progress");
  Require(
      UHumanMotionController::MeasureVelocity({0, 10, 0}, 1.f / 60.f, {0, 0, 0})
              .y == 0.f,
      "teleport is not a vertical movement impulse");
  auto loaded = CreatureBoneSkeletonLoader::LoadFromFile(argv[1], "");
  Require(loaded.has_value(), "load generated rig");
  const auto &g = *loaded;
  Require(g.humanRigVersion == 2 && g.modelScale == .9f,
          "versioned human scale");
  BoneSkeletonHierarchy rig(g);
  CheckConsumePose(g);
  CheckBowPose(g);
  const auto female = CreatureBoneSkeletonLoader::LoadFromFile(
      (std::filesystem::path(argv[1]).parent_path() / "female_short.geo.json")
          .string(),
      "");
  Require(female.has_value(), "load female rig for consumption contacts");
  CheckConsumePose(*female);
  CheckBowPose(*female);
  for (float direction : {-1.f, 1.f})
  {
    HumanMotionState counter;
    counter.distancePhase = 1.57079633f;
    HumanMotionInput walk;
    walk.facts.onGround = true;
    walk.facts.horizontalSpeed = 3.f;
    walk.facts.actualVelocity = {0, 0, direction * 3.f};
    walk.facts.supportValid[0] = walk.facts.supportValid[1] = true;
    const auto pose = UHumanMotionController::Compute(walk, counter, 0.f);
    Require(pose.bones.at("rightArm").rotation.x *
                    pose.bones.at("rightLeg").rotation.x <
                0.f,
            "arm counter-swing follows forward and backward steps");
  }
  for (int fps : {30, 60, 144})
  {
    HumanMotionState state;
    HumanMotionInput input;
    input.facts.onGround = true;
    input.facts.supportValid[0] = input.facts.supportValid[1] = true;
    input.facts.stanceBlend = 1.f;
    auto crouch = UHumanMotionController::Compute(input, state, 1.f / fps);
    auto sole = Point(rig, g, crouch, "rightFoot", {0, -.125f, 0});
    Require(std::abs(sole.y * .9f) < .01f, "crouch sole remains on ground");
    Require(crouch.bones.at("rightShin").rotation.w < .99f,
            "crouch bends knee");
    auto crown = Point(rig, g, crouch, "head", {0, .5f, 0});
    Require(crown.y * .9f < 1.51f, "crouched body fits active height");
    state = {};
    input.facts.stanceBlend = 0;
    input.facts.horizontalSpeed = 3;
    input.facts.actualVelocity = {0, 0, 3};
    input.facts.state = LocomotionState::Walk;
    glm::vec3 contactStart(0);
    bool locked = false;
    float maxDrift = 0;
    for (int frame = 0; frame < fps * 2; ++frame)
    {
      input.feet.z += 3.f / fps;
      auto pose = UHumanMotionController::Compute(input, state, 1.f / fps);
      auto foot = Point(rig, g, pose, "rightFoot", {0, -.125f, 0}) * .9f;
      foot.z = -foot.z;
      foot += input.feet;
      if (!locked && state.planted[0])
        contactStart = foot;
      if (locked && state.planted[0])
        maxDrift = std::max(maxDrift, glm::distance(contactStart, foot));
      if (state.planted[0])
        Require(std::abs(foot.y) < .01f, "walking sole height");
      locked = state.planted[0];
    }
    Require(maxDrift <= .02f, "planted foot drift");
    std::cout << fps << " FPS maximum stance drift=" << maxDrift << " blocks\n";
    for (auto gait : {LocomotionState::Run, LocomotionState::Crouch})
      for (const auto direction :
           {glm::vec3(0, 0, 1), glm::vec3(1, 0, 0), glm::vec3(0, 0, -1)})
      {
        HumanMotionInput moving;
        HumanMotionState motion;
        moving.facts.state = gait;
        moving.facts.onGround = true;
        moving.facts.stanceBlend = gait == LocomotionState::Crouch ? 1.f : 0.f;
        moving.facts.horizontalSpeed =
            gait == LocomotionState::Run ? 3.9f : .9f;
        moving.facts.actualVelocity = direction * moving.facts.horizontalSpeed;
        moving.facts.supportValid[0] = moving.facts.supportValid[1] = true;
        std::array<bool, 2> lockedFeet{{false, false}};
        std::array<glm::vec3, 2> anchors{};
        std::array<int, 2> contacts{{0, 0}};
        float drift = 0.f;
        for (int frame = 0; frame < fps * 2; ++frame)
        {
          moving.feet += moving.facts.actualVelocity / static_cast<float>(fps);
          const auto pose =
              UHumanMotionController::Compute(moving, motion, 1.f / fps);
          for (int side = 0; side < 2; ++side)
          {
            auto sole = Point(rig, g, pose, side ? "leftFoot" : "rightFoot",
                              {0, -.125f, 0}) *
                        .9f;
            sole.z = -sole.z;
            sole += moving.feet;
            if (motion.planted[side])
            {
              if (!lockedFeet[side])
              {
                anchors[side] = sole;
                ++contacts[side];
              }
              drift = std::max(drift, glm::distance(anchors[side], sole));
              Require(std::abs(sole.y) < .01f, "run/crouch sole height");
            }
            lockedFeet[side] = motion.planted[side];
          }
        }
        Require(drift <= .02f && contacts[0] >= 2 && contacts[1] >= 2,
                "run/crouch forward/side/back contact phases");
      }
    input.facts.onGround = false;
    input.facts.mode = CreatureMovementMode::Flying;
    input.facts.horizontalSpeed = 0;
    auto fly1 = UHumanMotionController::Compute(input, state, 1.f / fps);
    for (int i = 0; i < fps; ++i)
      UHumanMotionController::Compute(input, state, 1.f / fps);
    auto fly2 = UHumanMotionController::Compute(input, state, 1.f / fps);
    Require(glm::length(fly1.bones.at("rightArm").rotation -
                        fly2.bones.at("rightArm").rotation) > .001f,
            "hover keeps moving");
    input.facts.mode = CreatureMovementMode::Walking;
    input.facts.swimming = true;
    input.facts.headSubmerged = true;
    auto swim = UHumanMotionController::Compute(input, state, 1.f / fps);
    Require(state.mode == HumanMotionMode::Submerged,
            "submerged classification");
    auto eyes = Point(rig, g, swim, "head", {0, 4.5f / 16.f, -.25f});
    Require(std::abs(eyes.y * .9f - 1.62f) < .001f,
            "animated eyes match physical breathing height");
    input.facts.swimming = false;
    input.facts.verticalSpeed = -8;
    UHumanMotionController::Compute(input, state, 1.f / fps);
    Require(state.mode == HumanMotionMode::Fall, "fall classification");
    input.facts.onGround = true;
    input.facts.verticalSpeed = 0;
    UHumanMotionController::Compute(input, state, 1.f / fps);
    UHumanMotionController::Compute(input, state, 1.f / fps);
    Require(state.landingEvents == 1, "single landing event");
    HumanMotionState shore;
    HumanMotionInput shoreInput;
    shoreInput.facts.onGround = false;
    shoreInput.facts.swimming = true;
    shoreInput.facts.horizontalSpeed = 3.f;
    UHumanMotionController::Compute(shoreInput, shore, 1.f / fps);
    shoreInput.facts.onGround = true;
    shoreInput.facts.swimming = false;
    shoreInput.facts.horizontalSpeed = 0.f;
    shoreInput.facts.supportValid[0] = shoreInput.facts.supportValid[1] = true;
    const auto shorePose =
        UHumanMotionController::Compute(shoreInput, shore, 1.f / fps);
    Require(std::abs(Point(rig, g, shorePose, "rightFoot", {0, -.125f, 0}).y *
                     .9f) < .01f,
            "post-blend shore landing keeps sole grounded");
    HumanMotionState unreachable;
    HumanMotionInput ledge;
    ledge.facts.supportValid[0] = ledge.facts.supportValid[1] = true;
    ledge.facts.supportY[0] = -5.f;
    UHumanMotionController::Compute(ledge, unreachable, 1.f / fps);
    Require(!unreachable.planted[0] && unreachable.ikWeights[0] > 0.f &&
                unreachable.ikWeights[0] < 1.f,
            "unreachable IK fades instead of stretching");
    HumanMotionInput water;
    water.facts.onGround = false;
    water.facts.swimming = true;
    water.facts.horizontalSpeed = 3.f;
    HumanMotionState openWater, tightWater;
    auto openPose = UHumanMotionController::Compute(water, openWater, 0.f);
    water.facts.swimClearance = .15f;
    auto tightPose = UHumanMotionController::Compute(water, tightWater, 0.f);
    Require(std::abs(glm::angle(tightPose.bones.at("rightArm").rotation)) <
                std::abs(glm::angle(openPose.bones.at("rightArm").rotation)),
            "tight water groups arms without changing collider");
    HumanMotionState folded;
    HumanMotionInput close;
    close.facts.stanceBlend = 1.f;
    close.facts.supportValid[0] = close.facts.supportValid[1] = true;
    close.facts.supportY[0] = .3375f;
    const auto foldedPose =
        UHumanMotionController::Compute(close, folded, 1.f / fps);
    Require(!folded.planted[0], "too-close support disables IK");
    for (const char *bone : {"rightLeg", "rightShin", "rightFoot"})
      Require(std::isfinite(foldedPose.bones.at(bone).rotation.w),
              "folded IK stays finite");
    for (int frame = 0; frame < fps / 2; ++frame)
      UHumanMotionController::Compute(close, folded, 1.f / fps);
    Require(folded.ikWeights[0] < .001f,
            "unreachable idle contact completes its fade");
    HumanMotionState aligned;
    aligned.distancePhase = 1.57079633f;
    close.facts.horizontalSpeed = .9f;
    close.facts.actualVelocity = {0, 0, .9f};
    const auto alignedPose =
        UHumanMotionController::Compute(close, aligned, 0.f);
    Require(std::isfinite(alignedPose.bones.at("rightLeg").rotation.w),
            "aligned bend pole uses stable fallback");
  }
  HumanMotionInput oneHand;
  oneHand.facts.onGround = true;
  oneHand.carryHands = 1;
  HumanMotionState oneState, twoState, chargedState;
  const auto onePose = UHumanMotionController::Compute(oneHand, oneState, 0.f);
  auto both = oneHand;
  both.carryHands = 2;
  const auto twoPose = UHumanMotionController::Compute(both, twoState, 0.f);
  Require(std::abs(glm::dot(onePose.bones.at("leftArm").rotation,
                            twoPose.bones.at("leftArm").rotation)) < .98f,
          "second hand joins a bulky hold");
  both.throwCharge = 1.f;
  const auto charged = UHumanMotionController::Compute(both, chargedState, 0.f);
  Require(std::abs(glm::dot(twoPose.bones.at("rightArm").rotation,
                            charged.bones.at("rightArm").rotation)) < .99f,
          "windup changes hold pose");
  Require(chargedState.lastCarryHands == 2,
          "two hands retained for release layer");
  const float radius = CameraVolumeRadius(.1f, 90.f, 16.f / 9.f);
  Require(radius >= .20f, "near-plane corners protected");
  Require(CameraVolumeRadius(.3f, 110.f, 2.4f) > radius,
          "wide lens expands volume");
  auto hit = SweepCameraAabb({0, 0, 0}, {0, 0, 4}, {-.5f, -.5f, 2.f},
                             {.5f, .5f, 3.f}, .2f);
  Require(std::abs(hit - .45f) < .0001f, "analytic camera wall sweep");
  Require(SweepCameraAabb({0, 0, 0}, {0, 0, 0}, {-1, -1, -1}, {1, 1, 1}, .2f) ==
              0,
          "initial overlap detected");
  Require(SweepCameraAabb({2, 0, 0}, {2, 0, 4}, {-.5f, -.5f, 2.f},
                          {.5f, .5f, 3.f}, .2f) == 1,
          "clear camera path");
  std::cout << "human_motion_camera_test: passed at 30/60/144 FPS\n";
}

#include "Creatures/Visual/CreatureVisualBoneSkeleton.h"

#include "App/Platform/IUPlatformPaths.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonHierarchy.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonModelSpace.h"
#include "Creatures/Visual/BoneSkeleton/CreatureAttachmentBounds.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonCache.h"
#include "Creatures/Visual/CreatureDrawRequest.h"
#include "Creatures/Visual/CreatureRootTransform.h"
#include "Creatures/Visual/CreatureTextureResolver.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ItemInspectionSource.h"
#include "Pose/BoneSkeleton/BoneSkeletonPoseEngine.h"
#include "Pose/BoneSkeleton/SkeletalAnimationProfiles.h"
#include "Pose/HumanBowPose.h"
#include "Pose/HumanCarryPose.h"
#include "Pose/HumanMotionController.h"
#include "Render/Engine/GeometryEngine.h"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <limits>

namespace cutum
{

UCreatureVisualBoneSkeleton::~UCreatureVisualBoneSkeleton() = default;

static bool IsCarryArmBone(const std::string &name)
{
  return name == "rightArm" || name == "rightForearm" || name == "rightHand" ||
         name == "leftArm" || name == "leftForearm" || name == "leftHand";
}

bool UCreatureVisualBoneSkeleton::GetWorldBoneTransform(const std::string &name,
                                                        glm::mat4 &out) const
{
  if (!MeshAsset)
    return false;
  const auto bone = MeshAsset->geometry.boneIndexByName.find(name);
  if (bone == MeshAsset->geometry.boneIndexByName.end() ||
      bone->second >= CachedBoneMatrices.size())
    return false;
  const glm::mat4 root = BuildCreatureRootMatrix(
      BodyOrigin, BodyYawDeg, 0.f,
      []() { return BoneSkeletonEntityConventionMatrix(); },
      MeshAsset ? MeshAsset->geometry.modelScale : 1.f);
  out = root * CachedBoneMatrices[bone->second];
  return true;
}

std::unique_ptr<IUCreatureVisual> CreateCreatureVisualBoneSkeleton()
{
  return std::make_unique<UCreatureVisualBoneSkeleton>();
}

bool UCreatureVisualBoneSkeleton::GetBoneLocalBounds(const std::string &name,
                                                     glm::vec3 &low,
                                                     glm::vec3 &high) const
{
  if (!MeshAsset || !Hierarchy)
    return false;
  if (const auto cached = AttachmentBounds.find(name);
      cached != AttachmentBounds.end())
  {
    low = cached->second.first;
    high = cached->second.second;
    return true;
  }
  const bool any =
      ResolveCreatureAttachmentBounds(*MeshAsset, *Hierarchy, name, low, high);
  if (any)
    AttachmentBounds.emplace(name, std::make_pair(low, high));
  return any;
}

void UCreatureVisualBoneSkeleton::UpdatePose(
    const UCreature &creature, const CreatureLocomotionFacts &inputFacts,
    const CreaturePoseParams &presentation, const CreatureDefinition &animDef,
    float dt)
{
  auto facts = inputFacts;
  if (creature.ExternalMotion)
  {
    facts.onGround = false;
    facts.horizontalSpeed = 0;
    facts.actualVelocity = {};
    facts.state = LocomotionState::Fall;
    facts.verticalSpeed = -.1f;
  }
  BodyOrigin = creature.GetFeetPosition();
  BodyYawDeg = ComputeBodyYawDeg(creature, animDef);
  SpeciesId = animDef.Id;
  const std::string geoFile = !Appearance.geometryFileOverride.empty()
                                  ? Appearance.geometryFileOverride
                              : animDef.visual.boneSkeleton.geometryFile.empty()
                                  ? "geometry.geo.json"
                                  : animDef.visual.boneSkeleton.geometryFile;
  GeometryId = animDef.visual.boneSkeleton.geometryId;
  TextureStem = animDef.visual.boneSkeleton.textureStem.empty()
                    ? "diffuse"
                    : animDef.visual.boneSkeleton.textureStem;
  RefreshDefaultTextureKey(animDef);

  auto &meshCache = CreatureBoneSkeletonCache::Instance();
  const uint64_t revision = meshCache.GetRevision();
  const bool reloaded = !MeshRequestSeen || LoadedRevision != revision ||
                        LoadedSpecies != SpeciesId || LoadedFile != geoFile ||
                        LoadedGeometry != GeometryId ||
                        LoadedDirectory != animDef.visual.sourceDirectory;
  if (reloaded)
  {
    MeshRequestSeen = true;
    LoadedRevision = revision;
    LoadedSpecies = SpeciesId;
    LoadedFile = geoFile;
    LoadedGeometry = GeometryId;
    LoadedDirectory = animDef.visual.sourceDirectory;
    Hierarchy.reset();
    AttachmentBounds.clear();
    CachedBoneMatrices.clear();
    CarryPose = {};
    CarryWeight = 0;
    GroundClearance = {};
    MeshAsset = meshCache.Load(SpeciesId, geoFile, GeometryId, LoadedDirectory);
    if (MeshAsset)
    {
      Hierarchy = std::make_unique<BoneSkeletonHierarchy>(MeshAsset->geometry);
    }
    else
    {
      const std::string key = SpeciesId + "|" + geoFile + "|" + GeometryId;
      LogCreatureMissingTextureOnce(
          key, "UCreatureVisualBoneSkeleton: missing mesh asset for species=" +
                   SpeciesId + " geoFile=" + geoFile +
                   " geometryId=" + GeometryId);
    }
  }

  const bool humanV2 = animDef.Id == "human" && MeshAsset &&
                       MeshAsset->geometry.humanRigVersion == 2;
  if (humanV2)
  {
    HumanMotionInput input;
    input.facts = facts;
    input.feet = BodyOrigin;
    input.yawDeg = BodyYawDeg;
    const auto *active = creature.GetInventory().GetActiveEntryRef();
    input.rightHandBusy = active && !active->empty && !active->Id.empty();
    const auto &off = creature.GetInventory().GetEquippedOffhand();
    input.leftHandBusy =
        creature.CarryHandCount == 0 && !off.empty && !off.Id.empty();
    input.carryHands = creature.CarryHandCount;
    input.throwCharge = creature.ThrowCharge;
    input.bowCharge = creature.BowCharge;
    const auto *items = creature.GetInventory().GetItemDefinitions();
    const auto *held = items && active && !active->empty && !active->broken &&
                               active->kind == InventoryEntryKind::Item
                           ? items->Get(active->Id)
                           : nullptr;
    input.bowEquipped = held && held->Ranged.Enabled;
    if (active && !active->empty && !active->broken)
      input.consumePhase = creature.ItemUse.Phase(
          active->Id, active->instanceId, creature.ItemVisualSelection());
    const bool initialized = creature.HumanMotion.initialized;
    BonePose = UHumanMotionController::Compute(input, creature.HumanMotion, dt,
                                               creature.IsPossessed());
    if (input.bowEquipped && input.carryHands == 0 &&
        creature.HumanMotion.BowPresentationWeight > 0 &&
        (dt > 0 || !initialized || reloaded))
      if (auto *paths = IUPlatformPaths::TryGet())
      {
        const auto *inspection = FindItemInspectionSource(active->Id);
        const auto asset = inspection
                               ? inspection->Asset
                               : FindItemSurfaceAsset(
                                     paths->AssetRoot() / held->ModelPath,
                                     UItemDefinitionStorage::ContentRevision());
        if (asset && UHumanBowPose::Apply(
                         *MeshAsset, *Hierarchy, BonePose, *asset,
                         MakeItemVisualInputs(
                             active, creature.BowCharge, 0,
                             creature.GetInventory().IsCreative() ||
                                 creature.GetInventory().Count(
                                     InventoryEntryKind::Item, "arrow") > 0),
                         held->Visual.TpScale,
                         creature.HumanMotion.BowPresentationWeight))
          creature.HumanMotion.blended = BonePose;
      }
  }
  else
    BonePose = BoneSkeletonPoseEngine::Compute(facts, animDef, dt);
  if (humanV2)
  {
    const bool carrying = presentation.CarryHands > 0;
    if (carrying)
    {
      auto corrected = BonePose;
      const auto root = BuildCreatureRootMatrix(
          BodyOrigin, BodyYawDeg, 0.f,
          []() { return BoneSkeletonEntityConventionMatrix(); },
          MeshAsset->geometry.modelScale);
      const auto inverseRoot = glm::inverse(root);
      auto targets = presentation.CarryContacts;
      for (auto &target : targets)
        target = glm::vec3(inverseRoot * glm::vec4(target, 1));
      if (UHumanCarryPose::Apply(*MeshAsset, *Hierarchy, corrected, targets,
                                 presentation.CarryHands))
        CarryPose = std::move(corrected);
    }
    const float target = carrying ? 1.f : 0.f;
    CarryWeight =
        dt > 0 ? glm::mix(CarryWeight, target, 1.f - std::exp(-dt / .035f))
               : target;
    if (CarryWeight > .0001f && !CarryPose.bones.empty())
      UHumanCarryPose::Blend(BonePose, CarryPose, CarryWeight);
    // Do not feed this post-solve correction back into locomotion blending:
    // doing so would accumulate torso bend on the following frame.
  }
  CachedBoneMatrices.clear();
  if (MeshAsset && Hierarchy)
  {
    CachedBoneMatrices.resize(MeshAsset->boneMeshes.size());
    for (size_t boneIdx = 0; boneIdx < CachedBoneMatrices.size(); ++boneIdx)
    {
      CachedBoneMatrices[boneIdx] =
          Hierarchy->ComputeBoneMatrix(boneIdx, BonePose);
    }
  }

  const bool groundClearance =
      !humanV2 &&
      UsesBoneGroundClearance(
          facts, animDef, SkeletalAnimationProfiles::ResolveProfileId(animDef));
  const float requiredLift =
      groundClearance && MeshAsset
          ? ComputeBoneGroundClearance(*MeshAsset, CachedBoneMatrices)
          : 0.f;
  // The common root also carries equipment. Bone matrices, pivots, authored
  // rotations and physics/collider position remain untouched. On takeoff the
  // small clearance offset decays instead of snapping off in one frame.
  if (humanV2)
    GroundClearance =
        {}; // Human IK owns contact; a legacy-pack lift must not leak back.
  else
    BodyOrigin.y += GroundClearance.Update(requiredLift, groundClearance, dt);

  PoseCreature = &creature;
}

void UCreatureVisualBoneSkeleton::SubmitDraw(const CreatureDrawContext &ctx,
                                             const glm::mat4 &viewProj)
{
  CarryArmsDrawn = false;
  if (!MeshAsset || !Hierarchy || CachedBoneMatrices.empty() ||
      !DrawEnabled(ctx))
  {
    return;
  }
  const UCreatureTextureStorage &creatureTextures = *ctx.Textures;
  const RenderSettings &settings = *ctx.Settings;

  const GLuint tex = ResolveCreatureSpeciesTexture(
      creatureTextures, SpeciesId, TextureStem, DefaultTextureKey, &Appearance);
  if (tex == 0)
  {
    const std::string key =
        SpeciesId + "|" + TextureStem + "|" + DefaultTextureKey;
    LogCreatureMissingTextureOnce(
        key, "UCreatureVisualBoneSkeleton: missing texture for species=" +
                 SpeciesId + " stem=" + TextureStem + " default=" +
                 DefaultTextureKey + " skin=" + Appearance.effectiveSkinId);
    return;
  }

  const glm::mat4 bodyMat = BuildCreatureRootMatrix(
      BodyOrigin, BodyYawDeg, 0.f,
      []() { return BoneSkeletonEntityConventionMatrix(); },
      MeshAsset ? MeshAsset->geometry.modelScale : 1.f);

  CreatureDrawQueue &queue = *ctx.Queue;
  const auto firstArmRequest = queue.Size();
  for (size_t boneIdx = 0; boneIdx < MeshAsset->boneMeshes.size(); ++boneIdx)
  {
    if (ctx.ArmsOnly &&
        !IsCarryArmBone(MeshAsset->geometry.bones[boneIdx].name))
      continue;
    const auto &headGear = PoseCreature->GetInventory().GetEquippedArmor(0);
    if (MeshAsset->geometry.bones[boneIdx].name == "hair" &&
        (Appearance.hideHair || (!headGear.empty && !headGear.broken &&
                                 !headGear.Id.empty() && headGear.wear < 1.f)))
      continue;
    const glm::mat4 boneMat = bodyMat * CachedBoneMatrices[boneIdx];
    const BoneSkeletonBoneMeshCpu &boneMesh = MeshAsset->boneMeshes[boneIdx];
    for (const BoneSkeletonCubeMeshCpu &cube : boneMesh.cubes)
    {
      if (cube.interleavedPosUv.empty() || cube.indices.empty())
      {
        continue;
      }
      CreatureDrawRequest req;
      req.Kind = CreatureDrawKind::SkeletalMesh;
      req.Mvp = viewProj * boneMat * cube.restLocalMatrix;
      req.Texture = tex;
      req.SkeletalMesh = {MeshAsset, &cube};
      queue.Push(std::move(req));
    }
  }

  CarryArmsDrawn = ctx.ArmsOnly && queue.Size() > firstArmRequest;

  if (settings.CreatureWireframeOverlay)
  {
    for (size_t boneIdx = 0; boneIdx < MeshAsset->boneMeshes.size(); ++boneIdx)
    {
      if (ctx.ArmsOnly &&
          !IsCarryArmBone(MeshAsset->geometry.bones[boneIdx].name))
        continue;
      const auto &headGear = PoseCreature->GetInventory().GetEquippedArmor(0);
      if (MeshAsset->geometry.bones[boneIdx].name == "hair" &&
          (Appearance.hideHair ||
           (!headGear.empty && !headGear.broken && !headGear.Id.empty() &&
            headGear.wear < 1.f)))
        continue;
      const glm::mat4 boneMat = bodyMat * CachedBoneMatrices[boneIdx];
      const BoneSkeletonBoneMeshCpu &boneMesh = MeshAsset->boneMeshes[boneIdx];
      for (const BoneSkeletonCubeMeshCpu &cube : boneMesh.cubes)
      {
        if (cube.interleavedPosUv.empty())
        {
          continue;
        }
        CreatureDrawRequest req;
        req.Kind = CreatureDrawKind::WireframeBox;
        req.Mvp = viewProj * boneMat * cube.restLocalMatrix;
        req.WireColor = Appearance.wireframeColor;
        queue.Push(std::move(req));
      }
    }
  }

  if (PoseCreature)
  {
    const auto boneLookup = [this, &ctx](const std::string &boneName,
                                         glm::mat4 &out) -> bool
    {
      if (ctx.ArmsOnly && !IsCarryArmBone(boneName))
        return false;
      if (!MeshAsset || !Hierarchy)
      {
        return false;
      }
      const auto it = MeshAsset->geometry.boneIndexByName.find(boneName);
      if (it == MeshAsset->geometry.boneIndexByName.end())
      {
        return false;
      }
      if (it->second >= CachedBoneMatrices.size())
      {
        return false;
      }
      out = CachedBoneMatrices[it->second];
      return true;
    };
    // Worn equipment submits through the same draw queue as the body; item
    // definitions come with the context (scale falls back to category
    // defaults via id when the storage is absent).
    if (ctx.Queue)
    {
      const BoneBoundsLookupFn bounds =
          [this](const std::string &name, glm::vec3 &low, glm::vec3 &high)
      { return GetBoneLocalBounds(name, low, high); };
      WornEquipmentDrawer::SubmitFromCreature(
          *ctx.Queue, *PoseCreature, viewProj, bodyMat, boneLookup, bounds);
      if (!ctx.SkipWieldedItems)
        WornEquipmentDrawer::SubmitWieldedFromCreature(
            *ctx.Queue, *PoseCreature, ctx.Items, viewProj, bodyMat, boneLookup,
            bounds);
    }
  }
}

} // namespace cutum

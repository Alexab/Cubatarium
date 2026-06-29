#include "Creatures/Visual/Skeletal/CreatureSkeletalGeoLoader.h"
#include "Creatures/Visual/Skeletal/SkeletalCubeMeshBuilder.h"
#include "Creatures/Visual/Skeletal/SkeletalModelSpace.h"
#include "Creatures/Visual/Skeletal/CreatureBoneHierarchy.h"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <string>

namespace
{

constexpr const char *kCowGeo =
    "models/creatures/_sources/bedrock_geo/cow.geo.json";

bool NearlyEqual(float a, float b, float eps = 0.001f)
{
  return std::fabs(a - b) <= eps;
}

constexpr float kUvInsetPx = 0.5f;

float InsetNormU0(int px, float texWidth)
{
  return (static_cast<float>(px) + kUvInsetPx) / texWidth;
}

float InsetNormV1(int px, float texHeight)
{
  return (static_cast<float>(px) - kUvInsetPx) / texHeight;
}

glm::vec3 MeshCenter(const cutum::CreatureBoneHierarchy &hierarchy,
                     const cutum::CreatureSkeletalMeshAsset &asset,
                     size_t boneIdx, const cutum::SkeletalCreaturePose &pose)
{
  const glm::mat4 boneMat = hierarchy.ComputeBoneMatrix(boneIdx, pose);
  const cutum::SkeletalCubeMeshCpu &cube = asset.boneMeshes[boneIdx].cubes[0];
  return glm::vec3(
      boneMat * cube.restLocalMatrix * glm::vec4(0.f, 0.f, 0.f, 1.f));
}

glm::vec3 ExpectedCenter(const cutum::SkeletalCubeDef &cube)
{
  return cube.originBlocks + cube.sizeBlocks * 0.5f;
}

bool TestHumanoidGeometry(const cutum::CreatureSkeletalGeometry &geometry,
                          const std::string &label)
{
  const cutum::CreatureSkeletalMeshAsset asset =
      cutum::SkeletalCubeMeshBuilder::BuildMeshAsset(geometry);
  cutum::CreatureBoneHierarchy hierarchy(geometry);
  const cutum::SkeletalCreaturePose idlePose;

  const size_t headIdx = geometry.boneIndexByName.at("head");
  const size_t bodyIdx = geometry.boneIndexByName.at("body");
  const size_t rightArmIdx = geometry.boneIndexByName.at("rightArm");
  const size_t rightLegIdx = geometry.boneIndexByName.at("rightLeg");

  const glm::mat4 headMat = hierarchy.ComputeBoneMatrix(headIdx, idlePose);
  if (!NearlyEqual(glm::mat3(headMat)[1][1], 1.f) ||
      !NearlyEqual(glm::mat3(headMat)[2][2], 1.f))
  {
    std::cerr << "FAIL " << label << ": head inherits parent bind rotation\n";
    return false;
  }

  const cutum::SkeletalCubeDef &headCubeDef = geometry.bones[headIdx].cubes[0];
  const glm::vec3 headCenter = MeshCenter(hierarchy, asset, headIdx, idlePose);
  const glm::vec3 headExpected = ExpectedCenter(headCubeDef);
  if (!NearlyEqual(headCenter.x, headExpected.x) ||
      !NearlyEqual(headCenter.y, headExpected.y) ||
      !NearlyEqual(headCenter.z, headExpected.z))
  {
    std::cerr << "FAIL " << label << ": head center expected ("
              << headExpected.x << "," << headExpected.y << ","
              << headExpected.z << ") got (" << headCenter.x << ","
              << headCenter.y << "," << headCenter.z << ")\n";
    return false;
  }

  const glm::vec3 bodyCenter = MeshCenter(hierarchy, asset, bodyIdx, idlePose);
  const glm::vec3 legCenter = MeshCenter(hierarchy, asset, rightLegIdx, idlePose);
  const glm::vec3 armCenter = MeshCenter(hierarchy, asset, rightArmIdx, idlePose);

  if (headCenter.y <= bodyCenter.y + 0.1f)
  {
    std::cerr << "FAIL " << label << ": head should sit above body, headY="
              << headCenter.y << " bodyY=" << bodyCenter.y << "\n";
    return false;
  }
  if (bodyCenter.y <= legCenter.y + 0.1f)
  {
    std::cerr << "FAIL " << label << ": body should sit above legs, bodyY="
              << bodyCenter.y << " legY=" << legCenter.y << "\n";
    return false;
  }
  if (std::fabs(armCenter.x) <= std::fabs(bodyCenter.x) + 0.01f)
  {
    std::cerr << "FAIL " << label << ": arm should extend sideways, armX="
              << armCenter.x << " bodyX=" << bodyCenter.x << "\n";
    return false;
  }

  cutum::SkeletalCreaturePose walkPose;
  constexpr float kBob = 0.05f;
  cutum::SkeletalBonePose waistBob;
  waistBob.offsetBlocks.y = kBob;
  walkPose.bones["waist"] = waistBob;

  const glm::vec3 headWalk = MeshCenter(hierarchy, asset, headIdx, walkPose);
  const glm::vec3 bodyWalk = MeshCenter(hierarchy, asset, bodyIdx, walkPose);
  const glm::vec3 legWalk = MeshCenter(hierarchy, asset, rightLegIdx, walkPose);
  const float headDelta = headWalk.y - headCenter.y;
  const float bodyDelta = bodyWalk.y - bodyCenter.y;
  const float legDelta = legWalk.y - legCenter.y;
  if (!NearlyEqual(headDelta, kBob) || !NearlyEqual(bodyDelta, kBob) ||
      !NearlyEqual(legDelta, kBob))
  {
    std::cerr << "FAIL " << label << ": waist bob should move all parts equally, "
              << "headΔ=" << headDelta << " bodyΔ=" << bodyDelta
              << " legΔ=" << legDelta << " expected " << kBob << "\n";
    return false;
  }

  std::cout << "OK " << label << " humanoid layout\n";
  return true;
}

} // namespace

int main()
{
  const auto geometry = cutum::CreatureSkeletalGeoLoader::LoadFromFile(
      kCowGeo, "geometry.cow.v1.8");
  if (!geometry)
  {
    std::cerr << "FAIL: could not load cow geometry\n";
    return 1;
  }

  if (geometry->identifier != "geometry.cow.v1.8")
  {
    std::cerr << "FAIL: identifier=" << geometry->identifier << "\n";
    return 1;
  }

  if (geometry->bones.size() < 5)
  {
    std::cerr << "FAIL: expected >=5 bones, got " << geometry->bones.size()
              << "\n";
    return 1;
  }

  const auto bodyIt = geometry->boneIndexByName.find("body");
  const auto leg0It = geometry->boneIndexByName.find("leg0");
  if (bodyIt == geometry->boneIndexByName.end() ||
      leg0It == geometry->boneIndexByName.end())
  {
    std::cerr << "FAIL: missing body or leg0 bone\n";
    return 1;
  }

  const cutum::SkeletalBoneDef &body = geometry->bones[bodyIt->second];
  if (!NearlyEqual(body.bindPoseRotationDeg.x, 90.f))
  {
    std::cerr << "FAIL: body bind rotation x=" << body.bindPoseRotationDeg.x
              << "\n";
    return 1;
  }

  const cutum::CreatureSkeletalMeshAsset asset =
      cutum::SkeletalCubeMeshBuilder::BuildMeshAsset(*geometry);
  if (asset.boneMeshes.empty() || asset.boneMeshes[0].cubes.empty())
  {
    std::cerr << "FAIL: mesh asset empty\n";
    return 1;
  }

  const cutum::SkeletalCubeMeshCpu &cube = asset.boneMeshes[0].cubes[0];
  if (cube.interleavedPosUv.size() != 24u * 5u)
  {
    std::cerr << "FAIL: cube vertex layout size="
              << cube.interleavedPosUv.size() << "\n";
    return 1;
  }

  cutum::CreatureBoneHierarchy hierarchy(*geometry);
  cutum::SkeletalCreaturePose pose;
  cutum::SkeletalBonePose legPose;
  legPose.rotationDeg.x = 30.f;
  pose.bones["leg0"] = legPose;
  const glm::mat4 legMat =
      hierarchy.ComputeBoneMatrix(leg0It->second, pose);
  if (NearlyEqual(legMat[0][0], 1.f) && NearlyEqual(legMat[1][1], 1.f) &&
      NearlyEqual(legMat[2][2], 1.f))
  {
    std::cerr << "FAIL: leg bone matrix did not rotate\n";
    return 1;
  }

  cutum::SkeletalCreaturePose idlePose;
  const cutum::SkeletalBoneDef &leg0Bone = geometry->bones[leg0It->second];
  const glm::mat4 leg0Idle =
      hierarchy.ComputeBoneMatrix(leg0It->second, idlePose);
  const glm::mat3 legRot = glm::mat3(leg0Idle);
  if (!NearlyEqual(legRot[1][1], 1.f) || !NearlyEqual(legRot[2][2], 1.f))
  {
    std::cerr << "FAIL: cow leg0 idle should not inherit body bind rotation\n";
    return 1;
  }
  if (leg0Bone.pivotBlocks.y >= body.pivotBlocks.y - 0.05f)
  {
    std::cerr << "FAIL: cow leg0 pivot should sit below body pivot\n";
    return 1;
  }

  const auto pigGeometry = cutum::CreatureSkeletalGeoLoader::LoadFromFile(
      "models/creatures/pig/geometry.geo.json", "geometry.pig.v1.8");
  if (!pigGeometry)
  {
    std::cerr << "FAIL: could not load pig geometry\n";
    return 1;
  }
  cutum::CreatureBoneHierarchy pigHierarchy(*pigGeometry);
  const size_t pigBodyIdx = pigGeometry->boneIndexByName.at("body");
  const size_t pigHeadIdx = pigGeometry->boneIndexByName.at("head");
  const glm::mat4 pigBodyMat =
      pigHierarchy.ComputeBoneMatrix(pigBodyIdx, idlePose);
  const glm::mat4 pigHeadMat =
      pigHierarchy.ComputeBoneMatrix(pigHeadIdx, idlePose);
  if (NearlyEqual(glm::mat3(pigBodyMat)[1][1], 1.f))
  {
    std::cerr << "FAIL: pig body missing bind_pose_rotation\n";
    return 1;
  }
  if (!NearlyEqual(glm::mat3(pigHeadMat)[1][1], 1.f) ||
      !NearlyEqual(glm::mat3(pigHeadMat)[2][2], 1.f))
  {
    std::cerr << "FAIL: pig head inherits body bind rotation\n";
    return 1;
  }

  const cutum::CreatureSkeletalMeshAsset pigAsset =
      cutum::SkeletalCubeMeshBuilder::BuildMeshAsset(*pigGeometry);
  const cutum::SkeletalCubeMeshCpu &pigHeadCube =
      pigAsset.boneMeshes[pigHeadIdx].cubes[0];
  const glm::vec3 pigHeadCenter = glm::vec3(
      pigHeadMat * pigHeadCube.restLocalMatrix * glm::vec4(0.f, 0.f, 0.f, 1.f));
  const cutum::SkeletalCubeDef &pigHeadCubeDef =
      pigGeometry->bones[pigHeadIdx].cubes[0];
  const glm::vec3 pigHeadExpected =
      (pigHeadCubeDef.originBlocks + pigHeadCubeDef.sizeBlocks * 0.5f);
  if (!NearlyEqual(pigHeadCenter.x, pigHeadExpected.x) ||
      !NearlyEqual(pigHeadCenter.y, pigHeadExpected.y) ||
      !NearlyEqual(pigHeadCenter.z, pigHeadExpected.z))
  {
    std::cerr << "FAIL: pig head mesh center expected (" << pigHeadExpected.x
              << "," << pigHeadExpected.y << "," << pigHeadExpected.z
              << ") got (" << pigHeadCenter.x << "," << pigHeadCenter.y << ","
              << pigHeadCenter.z << ")\n";
    return 1;
  }

  const cutum::SkeletalCubeMeshCpu &pigBodyCube =
      pigAsset.boneMeshes[pigBodyIdx].cubes[0];
  const glm::vec3 pigBodyCenter = glm::vec3(
      pigBodyMat * pigBodyCube.restLocalMatrix * glm::vec4(0.f, 0.f, 0.f, 1.f));
  const cutum::SkeletalCubeDef &pigBodyCubeDef =
      pigGeometry->bones[pigBodyIdx].cubes[0];
  const glm::vec3 pigBodyOrigin = pigBodyCubeDef.originBlocks;
  const glm::vec3 pigBodySize = pigBodyCubeDef.sizeBlocks;
  const glm::vec3 pigBodyPivot = pigGeometry->bones[pigBodyIdx].pivotBlocks;
  const glm::vec3 pigBodyMeshOffset =
      (pigBodyOrigin + pigBodySize * 0.5f) - pigBodyPivot;
  const glm::mat4 pigBodyRot =
      glm::translate(glm::mat4(1.f), pigBodyPivot) *
      cutum::SkeletalEulerDegToMat(cutum::SkeletalBindPoseRotationDeg(
          pigGeometry->bones[pigBodyIdx].bindPoseRotationDeg));
  const glm::vec3 pigBodyExpected =
      glm::vec3(pigBodyRot * glm::vec4(pigBodyMeshOffset, 1.f));
  if (!NearlyEqual(pigBodyCenter.x, pigBodyExpected.x) ||
      !NearlyEqual(pigBodyCenter.y, pigBodyExpected.y) ||
      !NearlyEqual(pigBodyCenter.z, pigBodyExpected.z))
  {
    std::cerr << "FAIL: pig body mesh center expected (" << pigBodyExpected.x
              << "," << pigBodyExpected.y << "," << pigBodyExpected.z
              << ") got (" << pigBodyCenter.x << "," << pigBodyCenter.y << ","
              << pigBodyCenter.z << ")\n";
    return 1;
  }

  if (pigBodyCenter.y > pigHeadExpected.y + 0.15f)
  {
    std::cerr << "FAIL: pig body center should sit near head/legs, bodyY="
              << pigBodyCenter.y << " headY=" << pigHeadExpected.y << "\n";
    return 1;
  }

  std::cout << "OK creature_skeletal_geo_loader_test: bones="
            << geometry->bones.size() << " cubes="
            << asset.boneMeshes[0].cubes.size() << "\n";

  const auto beeGeometry = cutum::CreatureSkeletalGeoLoader::LoadFromFile(
      "models/creatures/bee/geometry.geo.json", "geometry.bee");
  if (!beeGeometry)
  {
    std::cerr << "FAIL: could not load bee geometry\n";
    return 1;
  }
  const cutum::CreatureSkeletalMeshAsset beeAsset =
      cutum::SkeletalCubeMeshBuilder::BuildMeshAsset(*beeGeometry);
  size_t beeCubeCount = 0;
  size_t beeRenderedFaces = 0;
  for (const cutum::SkeletalBoneMeshCpu &boneMesh : beeAsset.boneMeshes)
  {
    beeCubeCount += boneMesh.cubes.size();
    for (const cutum::SkeletalCubeMeshCpu &cube : boneMesh.cubes)
    {
      if (!cube.interleavedPosUv.empty())
      {
        beeRenderedFaces += cube.indices.size() / 6;
      }
    }
  }
  if (beeGeometry->bones.size() != 7 || beeCubeCount != 9)
  {
    std::cerr << "FAIL: bee bones/cubes=" << beeGeometry->bones.size() << "/"
              << beeCubeCount << "\n";
    return 1;
  }
  if (beeRenderedFaces < 9)
  {
    std::cerr << "FAIL: bee rendered face groups=" << beeRenderedFaces << "\n";
    return 1;
  }

  const cutum::SkeletalBoneMeshCpu &beeBody =
      beeAsset.boneMeshes[beeGeometry->boneIndexByName.at("body")];
  const cutum::SkeletalCubeMeshCpu &beeTorso = beeBody.cubes[0];
  // Face 1 (+X east), first vertex (BL): u0 / v1 with half-texel inset.
  const float eastU = beeTorso.interleavedPosUv[4u * 5u + 3u];
  const float eastV = beeTorso.interleavedPosUv[4u * 5u + 4u];
  if (!NearlyEqual(eastU, InsetNormU0(17, 64.f)) ||
      !NearlyEqual(eastV, InsetNormV1(17, 64.f)))
  {
    std::cerr << "FAIL: bee east UV=(" << eastU << "," << eastV
              << ") expected inset east BL (17,17)/64\n";
    return 1;
  }
  const float southU = beeTorso.interleavedPosUv[3u];
  if (!NearlyEqual(southU, InsetNormU0(27, 64.f)))
  {
    std::cerr << "FAIL: bee south U=" << southU << " expected inset 27/64\n";
    return 1;
  }

  std::cout << "OK bee: bones=7 cubes=9 rendered_face_groups="
            << beeRenderedFaces << "\n";

  const auto humanGeometry = cutum::CreatureSkeletalGeoLoader::LoadFromFile(
      "models/creatures/human/geometry.geo.json", "geometry.zombie.v1.8");
  if (!humanGeometry)
  {
    std::cerr << "FAIL: could not load human geometry\n";
    return 1;
  }
  if (!TestHumanoidGeometry(*humanGeometry, "human"))
  {
    return 1;
  }

  const auto zombieGeometry = cutum::CreatureSkeletalGeoLoader::LoadFromFile(
      "models/creatures/zombie/geometry.geo.json", "geometry.zombie.v1.8");
  if (!zombieGeometry)
  {
    std::cerr << "FAIL: could not load zombie geometry\n";
    return 1;
  }
  if (!TestHumanoidGeometry(*zombieGeometry, "zombie"))
  {
    return 1;
  }

  const auto bunnyGeometry = cutum::CreatureSkeletalGeoLoader::LoadFromFile(
      "models/creatures/bunny/geometry.geo.json", "geometry.rabbit.v1.8");
  if (!bunnyGeometry)
  {
    std::cerr << "FAIL: could not load bunny geometry\n";
    return 1;
  }
  const cutum::CreatureSkeletalMeshAsset bunnyAsset =
      cutum::SkeletalCubeMeshBuilder::BuildMeshAsset(*bunnyGeometry);
  const cutum::SkeletalBoneMeshCpu &bunnyBody =
      bunnyAsset.boneMeshes[bunnyGeometry->boneIndexByName.at("body")];
  const cutum::SkeletalCubeMeshCpu &bunnyTorso = bunnyBody.cubes[0];
  const float bunnyEastU = bunnyTorso.interleavedPosUv[4u * 5u + 3u];
  const float bunnyEastV = bunnyTorso.interleavedPosUv[4u * 5u + 4u];
  if (!NearlyEqual(bunnyEastU, (25.5f / 64.f)) ||
      !NearlyEqual(bunnyEastV, (14.5f / 32.f)))
  {
    std::cerr << "FAIL: bunny mirrored east UV=(" << bunnyEastU << ","
              << bunnyEastV << ") expected ~(25.5/64,14.5/32)\n";
    return 1;
  }

  const auto foxGeometry = cutum::CreatureSkeletalGeoLoader::LoadFromFile(
      "models/creatures/fox/geometry.geo.json", "geometry.fox");
  if (!foxGeometry)
  {
    std::cerr << "FAIL: could not load fox geometry\n";
    return 1;
  }
  const cutum::CreatureSkeletalMeshAsset foxAsset =
      cutum::SkeletalCubeMeshBuilder::BuildMeshAsset(*foxGeometry);
  for (const cutum::SkeletalBoneMeshCpu &boneMesh : foxAsset.boneMeshes)
  {
    for (const cutum::SkeletalCubeMeshCpu &cube : boneMesh.cubes)
    {
      for (size_t i = 3; i + 1 < cube.interleavedPosUv.size(); i += 5)
      {
        const float u = cube.interleavedPosUv[i];
        const float v = cube.interleavedPosUv[i + 1];
        if (u < -1e-4f || u > 1.0001f || v < -1e-4f || v > 1.0001f)
        {
          std::cerr << "FAIL: fox UV out of range u=" << u << " v=" << v
                    << "\n";
          return 1;
        }
      }
    }
  }
  if (foxGeometry->boneIndexByName.find("tail") == foxGeometry->boneIndexByName.end() ||
      foxGeometry->boneIndexByName.find("leg0") == foxGeometry->boneIndexByName.end() ||
      foxGeometry->boneIndexByName.find("leg3") == foxGeometry->boneIndexByName.end())
  {
    std::cerr << "FAIL: fox expected tail/leg bones missing\n";
    return 1;
  }

  return 0;
}

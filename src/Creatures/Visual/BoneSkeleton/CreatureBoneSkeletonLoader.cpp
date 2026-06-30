#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonLoader.h"

#include "Creatures/Visual/BoneSkeleton/BoneSkeletonModelSpace.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{

constexpr float kBoneSkeletonUnitsPerBlock = 16.f;

glm::vec3 ReadSkeletalVec3Blocks(const nlohmann::json &arr, const glm::vec3 &fb)
{
  if (!arr.is_array() || arr.size() < 3)
  {
    return fb;
  }
  return BoneSkeletonUnitsToBlocks(glm::vec3(arr[0].get<float>(), arr[1].get<float>(),
                                        arr[2].get<float>()));
}

glm::ivec2 ReadUvPixels(const nlohmann::json &arr)
{
  if (!arr.is_array() || arr.size() < 2)
  {
    return {0, 0};
  }
  return {static_cast<int>(std::lround(arr[0].get<float>())),
          static_cast<int>(std::lround(arr[1].get<float>()))};
}

BoneSkeletonCubeDef ReadCube(const nlohmann::json &cubeJson, bool boneMirror)
{
  BoneSkeletonCubeDef cube;
  cube.originBlocks =
      ReadSkeletalVec3Blocks(cubeJson.value("origin", nlohmann::json::array()),
                            cube.originBlocks);
  if (cubeJson.contains("size") && cubeJson["size"].is_array() &&
      cubeJson["size"].size() >= 3)
  {
    const auto &sz = cubeJson["size"];
    cube.sizeBlocks = BoneSkeletonUnitsToBlocks(
        glm::vec3(std::abs(sz[0].get<float>()), std::abs(sz[1].get<float>()),
                  std::abs(sz[2].get<float>())));
  }
  if (cubeJson.contains("pivot"))
  {
    cube.pivotBlocks =
        ReadSkeletalVec3Blocks(cubeJson.value("pivot", nlohmann::json::array()),
                              cube.pivotBlocks);
    cube.hasPivot = true;
  }
  cube.uvPixels = ReadUvPixels(cubeJson.value("uv", nlohmann::json::array()));
  if (cubeJson.contains("rotation"))
  {
    const auto &rot = cubeJson["rotation"];
    if (rot.is_array() && rot.size() >= 3)
    {
      cube.rotationDeg = glm::vec3(rot[0].get<float>(), rot[1].get<float>(),
                                   rot[2].get<float>());
    }
  }
  cube.inflateBlocks =
      cubeJson.value("inflate", 0.f) / kBoneSkeletonUnitsPerBlock;
  cube.mirror = cubeJson.value("mirror", boneMirror);
  return cube;
}

BoneSkeletonBoneDef ReadBone(const nlohmann::json &boneJson)
{
  BoneSkeletonBoneDef bone;
  bone.name = boneJson.value("name", "");
  bone.parent = boneJson.value("parent", "");
  bone.pivotBlocks =
      ReadSkeletalVec3Blocks(boneJson.value("pivot", nlohmann::json::array()),
                            bone.pivotBlocks);
  if (boneJson.contains("bind_pose_rotation"))
  {
    const auto &rot = boneJson["bind_pose_rotation"];
    if (rot.is_array() && rot.size() >= 3)
    {
      bone.bindPoseRotationDeg =
          glm::vec3(rot[0].get<float>(), rot[1].get<float>(), rot[2].get<float>());
    }
  }
  bone.mirror = boneJson.value("mirror", false);
  if (boneJson.value("neverRender", false))
  {
    return bone;
  }
  if (boneJson.contains("rotation"))
  {
    const auto &rot = boneJson["rotation"];
    if (rot.is_array() && rot.size() >= 3)
    {
      bone.boneRotationDeg +=
          glm::vec3(rot[0].get<float>(), rot[1].get<float>(), rot[2].get<float>());
    }
  }
  if (boneJson.contains("cubes") && boneJson["cubes"].is_array())
  {
    for (const auto &cubeJson : boneJson["cubes"])
    {
      if (cubeJson.is_object())
      {
        bone.cubes.push_back(ReadCube(cubeJson, bone.mirror));
      }
    }
  }
  return bone;
}

void FinalizeBoneIndices(CreatureBoneSkeletonGeometry &geometry)
{
  geometry.boneIndexByName.clear();
  for (size_t i = 0; i < geometry.bones.size(); ++i)
  {
    geometry.boneIndexByName[geometry.bones[i].name] = i;
  }
}

bool ParseGeometryObject(const nlohmann::json &geoObj,
                         CreatureBoneSkeletonGeometry &out)
{
  if (!geoObj.is_object())
  {
    return false;
  }
  out.textureSize.x = geoObj.value("texturewidth",
                                   geoObj.value("texture_width", 64));
  out.textureSize.y = geoObj.value("textureheight",
                                   geoObj.value("texture_height", 32));
  if (geoObj.contains("visible_bounds_offset"))
  {
    const auto &off = geoObj["visible_bounds_offset"];
    if (off.is_array() && off.size() >= 3)
    {
      out.visibleBoundsOffsetBlocks = BoneSkeletonVisibleBoundsOffsetBlocks(
          glm::vec3(off[0].get<float>(), off[1].get<float>(), off[2].get<float>()));
    }
  }
  out.visibleBoundsWidthBlocks =
      geoObj.value("visible_bounds_width", out.visibleBoundsWidthBlocks);
  out.visibleBoundsHeightBlocks =
      geoObj.value("visible_bounds_height", out.visibleBoundsHeightBlocks);

  if (!geoObj.contains("bones") || !geoObj["bones"].is_array())
  {
    return false;
  }
  out.bones.clear();
  for (const auto &boneJson : geoObj["bones"])
  {
    if (!boneJson.is_object())
    {
      continue;
    }
    BoneSkeletonBoneDef bone = ReadBone(boneJson);
    if (!bone.name.empty())
    {
      out.bones.push_back(std::move(bone));
    }
  }
  FinalizeBoneIndices(out);
  return !out.bones.empty();
}

} // namespace

std::optional<CreatureBoneSkeletonGeometry>
CreatureBoneSkeletonLoader::LoadFromFile(const std::string &path,
                                       const std::string &expectedId)
{
  try
  {
    std::ifstream file(path);
    if (!file.is_open())
    {
      std::cerr << "CreatureBoneSkeletonLoader: cannot open " << path << std::endl;
      return std::nullopt;
    }
    nlohmann::json root;
    file >> root;

    CreatureBoneSkeletonGeometry geometry;

    if (root.contains("minecraft:geometry") && root["minecraft:geometry"].is_array())
    {
      for (const auto &entry : root["minecraft:geometry"])
      {
        if (!entry.is_object())
        {
          continue;
        }
        std::string id;
        if (entry.contains("description") && entry["description"].is_object())
        {
          id = entry["description"].value("identifier", "");
          geometry.textureSize.x = static_cast<int>(std::lround(
              entry["description"].value("texture_width", geometry.textureSize.x)));
          geometry.textureSize.y = static_cast<int>(std::lround(
              entry["description"].value("texture_height", geometry.textureSize.y)));
          if (entry["description"].contains("visible_bounds_offset"))
          {
            const auto &off = entry["description"]["visible_bounds_offset"];
            if (off.is_array() && off.size() >= 3)
            {
              geometry.visibleBoundsOffsetBlocks = BoneSkeletonVisibleBoundsOffsetBlocks(
                  glm::vec3(off[0].get<float>(), off[1].get<float>(),
                            off[2].get<float>()));
            }
          }
          geometry.visibleBoundsWidthBlocks =
              entry["description"].value("visible_bounds_width",
                                       geometry.visibleBoundsWidthBlocks);
          geometry.visibleBoundsHeightBlocks =
              entry["description"].value("visible_bounds_height",
                                       geometry.visibleBoundsHeightBlocks);
        }
        if (!expectedId.empty() && !id.empty() && id != expectedId)
        {
          continue;
        }
        geometry.identifier = id;
        CreatureBoneSkeletonGeometry parsed;
        parsed.identifier = id;
        parsed.textureSize = geometry.textureSize;
        parsed.visibleBoundsOffsetBlocks = geometry.visibleBoundsOffsetBlocks;
        parsed.visibleBoundsWidthBlocks = geometry.visibleBoundsWidthBlocks;
        parsed.visibleBoundsHeightBlocks = geometry.visibleBoundsHeightBlocks;
        if (entry.contains("bones") && entry["bones"].is_array())
        {
          for (const auto &boneJson : entry["bones"])
          {
            if (boneJson.is_object())
            {
              BoneSkeletonBoneDef bone = ReadBone(boneJson);
              if (!bone.name.empty())
              {
                parsed.bones.push_back(std::move(bone));
              }
            }
          }
          FinalizeBoneIndices(parsed);
          if (!parsed.bones.empty())
          {
            return parsed;
          }
        }
      }
    }

    for (const auto &[key, value] : root.items())
    {
      if (key.rfind("geometry.", 0) != 0 || !value.is_object())
      {
        continue;
      }
      if (!expectedId.empty() && key != expectedId)
      {
        continue;
      }
      CreatureBoneSkeletonGeometry parsed;
      parsed.identifier = key;
      if (ParseGeometryObject(value, parsed))
      {
        parsed.identifier = key;
        return parsed;
      }
    }

    std::cerr << "CreatureBoneSkeletonLoader: no geometry in " << path << std::endl;
    return std::nullopt;
  }
  catch (const std::exception &e)
  {
    std::cerr << "CreatureBoneSkeletonLoader: " << path << ": " << e.what()
              << std::endl;
    return std::nullopt;
  }
}

} // namespace cutum

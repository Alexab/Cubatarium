#include "Creatures/Visual/Gltf/CreatureGltfLoader.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <glm/gtc/quaternion.hpp>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{

std::vector<uint8_t> ReadFileBytes(const std::string &path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in)
  {
    return {};
  }
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
}

std::vector<std::vector<uint8_t>> LoadGltfBuffers(
    const nlohmann::json &gltf, const std::filesystem::path &baseDir)
{
  std::vector<std::vector<uint8_t>> bins;
  if (!gltf.contains("buffers") || !gltf["buffers"].is_array())
  {
    return bins;
  }
  bins.resize(gltf["buffers"].size());
  for (size_t i = 0; i < bins.size(); ++i)
  {
    const std::string uri = gltf["buffers"][i].value("uri", "");
    if (uri.empty() || uri.rfind("data:", 0) == 0)
    {
      continue;
    }
    bins[i] = ReadFileBytes((baseDir / uri).string());
  }
  return bins;
}

const uint8_t *AccessorPtr(const nlohmann::json &gltf,
                           const std::vector<std::vector<uint8_t>> &bins,
                           int accessorIndex, size_t &byteStride,
                           int &componentType, std::string &type, size_t &count)
{
  byteStride = 0;
  componentType = 0;
  type.clear();
  count = 0;
  if (accessorIndex < 0 ||
      accessorIndex >= static_cast<int>(gltf["accessors"].size()))
  {
    return nullptr;
  }
  const auto &acc = gltf["accessors"][accessorIndex];
  componentType = acc.value("componentType", 0);
  type = acc.value("type", "");
  count = acc.value("count", 0);
  const int bvIndex = acc.value("bufferView", -1);
  if (bvIndex < 0 || bvIndex >= static_cast<int>(gltf["bufferViews"].size()))
  {
    return nullptr;
  }
  const auto &bv = gltf["bufferViews"][bvIndex];
  const int bufferIndex = bv.value("buffer", 0);
  if (bufferIndex < 0 || bufferIndex >= static_cast<int>(bins.size()))
  {
    return nullptr;
  }
  const std::vector<uint8_t> &bin = bins[static_cast<size_t>(bufferIndex)];
  const size_t offset = bv.value("byteOffset", 0) + acc.value("byteOffset", 0);
  byteStride = bv.value("byteStride", 0);
  if (offset >= bin.size())
  {
    return nullptr;
  }
  return bin.data() + offset;
}

size_t ComponentSize(int componentType)
{
  switch (componentType)
  {
  case 5126:
    return 4; // FLOAT
  case 5123:
    return 2; // UNSIGNED_SHORT
  case 5121:
    return 1; // UNSIGNED_BYTE
  case 5125:
    return 4; // UNSIGNED_INT
  default:
    return 0;
  }
}

size_t TypeComponents(const std::string &type)
{
  if (type == "SCALAR")
  {
    return 1;
  }
  if (type == "VEC2")
  {
    return 2;
  }
  if (type == "VEC3")
  {
    return 3;
  }
  if (type == "VEC4")
  {
    return 4;
  }
  if (type == "MAT4")
  {
    return 16;
  }
  return 0;
}

std::vector<float> ReadFloatAccessor(const nlohmann::json &gltf,
                                     const std::vector<std::vector<uint8_t>> &bins,
                                     int accessorIndex)
{
  size_t stride = 0;
  int componentType = 0;
  std::string type;
  size_t count = 0;
  const uint8_t *ptr =
      AccessorPtr(gltf, bins, accessorIndex, stride, componentType, type, count);
  if (!ptr || componentType != 5126)
  {
    return {};
  }
  const size_t comps = TypeComponents(type);
  if (comps == 0)
  {
    return {};
  }
  std::vector<float> out;
  out.reserve(count * comps);
  for (size_t i = 0; i < count; ++i)
  {
    const uint8_t *row =
        ptr + (stride ? stride * i : i * comps * sizeof(float));
    for (size_t c = 0; c < comps; ++c)
    {
      float v;
      std::memcpy(&v, row + c * sizeof(float), sizeof(float));
      out.push_back(v);
    }
  }
  return out;
}

std::vector<unsigned int>
ReadIndexAccessor(const nlohmann::json &gltf, const std::vector<std::vector<uint8_t>> &bins,
                  int accessorIndex)
{
  size_t stride = 0;
  int componentType = 0;
  std::string type;
  size_t count = 0;
  const uint8_t *ptr =
      AccessorPtr(gltf, bins, accessorIndex, stride, componentType, type, count);
  if (!ptr || type != "SCALAR")
  {
    return {};
  }
  std::vector<unsigned int> out;
  out.reserve(count);
  const size_t cs = ComponentSize(componentType);
  for (size_t i = 0; i < count; ++i)
  {
    const uint8_t *row = ptr + (stride ? stride * i : i * cs);
    if (componentType == 5123)
    {
      uint16_t v;
      std::memcpy(&v, row, sizeof(v));
      out.push_back(v);
    }
    else if (componentType == 5125)
    {
      uint32_t v;
      std::memcpy(&v, row, sizeof(v));
      out.push_back(v);
    }
    else if (componentType == 5126)
    {
      float v;
      std::memcpy(&v, row, sizeof(v));
      out.push_back(static_cast<unsigned int>(v));
    }
  }
  return out;
}

void BuildInterleavedMesh(BoneSkeletonCubeMeshCpu &mesh,
                          const std::vector<float> &positions,
                          const std::vector<float> &uvs)
{
  const size_t vertCount = positions.size() / 3;
  mesh.interleavedPosUv.clear();
  mesh.interleavedPosUv.reserve(vertCount * 5);
  for (size_t i = 0; i < vertCount; ++i)
  {
    mesh.interleavedPosUv.push_back(positions[i * 3 + 0]);
    mesh.interleavedPosUv.push_back(positions[i * 3 + 1]);
    mesh.interleavedPosUv.push_back(positions[i * 3 + 2]);
    const float u = (i * 2 + 0 < uvs.size()) ? uvs[i * 2 + 0] : 0.f;
    const float v = (i * 2 + 1 < uvs.size()) ? uvs[i * 2 + 1] : 0.f;
    mesh.interleavedPosUv.push_back(u);
    mesh.interleavedPosUv.push_back(v);
  }
}

std::string MaterialTextureStem(const nlohmann::json &gltf, int materialIndex)
{
  if (materialIndex < 0 ||
      materialIndex >= static_cast<int>(gltf["materials"].size()))
  {
    return "body";
  }
  const auto &mat = gltf["materials"][materialIndex];
  if (mat.contains("name"))
  {
    return mat["name"].get<std::string>();
  }
  return "body";
}

std::vector<uint8_t> ReadU8Accessor(const nlohmann::json &gltf,
                                    const std::vector<std::vector<uint8_t>> &bins,
                                    int accessorIndex)
{
  size_t stride = 0;
  int componentType = 0;
  std::string type;
  size_t count = 0;
  const uint8_t *ptr =
      AccessorPtr(gltf, bins, accessorIndex, stride, componentType, type, count);
  if (!ptr || componentType != 5121)
  {
    return {};
  }
  const size_t comps = TypeComponents(type);
  if (comps == 0)
  {
    return {};
  }
  std::vector<uint8_t> out;
  out.reserve(count * comps);
  for (size_t i = 0; i < count; ++i)
  {
    const uint8_t *row = ptr + (stride ? stride * i : i * comps);
    for (size_t c = 0; c < comps; ++c)
    {
      out.push_back(row[c]);
    }
  }
  return out;
}

std::vector<glm::mat4> ReadMat4Accessor(const nlohmann::json &gltf,
                                        const std::vector<std::vector<uint8_t>> &bins,
                                        int accessorIndex)
{
  const std::vector<float> floats = ReadFloatAccessor(gltf, bins, accessorIndex);
  std::vector<glm::mat4> mats;
  for (size_t i = 0; i + 15 < floats.size(); i += 16)
  {
    glm::mat4 m(1.f);
    for (int col = 0; col < 4; ++col)
    {
      for (int row = 0; row < 4; ++row)
      {
        m[col][row] = floats[i + col * 4 + row];
      }
    }
    mats.push_back(m);
  }
  return mats;
}

void ParseNodes(const nlohmann::json &gltf, CreatureGltfMeshAsset &asset)
{
  if (!gltf.contains("nodes") || !gltf["nodes"].is_array())
  {
    return;
  }
  asset.nodes.clear();
  asset.nodes.resize(gltf["nodes"].size());
  for (size_t i = 0; i < asset.nodes.size(); ++i)
  {
    const auto &nodeJson = gltf["nodes"][i];
    GltfNodeCpu &node = asset.nodes[i];
    node.name = nodeJson.value("name", "");
    if (nodeJson.contains("translation") && nodeJson["translation"].is_array())
    {
      const auto &t = nodeJson["translation"];
      node.translation =
          glm::vec3(t[0].get<float>(), t[1].get<float>(), t[2].get<float>());
    }
    if (nodeJson.contains("rotation") && nodeJson["rotation"].is_array())
    {
      const auto &r = nodeJson["rotation"];
      node.rotation = glm::quat(r[3].get<float>(), r[0].get<float>(),
                                r[1].get<float>(), r[2].get<float>());
    }
    if (nodeJson.contains("scale") && nodeJson["scale"].is_array())
    {
      const auto &s = nodeJson["scale"];
      node.scale = glm::vec3(s[0].get<float>(), s[1].get<float>(),
                             s[2].get<float>());
    }
    if (nodeJson.contains("children") && nodeJson["children"].is_array())
    {
      for (const auto &child : nodeJson["children"])
      {
        const int childIndex = child.get<int>();
        if (childIndex >= 0 &&
            childIndex < static_cast<int>(asset.nodes.size()))
        {
          asset.nodes[static_cast<size_t>(childIndex)].parent =
              static_cast<int>(i);
        }
      }
    }
  }
}

void ParseSkin(const nlohmann::json &gltf,
               const std::vector<std::vector<uint8_t>> &bins,
               CreatureGltfMeshAsset &asset)
{
  if (!gltf.contains("skins") || gltf["skins"].empty())
  {
    return;
  }
  const auto &skinJson = gltf["skins"][0];
  if (skinJson.contains("joints") && skinJson["joints"].is_array())
  {
    for (const auto &joint : skinJson["joints"])
    {
      asset.skin.jointNodes.push_back(joint.get<int>());
    }
  }
  const int ibmAcc = skinJson.value("inverseBindMatrices", -1);
  asset.skin.inverseBindMatrices = ReadMat4Accessor(gltf, bins, ibmAcc);
  asset.hasSkin = !asset.skin.jointNodes.empty();
}

GltfAnimationChannelCpu
ReadAnimationChannel(const nlohmann::json &gltf,
                     const std::vector<std::vector<uint8_t>> &bins,
                     const nlohmann::json &channelJson,
                     const nlohmann::json &samplerJson)
{
  GltfAnimationChannelCpu ch;
  if (channelJson.contains("target") && channelJson["target"].is_object())
  {
    const auto &tgt = channelJson["target"];
    ch.nodeIndex = tgt.value("node", 0);
    ch.path = tgt.value("path", "");
  }
  const int inputAcc = samplerJson.value("input", -1);
  const int outputAcc = samplerJson.value("output", -1);
  ch.keyTimes = ReadFloatAccessor(gltf, bins, inputAcc);
  const std::vector<float> outFloats = ReadFloatAccessor(gltf, bins, outputAcc);
  if (ch.path == "translation" || ch.path == "scale")
  {
    for (size_t i = 0; i + 2 < outFloats.size(); i += 3)
    {
      ch.keyVec3.emplace_back(outFloats[i], outFloats[i + 1], outFloats[i + 2]);
    }
  }
  else if (ch.path == "rotation")
  {
    for (size_t i = 0; i + 3 < outFloats.size(); i += 4)
    {
      ch.keyQuat.emplace_back(outFloats[i + 3], outFloats[i], outFloats[i + 1],
                              outFloats[i + 2]);
    }
  }
  return ch;
}

} // namespace

std::shared_ptr<CreatureGltfMeshAsset>
CreatureGltfLoader::LoadFromFile(const std::string &gltfPath)
{
  std::ifstream in(gltfPath);
  if (!in)
  {
    std::cerr << "CreatureGltfLoader: cannot open " << gltfPath << std::endl;
    return nullptr;
  }
  nlohmann::json gltf;
  try
  {
    in >> gltf;
  }
  catch (const std::exception &ex)
  {
    std::cerr << "CreatureGltfLoader: JSON parse error " << gltfPath << ": "
              << ex.what() << std::endl;
    return nullptr;
  }

  const std::filesystem::path baseDir =
      std::filesystem::path(gltfPath).parent_path();
  const std::vector<std::vector<uint8_t>> bins = LoadGltfBuffers(gltf, baseDir);

  auto asset = std::make_shared<CreatureGltfMeshAsset>();
  ParseNodes(gltf, *asset);
  ParseSkin(gltf, bins, *asset);
  if (gltf.contains("nodes") && gltf["nodes"].is_array() && !gltf["nodes"].empty())
  {
    asset->rootNodeIndex = 0;
  }

  if (gltf.contains("meshes") && gltf["meshes"].is_array())
  {
    for (const auto &meshJson : gltf["meshes"])
    {
      if (!meshJson.contains("primitives") || !meshJson["primitives"].is_array())
      {
        continue;
      }
      for (const auto &primJson : meshJson["primitives"])
      {
        GltfPrimitiveCpu prim;
        const int posAcc =
            primJson["attributes"].value("POSITION", -1);
        const int uvAcc =
            primJson["attributes"].value("TEXCOORD_0", -1);
        const int jointsAcc = primJson["attributes"].value("JOINTS_0", -1);
        const int weightsAcc = primJson["attributes"].value("WEIGHTS_0", -1);
        const std::vector<float> positions =
            ReadFloatAccessor(gltf, bins, posAcc);
        const std::vector<float> uvs = ReadFloatAccessor(gltf, bins, uvAcc);
        const int idxAcc = primJson.value("indices", -1);
        BuildInterleavedMesh(prim.mesh, positions, uvs);
        prim.mesh.indices = ReadIndexAccessor(gltf, bins, idxAcc);
        prim.jointIndices = ReadU8Accessor(gltf, bins, jointsAcc);
        prim.jointWeights = ReadFloatAccessor(gltf, bins, weightsAcc);
        prim.skinned = !prim.jointIndices.empty() && !prim.jointWeights.empty() &&
                       asset->hasSkin;
        prim.textureStem =
            MaterialTextureStem(gltf, primJson.value("material", -1));
        if (!prim.mesh.interleavedPosUv.empty() && !prim.mesh.indices.empty())
        {
          asset->primitives.push_back(std::move(prim));
        }
      }
    }
  }

  if (gltf.contains("animations") && gltf["animations"].is_array())
  {
    for (const auto &animJson : gltf["animations"])
    {
      GltfAnimationCpu anim;
      anim.name = animJson.value("name", "");
      if (animJson.contains("channels") && animJson.contains("samplers"))
      {
        const auto &channels = animJson["channels"];
        const auto &samplers = animJson["samplers"];
        for (const auto &chJson : channels)
        {
          const int samplerIndex = chJson.value("sampler", -1);
          if (samplerIndex >= 0 &&
              samplerIndex < static_cast<int>(samplers.size()))
          {
            anim.channels.push_back(ReadAnimationChannel(
                gltf, bins, chJson, samplers[samplerIndex]));
          }
        }
      }
      if (!anim.name.empty())
      {
        asset->animationIndexByName[anim.name] = asset->animations.size();
      }
      asset->animations.push_back(std::move(anim));
    }
  }

  asset->loaded = !asset->primitives.empty();
  if (!asset->loaded)
  {
    std::cerr << "CreatureGltfLoader: no primitives in " << gltfPath << std::endl;
    return nullptr;
  }

  float bindMinY = 1e9f;
  for (const GltfPrimitiveCpu &prim : asset->primitives)
  {
    const BoneSkeletonCubeMeshCpu &mesh = prim.mesh;
    for (size_t i = 0; i + 2 < mesh.interleavedPosUv.size(); i += 5)
    {
      bindMinY = std::min(bindMinY, mesh.interleavedPosUv[i + 1]);
    }
  }
  if (bindMinY < 1e8f)
  {
    asset->bindMinY = bindMinY;
  }

  return asset;
}

} // namespace cutum

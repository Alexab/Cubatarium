#include "Creatures/Visual/Gltf/CreatureGltfLoader.h"
#include "Creatures/Visual/Gltf/GltfConservativeRenderRadius.h"
#include "Core/Memory/VisualLoaderSource.h"

#include "Creatures/Visual/CreatureMeshGpuHash.h"

#include <algorithm>
#include <cmath>
#include <limits>
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

std::vector<std::vector<uint8_t>> LoadGltfBuffers(
    const nlohmann::json &gltf, const std::filesystem::path &baseDir,
    VisualLoaderSource &source)
{
  if (!gltf.contains("buffers") || !gltf["buffers"].is_array())
    return {};

  std::vector<std::vector<uint8_t>> bins(gltf["buffers"].size());
  source.BinaryTable(bins);
  for (size_t i = 0; i < bins.size(); ++i)
  {
    const std::string uri = gltf["buffers"][i].value("uri", "");
    if (uri.empty() || uri.rfind("data:", 0) == 0)
      continue;
    if (auto bytes = ReadVisualModelBuffer(baseDir / uri, source))
      bins[i] = std::move(*bytes);
  }
  return bins;
}

size_t ComponentSize(size_t componentType)
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

struct AccessorSpan
{
  const uint8_t *data{};
  size_t stride{}, count{}, components{}, componentType{};
};

std::optional<size_t> ReadExtent(const nlohmann::json &object, const char *key,
                                 size_t fallback = 0)
{
  const auto value = object.find(key);
  if (value == object.end())
    return fallback;
  if (!value->is_number_integer() ||
      (!value->is_number_unsigned() && value->get<int64_t>() < 0))
    return {};
  const auto size = value->get<uint64_t>();
  if (size > (std::numeric_limits<size_t>::max)())
    return {};
  return static_cast<size_t>(size);
}

AccessorSpan GetAccessorSpan(const nlohmann::json &gltf,
                             const std::vector<std::vector<uint8_t>> &bins,
                             int accessorIndex)
{
  const auto accessors = gltf.find("accessors");
  const auto views = gltf.find("bufferViews");
  if (accessors == gltf.end() || !accessors->is_array() ||
      views == gltf.end() || !views->is_array() || accessorIndex < 0 ||
      static_cast<size_t>(accessorIndex) >= accessors->size())
    return {};

  const auto &accessor = (*accessors)[accessorIndex];
  const auto viewIndex = ReadExtent(accessor, "bufferView", views->size());
  if (!viewIndex || *viewIndex >= views->size())
    return {};
  const auto &view = (*views)[*viewIndex];
  const auto bufferIndex = ReadExtent(view, "buffer");
  if (!bufferIndex || *bufferIndex >= bins.size())
    return {};

  const auto &bin = bins[*bufferIndex];
  const auto viewOffset = ReadExtent(view, "byteOffset");
  if (!viewOffset || *viewOffset > bin.size())
    return {};
  // Older model packs also use views without byteLength.
  const auto length = ReadExtent(view, "byteLength", bin.size() - *viewOffset);
  const auto offset = ReadExtent(accessor, "byteOffset");
  const auto count = ReadExtent(accessor, "count");
  const auto components = TypeComponents(accessor.value("type", ""));
  const auto componentType = ReadExtent(accessor, "componentType");
  const auto rawStride = ReadExtent(view, "byteStride");
  if (!length || !offset || !count || !componentType || !rawStride)
    return {};
  const auto rowBytes = components * ComponentSize(*componentType);
  const auto stride = *rawStride ? *rawStride : rowBytes;
  // Check by subtraction and division before forming addresses or capacities.
  if (!rowBytes || !*count || stride < rowBytes || *length > bin.size() - *viewOffset ||
      *offset > *length || rowBytes > *length - *offset ||
      *count - 1 > (*length - *offset - rowBytes) / stride)
    return {};

  return {bin.data() + *viewOffset + *offset, stride, *count, components, *componentType};
}

std::vector<float> ReadFloatAccessor(const nlohmann::json &gltf,
                                     const std::vector<std::vector<uint8_t>> &bins,
                                     int accessorIndex)
{
  const auto span = GetAccessorSpan(gltf, bins, accessorIndex);
  if (!span.data || span.componentType != 5126)
    return {};

  std::vector<float> out(span.count * span.components);
  for (size_t i = 0; i < span.count; ++i)
    std::memcpy(out.data() + i * span.components, span.data + i * span.stride,
                span.components * sizeof(float));
  return out;
}

std::vector<unsigned int>
ReadIndexAccessor(const nlohmann::json &gltf, const std::vector<std::vector<uint8_t>> &bins,
                  int accessorIndex)
{
  const auto span = GetAccessorSpan(gltf, bins, accessorIndex);
  if (!span.data || span.components != 1)
    return {};

  std::vector<unsigned int> out;
  out.reserve(span.count);
  for (size_t i = 0; i < span.count; ++i)
  {
    const uint8_t *row = span.data + i * span.stride;
    switch (span.componentType)
    {
    case 5121:
      out.push_back(*row);
      break;
    case 5123:
    {
      uint16_t value;
      std::memcpy(&value, row, sizeof(value));
      out.push_back(value);
      break;
    }
    case 5125:
    {
      uint32_t value;
      std::memcpy(&value, row, sizeof(value));
      out.push_back(value);
      break;
    }
    case 5126:
    {
      // Legacy packs use floating-point indices.
      float value;
      std::memcpy(&value, row, sizeof(value));
      if (!std::isfinite(value) || value < 0.f ||
          double(value) > (std::numeric_limits<unsigned int>::max)())
        return {};
      out.push_back(static_cast<unsigned int>(value));
      break;
    }
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
  const auto span = GetAccessorSpan(gltf, bins, accessorIndex);
  if (!span.data || span.componentType != 5121)
    return {};

  std::vector<uint8_t> out(span.count * span.components);
  for (size_t i = 0; i < span.count; ++i)
    std::memcpy(out.data() + i * span.components, span.data + i * span.stride,
                span.components);
  return out;
}

std::vector<glm::mat4> ReadMat4Accessor(const nlohmann::json &gltf,
                                        const std::vector<std::vector<uint8_t>> &bins,
                                        int accessorIndex, VisualLoaderSource &source)
{
  const std::vector<float> floats = ReadFloatAccessor(gltf, bins, accessorIndex);
  source.Scratch(floats);
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
               CreatureGltfMeshAsset &asset, VisualLoaderSource &source)
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
  asset.skin.inverseBindMatrices = ReadMat4Accessor(gltf, bins, ibmAcc, source);
  asset.hasSkin = !asset.skin.jointNodes.empty();
}

GltfAnimationChannelCpu
ReadAnimationChannel(const nlohmann::json &gltf,
                     const std::vector<std::vector<uint8_t>> &bins,
                     const nlohmann::json &channelJson,
                     const nlohmann::json &samplerJson, VisualLoaderSource &source)
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
  source.Scratch(outFloats);
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
  VisualLoaderSource source(VisualLoaderKind::Gltf);
  const std::filesystem::path modelPath(gltfPath);
  source.Path(modelPath);
  std::ifstream in(modelPath);
  if (!in)
  {
    std::cerr << "CreatureGltfLoader: cannot open " << gltfPath << std::endl;
    return nullptr;
  }
  CatalogJsonDocument gltf;
  try
  {
    gltf = source.ReadDocument(in, CatalogJsonKind::Definitions, false);
  }
  catch (const std::exception &ex)
  {
    std::cerr << "CreatureGltfLoader: JSON parse error " << gltfPath << ": "
              << ex.what() << std::endl;
    return nullptr;
  }

  auto bins = LoadGltfBuffers(gltf, modelPath.parent_path(), source);

  auto asset = std::make_shared<CreatureGltfMeshAsset>();
  ParseNodes(gltf, *asset);
  ParseSkin(gltf, bins, *asset, source);
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
        {
          const auto positions = ReadFloatAccessor(gltf, bins, posAcc);
          const auto uvs = ReadFloatAccessor(gltf, bins, uvAcc);
          source.Scratch(positions, uvs);
          BuildInterleavedMesh(prim.mesh, positions, uvs);
        }
        const auto vertices = prim.mesh.interleavedPosUv.size() / 5;
        prim.mesh.indices = ReadIndexAccessor(gltf, bins, primJson.value("indices", -1));
        if (std::any_of(prim.mesh.indices.begin(), prim.mesh.indices.end(),
                        [vertices](unsigned index) { return index >= vertices; }))
          continue;
        prim.jointIndices = ReadU8Accessor(gltf, bins, jointsAcc);
        prim.jointWeights = ReadFloatAccessor(gltf, bins, weightsAcc);
        prim.skinned = !prim.jointIndices.empty() && !prim.jointWeights.empty() &&
                       asset->hasSkin;
        prim.textureStem =
            MaterialTextureStem(gltf, primJson.value("material", -1));
        if (!prim.mesh.interleavedPosUv.empty() && !prim.mesh.indices.empty())
        {
          prim.GpuHash = GltfSkinnedMeshGpuHash(prim);
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
                gltf, bins, chJson, samplers[samplerIndex], source));
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

  gltf = CatalogJsonDocument{};
  decltype(bins){}.swap(bins);
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

  asset->conservativeRenderRadius=ComputeGltfConservativeRenderRadius(*asset);
  source.Success();
  return asset;
}

} // namespace cutum

#pragma once

#include "Core/Memory/VisualLoaderSource.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonLoader.h"
#include "Creatures/Visual/Gltf/CreatureGltfLoader.h"
#include "Test/SharedVisualSourceStorageFixture.h"

namespace cutum::shared_visual_source_fixture
{

inline Json CheckVisualLoaderConstruction(const std::filesystem::path &report)
{
  auto folder = report;
  folder += ".visual-loader";
  Require(std::filesystem::create_directory(folder), "create isolated visual loader input");
  const auto model = folder / "model.gltf";
  const auto binary = folder / "model.bin";
  constexpr size_t fileBytes = 1024 * 1024;
  const auto writeBinary = [&](uint16_t thirdIndex)
  {
    std::ofstream out(binary, std::ios::binary | std::ios::trunc);
    const float vertices[] = {0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0};
    const uint16_t indices[] = {0, 1, thirdIndex};
    out.write(reinterpret_cast<const char *>(vertices), sizeof(vertices));
    out.write(reinterpret_cast<const char *>(indices), sizeof(indices));
    out.seekp(fileBytes - 1);
    out.put(0);
    Require(bool(out), "write valid interleaved glTF fixture and padding");
  };

  Json gltf = {
      {"asset", {{"version", "2.0"}}},
      {"buffers", Json::array({{{"uri", "model.bin"}, {"byteLength", fileBytes}}})},
      {"bufferViews", Json::array({
          {{"buffer", 0}, {"byteLength", 48}, {"byteStride", 16}},
          {{"buffer", 0}, {"byteOffset", 48}, {"byteLength", 6}}})},
      {"accessors", Json::array({
          {{"bufferView", 0}, {"componentType", 5126}, {"type", "VEC3"}, {"count", 3}},
          {{"bufferView", 1}, {"componentType", 5123}, {"type", "SCALAR"}, {"count", 3}}})},
      {"meshes", Json::array({{{"primitives", Json::array({
          {{"attributes", {{"POSITION", 0}}}, {"indices", 1}}})}}})}};
  const auto writeModel = [&]
  {
    std::ofstream out(model);
    out << gltf << "\nlegacy trailing note\n";
    Require(bool(out), "write complete legacy glTF document");
  };
  const auto modelName = model.string();
  writeBinary(2);
  writeModel();

  const auto before = GetVisualLoaderSources();
  size_t peak = 0, returned = 0;
  StlSourceStorage binaryRequest;
  binaryRequest.Buffer(fileBytes, sizeof(uint8_t));
  {
    observation::Scope observed;
    auto asset = CreatureGltfLoader::LoadFromFile(modelName);
    Require(asset && asset->primitives.size() == 1, "legacy JSON prefix and complete vertex views load");
    const auto &mesh = asset->primitives.front().mesh;
    Require(mesh.interleavedPosUv.size() == 15 && mesh.interleavedPosUv[5] == 1.f &&
                mesh.interleavedPosUv[11] == 1.f && mesh.indices.size() == 3 &&
                mesh.indices[2] == 2, "strided position and packed index results match fixture");
    const auto observedLoad = observation::Snapshot();
    StlSourceStorage retained;
    AddVisualAssetControl<CreatureGltfMeshAsset>(retained);
    MeasureVisualPayload(*asset, retained);
    peak = observedLoad.peak;
    returned = observedLoad.live;
    Require(observedLoad.max_request == binaryRequest.bytes.requested &&
                observedLoad.peak < 2 * binaryRequest.bytes.requested &&
                observedLoad.live == retained.bytes.requested && observedLoad.peak > observedLoad.live,
            "one binary extent allocation and the independently measured final asset");
    asset.reset();
    Require(observation::Snapshot().live == 0, "cold glTF source and payload fully retire");
  }
  const auto index = [](VisualLoaderKind kind) { return static_cast<size_t>(kind); };
  const auto valid = GetVisualLoaderSources()[index(VisualLoaderKind::Gltf)];
  Require(valid.completed == before[index(VisualLoaderKind::Gltf)].completed + 1 &&
              valid.binaryBackingMax >= binaryRequest.bytes.requested &&
              valid.scratchBackingMax >= 9 * sizeof(float),
          "production file and accessor observations cover the valid controlled fixture");
  gltf["accessors"][0]["count"] = 4;
  writeModel();
  Require(!CreatureGltfLoader::LoadFromFile(modelName),
          "position count must fit its view even when the binary has trailing data");
  gltf["accessors"][0]["count"] = 3.5;
  writeModel();
  Require(!CreatureGltfLoader::LoadFromFile(modelName),
          "fractional extents are rejected before conversion");
  gltf["accessors"][0]["count"] = 3;
  writeModel();
  writeBinary(3);
  Require(!CreatureGltfLoader::LoadFromFile(modelName),
          "triangle index must refer to a vertex in the decoded primitive");

  creature_item_icon_fixture::PlatformFixture platform;
  const auto root = IUPlatformPaths::Current().AssetRoot();
  const auto skeletonPath = (root / "models/creatures/human/geometry.geo.json").string();
  const auto itemPath = root / "models/items/bucket/model.gltf";
  auto &rigid = CreatureRigidModelCache::Instance();
  const auto rigidFolder = (root / "models/creatures/rigid_demo_walker").string();
  rigid.Clear();
  {
    observation::Scope observed;
    auto geometry = CreatureBoneSkeletonLoader::LoadFromFile(skeletonPath, {});
    Require(geometry && !geometry->bones.empty(), "cold skeletal source loads its geometry");
    geometry.reset();
    Require(observation::Snapshot().live == 0, "skeletal source releases file and geometry");
  }
  {
    observation::Scope observed;
    auto asset = LoadItemRenderAsset(itemPath);
    Require(asset && !asset->Primitives.empty(), "cold item source loads the original item");
    StlSourceStorage retained;
    AddVisualAssetControl<ItemRenderAsset>(retained);
    MeasureVisualPayload(*asset, retained);
    Require(observation::Snapshot().live == retained.bytes.requested,
            "only the returned item allocation remains after cold decode");
    asset.reset();
    Require(observation::Snapshot().live == 0, "cold item source and payload retire");
  }
  Require(bool(rigid.LoadParts(rigidFolder, "rigid_model.json")),
          "cold rigid source loads its original definition");
  rigid.Clear();
  const auto after = GetVisualLoaderSources();
  for (size_t i = 0; i < after.size(); ++i)
  {
    const auto &row = after[i];
    Require(row.completed == before[i].completed + 1 &&
                row.documents == before[i].documents +
                    (i == index(VisualLoaderKind::Gltf) ? 4 : 1) &&
                row.active == before[i].active && row.unclosedDocuments == 0 &&
                row.unsupportedSamples == 0 && row.jsonBytesMax && row.jsonHeapUpperMax &&
                row.pathUnitsMax && row.peakActive,
            "each production loader retires its stream and records the expected results");
  }
  return {{"status", "PASS"}, {"gltf_binary_bytes", fileBytes},
          {"gltf_binary_requested", binaryRequest.bytes.requested},
          {"gltf_peak_requested", peak}, {"gltf_returned_requested", returned},
          {"legacy_json_prefix", true}, {"glTF_view_range_checked", true},
          {"extent_type_checked", true}, {"index_range_checked", true},
          {"constructors_checked", after.size()}};
}

} // namespace cutum::shared_visual_source_fixture

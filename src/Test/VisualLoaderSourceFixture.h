#pragma once

#include "Core/Memory/VisualFilesystemSource.h"
#include "Core/Memory/VisualLoaderSource.h"
#include "Items/ItemSceneCursor.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonCubeMeshBuilder.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonLoader.h"
#include "Creatures/Visual/Gltf/CreatureGltfLoader.h"
#include "Test/SharedVisualSourceStorageFixture.h"

namespace cutum::shared_visual_source_fixture
{

inline Json CheckVisualFilesystemSource(const std::filesystem::path &report)
{
  if (!StlSourceStorage::SupportedProfile())
    return {{"status", "UNSUPPORTED"}};
#if defined(_WIN32) && defined(_MSC_VER)
  auto folder = std::filesystem::absolute(report);
  folder += L".visual-paths";
  Require(std::filesystem::create_directory(folder), "create isolated filesystem path input");
  const auto shortFile = folder / L"short";
  const auto longFile = folder / (std::wstring(110, L'n') + L".png");
  std::ofstream(shortFile).put('a');
  std::ofstream(longFile).put('b');
  auto longFolder = std::filesystem::path(L"\\\\?\\" + folder.native());
  for (int i = 0; i < 4; ++i)
  {
    longFolder /= std::wstring(72, static_cast<wchar_t>(L'a' + i));
    Require(std::filesystem::create_directory(longFolder), "create extended-length directory path");
  }
  Require(longFolder.native().size() > 260, "exercise the native path query resize");
  const auto missing = longFolder / L"missing" / L"another" / L"unavailable";
  const auto unicode = folder / L"\u00e9" / L".." / L"\u03b1";
  std::wstring manyTokens;
  for (int i = 0; i < 440; ++i)
    manyTokens += L"a/./b/../";
  const std::filesystem::path lexical(manyTokens);
  const auto relativeInput = folder / L"resource" / std::wstring(200, L'z');
  const auto relativeBase = folder / L"other" / L"nested";
  Json cases = Json::array();
  const auto check = [&](const char *name, VisualFilesystemOp operation, const auto &input,
                         size_t baseUnits, auto run, const auto &expected) {
    const auto bound = BoundVisualFilesystemSource(operation, input.native().size(), baseUnits);
    Require(bound.supported && !bound.overflow, "recognize the pinned filesystem request domain");
    observation::Counts recorded;
    {
      observation::Scope observe;
      {
        const auto actual = run();
        recorded = observation::Snapshot();
        Require(actual == expected, "observed visual path operation preserves the standard-library result");
      }
      const auto retired = observation::Snapshot();
      Require(!retired.live && !retired.blocks && recorded.peak && recorded.peak <= bound.requested,
              "filesystem requests fit the operation bound and retire after use");
    }
    for (size_t i = 0; i < recorded.attempts; ++i)
    {
      observation::Scope refuse(i);
      bool failed = false;
      try { (void)run(); }
      catch (const std::bad_alloc &) { failed = true; }
      const auto retired = observation::Snapshot();
      Require(failed && retired.refusals && !retired.live && !retired.blocks,
              "filesystem allocation refusal releases the admitted request prefix");
    }
    cases.push_back({{"name", name}, {"input_units", input.native().size()}, {"base_units", baseUnits},
        {"ordinary_peak", recorded.peak}, {"ordinary_upper", bound.requested},
        {"refusal_prefixes", recorded.attempts}, {"matches_std", true}, {"retired", true}});
  };
  check("absolute_long", VisualFilesystemOp::Absolute, longFolder, 0,
        [&] { return VisualAbsolute(longFolder); }, std::filesystem::absolute(longFolder));
  check("canonical_long", VisualFilesystemOp::WeakCanonical, longFolder, 0,
        [&] { return VisualWeakCanonical(longFolder); }, std::filesystem::weakly_canonical(longFolder));
  check("canonical_missing_suffix", VisualFilesystemOp::WeakCanonical, missing, 0,
        [&] { return VisualWeakCanonical(missing); }, std::filesystem::weakly_canonical(missing));
  check("lexical_many_tokens", VisualFilesystemOp::LexicalNormal, lexical, 0,
        [&] { return VisualLexicalNormal(lexical); }, lexical.lexically_normal());
  check("lexical_unicode", VisualFilesystemOp::LexicalNormal, unicode, 0,
        [&] { return VisualLexicalNormal(unicode); }, unicode.lexically_normal());
  check("lexical_relative", VisualFilesystemOp::LexicalRelative, relativeInput, relativeBase.native().size(),
        [&] { return VisualLexicalRelative(relativeInput, relativeBase); }, relativeInput.lexically_relative(relativeBase));
  check("generic_string", VisualFilesystemOp::GenericString, longFile, 0,
        [&] { return VisualGenericString(longFile); }, longFile.generic_string());
  check("native_string", VisualFilesystemOp::NativeString, longFile, 0,
        [&] { return VisualNativeString(longFile); }, longFile.string());
  const auto handles = [] {
    DWORD count{};
    Require(GetProcessHandleCount(GetCurrentProcess(), &count) != 0, "observe directory search handle retirement");
    return count;
  };
  const auto prior = handles();
  check("flat_directory", VisualFilesystemOp::FlatScan, folder, 0, [&] {
    size_t files = 0;
    VisualFilesystemSource source(VisualFilesystemOp::FlatScan, folder.native().size());
    for (const auto &entry : std::filesystem::directory_iterator(folder))
    {
      source.Output(entry.path().native());
      files += entry.is_regular_file();
    }
    source.Finish();
    return files;
  }, size_t{2});
  const auto overflow = BoundVisualFilesystemSource(VisualFilesystemOp::WeakCanonical, SIZE_MAX);
  Require(overflow.overflow && !overflow.supported && handles() == prior,
          "filesystem arithmetic refuses overflow and directory cases retire search handles");
  return {{"status", "PASS"}, {"cases", std::move(cases)},
          {"handles_restored", true}, {"overflow_rejected", true}};
#else
  (void)report;
  return {{"status", "UNSUPPORTED"}};
#endif
}

inline Json CheckItemSceneTraversal()
{
  constexpr size_t depth = 4096;
  auto nodes = Json::array();
  for (size_t i = 0; i < depth + 2; ++i)
  {
    Json node{{"id", i}};
    if (i == 0)
      node["children"] = {1, depth};
    else if (i + 1 < depth)
      node["children"] = {i + 1};
    nodes.push_back(std::move(node));
  }
  const auto roots = Json::array({0, depth + 1});
  ItemSceneTraversalStorage sample;
  size_t attempts = 0;
  {
    observation::Scope observed;
    {
      ItemSceneCursor cursor(nodes, roots);
      size_t index = 0;
      while (const auto step = cursor.Next())
      {
        const int expectedParent = index == 0 || index == depth + 1 ?
            -1 : index == depth ? 0 : static_cast<int>(index - 1);
        Require(step->Node->at("id").get<size_t>() == index && step->Parent == expectedParent,
                "iterative traversal keeps sibling order and emitted parent indices");
        ++index;
      }
      sample = cursor.InspectStorage();
      const auto requests = observation::Snapshot();
      attempts = requests.attempts;
      Require(index == nodes.size() && sample.visited == index && sample.maxDepth == depth &&
                  sample.supported && sample.peakRequested == requests.peak &&
                  sample.peakRequested == sample.visitedRequested + sample.frameGrowthRequested,
              "the recorded bitmap and frame growth peak match real allocation requests");
    }
    Require(!observation::Snapshot().live && !observation::Snapshot().blocks,
            "retire traversal backing at the end of the scene");
  }
  for (size_t i = 0; i < attempts; ++i)
  {
    observation::Scope refused(i);
    bool failed = false;
    try
    {
      ItemSceneCursor cursor(nodes, roots);
      while (cursor.Next()) {}
    }
    catch (const std::bad_alloc &) { failed = true; }
    Require(failed && !observation::Snapshot().live && !observation::Snapshot().blocks,
            "every allocation refusal retires its admitted scene frames and bitmap");
  }
  return {{"status", "PASS"}, {"nodes", sample.nodes}, {"visited", sample.visited},
          {"max_depth", sample.maxDepth}, {"frame_capacity", sample.frameCapacity},
          {"visited_requested", sample.visitedRequested}, {"frame_growth_requested", sample.frameGrowthRequested},
          {"peak_requested", sample.peakRequested}, {"refusal_prefixes", attempts},
          {"inline_cursor_bytes", sizeof(ItemSceneCursor)}};
}

inline Json CheckVisualFileBuffers(const std::filesystem::path &report)
{
  auto folder = report;
  folder += ".visual-files";
  Require(std::filesystem::create_directory(folder), "create isolated file-buffer input");
  const auto model = folder / "buffered.json";
  const auto broken = folder / "malformed.json";
  const auto binary = folder / "mesh.bin";
  constexpr size_t buffer = VisualModelInputFile::BufferBytes;
  const std::string payload(buffer * 2 + 17, 'v');
  {
    std::ofstream out(model, std::ios::binary);
    out << "{\r\n  \"payload\": \"" << payload << "\"\r\n}\r\n";
    Require(bool(out), "write a model spanning multiple text refills");
  }
  {
    std::ofstream out(broken, std::ios::binary);
    out << "{\"payload\":[0,";
    Require(bool(out), "write an incomplete model");
  }
  {
    std::ofstream out(binary, std::ios::binary);
    out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    Require(bool(out), "write a buffer spanning multiple binary refills");
  }

  const auto before = GetVisualFileBuffers();
  VisualLoaderSource source(VisualLoaderKind::Rigid);
  {
    VisualModelInputFile one(model, source);
    VisualModelInputFile two(binary, source, std::ios::binary);
    const auto both = GetVisualFileBuffers();
    Require(one && two && both.active == before.active + 2 &&
                both.activeBufferBytes == before.activeBufferBytes + 2 * buffer &&
                both.peakBufferBytes >= both.activeBufferBytes &&
                both.configured == before.configured + 2,
            "both open streams simultaneously use their own fixed buffer");
    Require(one.Stream().get() == '{' && two.Stream().get() == 'v',
            "attached streams return the original contents");
    Require(one.Close() && one.Close(), "explicit retirement is idempotent");
    const auto second = GetVisualFileBuffers();
    Require(second.active == before.active + 1 &&
                second.activeBufferBytes == before.activeBufferBytes + buffer &&
                second.closed == before.closed + 1,
            "the remaining stream keeps exactly one attached buffer");
  }
  bool rejected = false;
  try { source.ReadDocument(broken, CatalogJsonKind::Definitions); }
  catch (const nlohmann::json::exception &) { rejected = true; }
  const auto afterError = GetVisualFileBuffers();
  Require(rejected && afterError.active == before.active &&
              afterError.closed == before.closed + 3,
          "a parser exception closes its input stream");

  auto document = source.ReadDocument(model, CatalogJsonKind::Definitions);
  Require(document && document->at("payload") == payload,
          "the same reader decodes complete CRLF text across buffer boundaries");
  auto bytes = ReadVisualModelBuffer(binary, source, payload.size());
  Require(bytes && bytes->size() == payload.size() &&
              std::equal(bytes->begin(), bytes->end(), payload.begin()),
          "bulk binary read keeps all bytes at the declared extent");
  Require(!ReadVisualModelBuffer(binary, source, payload.size() - 1),
          "a size mismatch retires the stream without returning a candidate");
  Require(!source.ReadDocument(folder / "absent.json", CatalogJsonKind::Definitions),
          "missing input retains the recoverable open result");
  const auto after = GetVisualFileBuffers();
  Require(after.active == before.active && after.activeBufferBytes == before.activeBufferBytes &&
              after.opened == before.opened + 6 && after.configured == before.configured + 6 &&
              after.closed == before.closed + 6 && after.fallback == before.fallback &&
              after.closeFailures == before.closeFailures,
          "success, exception, extent mismatch and open refusal leave no active file buffer");
  return {{"status", "PASS"}, {"bytes_per_stream", buffer},
          {"simultaneous_buffer_bytes", 2 * buffer}, {"opened_and_retired", size_t(6)},
          {"crlf_and_binary_bytes", payload.size()}, {"inline_file_owner_bytes", sizeof(VisualModelInputFile)}};
}

inline Json CheckSkeletonMeshConstruction()
{
  creature_item_icon_fixture::PlatformFixture platform;
  const auto path = IUPlatformPaths::Current().AssetRoot() / "models/creatures/human/geometry.geo.json";
  const auto original = CreatureBoneSkeletonLoader::LoadFromFile(path.string(), {});
  Require(original && !original->bones.empty() && !original->boneIndexByName.empty(),
          "load the real human skeleton for both assembly paths");
  const auto reference = BoneSkeletonCubeMeshBuilder::BuildMeshAsset(*original);
  size_t copiedPeak = 0;
  {
    observation::Scope observed;
    {
      const auto copied = BoneSkeletonCubeMeshBuilder::BuildMeshAsset(*original);
      copiedPeak = observation::Snapshot().peak;
      Require(copied.boneMeshes.size() == reference.boneMeshes.size(),
              "borrowed builder retains the full skeleton");
    }
    Require(!observation::Snapshot().live, "borrowed assembly releases its requests");
  }
  auto staging = original;
  const auto *bones = staging->bones.data();
  const auto *indexEntry = &*staging->boneIndexByName.begin();
  size_t attempts = 0, movedPeak = 0;
  SkeletonMeshBuildStorage sample;
  {
    observation::Scope observed;
    auto asset = BoneSkeletonCubeMeshBuilder::BuildSharedMeshAsset(std::move(*staging), &sample);
    const auto live = observation::Snapshot();
    attempts = live.attempts;
    movedPeak = live.peak;
    Require(sample.supported && live.live == sample.requestedBytes &&
                movedPeak == sample.requestedBytes && movedPeak < copiedPeak,
            "moved assembly and its empty index match independent observed requests");
    Require(asset->geometry.bones.data() == bones &&
                &*asset->geometry.boneIndexByName.begin() == indexEntry &&
                asset->boneMeshes.size() == reference.boneMeshes.size(),
            "production assembly uses the original geometry and index storage");
    for (size_t i = 0; i < asset->boneMeshes.size(); ++i)
    {
      const auto &expected = reference.boneMeshes[i];
      const auto &output = asset->boneMeshes[i];
      Require(output.boneName == expected.boneName && output.cubes.size() == expected.cubes.size(),
              "bone order and cuboids are preserved");
      for (size_t j = 0; j < output.cubes.size(); ++j)
      {
        const auto &a = output.cubes[j];
        const auto &b = expected.cubes[j];
        Require(a.indices == b.indices && a.interleavedPosUv == b.interleavedPosUv &&
                    a.restLocalMatrix == b.restLocalMatrix && a.GpuHash == b.GpuHash,
                "moved mesh matches positions, UVs, indices, matrices and hash");
      }
    }
    staging.reset();
    Require(observation::Snapshot().live == sample.controlBytes + sample.meshBytes,
            "retiring the moved-from skeleton releases its empty index before publication");
    asset.reset();
    Require(!observation::Snapshot().live, "moved assembly releases all new requests");
  }
  for (size_t i = 0; i < attempts; ++i)
  {
    auto candidate = original;
    observation::Scope refused(i);
    bool failed = false;
    try { BoneSkeletonCubeMeshBuilder::BuildSharedMeshAsset(std::move(*candidate)); }
    catch (const std::bad_alloc &) { failed = true; }
    candidate.reset();
    Require(failed && !observation::Snapshot().live && !observation::Snapshot().blocks,
            "each refused mesh assembly releases its provisional buffers");
  }
  return {{"status", "PASS"}, {"borrowed_peak_requested", copiedPeak},
          {"moved_peak_requested", movedPeak}, {"mesh_requested", sample.meshBytes},
          {"control_requested", sample.controlBytes}, {"empty_index_requested", sample.emptyIndexBytes},
          {"explicit_cube_arrays", sample.explicitScratchBytes}, {"bones", sample.bones},
          {"cubes", sample.cubes}, {"refusal_prefixes", attempts}};
}

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
  size_t itemPeak = 0, itemReturned = 0;
  {
    observation::Scope observed;
    auto asset = LoadItemRenderAsset(itemPath);
    Require(asset && !asset->Primitives.empty(), "cold item source loads the original item");
    StlSourceStorage retained;
    AddVisualAssetControl<ItemRenderAsset>(retained);
    MeasureVisualPayload(*asset, retained);
    const auto itemRequests = observation::Snapshot();
    itemPeak = itemRequests.peak;
    itemReturned = itemRequests.live;
    Require(itemReturned == retained.bytes.requested && itemPeak > itemReturned,
            "only the returned item allocation remains after cold decode");
    for (const auto &primitive : asset->Primitives)
      Require(primitive.Vertices.capacity() == primitive.Vertices.size() &&
                  primitive.Indices.capacity() == primitive.Indices.size(),
              "item vertex and index backing matches decoded counts without growth slack");
    asset.reset();
    Require(observation::Snapshot().live == 0, "cold item source and payload retire");
  }
  Require(bool(rigid.LoadParts(rigidFolder, "rigid_model.json")),
          "cold rigid source loads its original definition");
  rigid.Clear();
  const auto after = GetVisualLoaderSources();
  const auto &item = after[index(VisualLoaderKind::Item)];
  const auto &previousItem = before[index(VisualLoaderKind::Item)];
  Require(item.resourceRoots == previousItem.resourceRoots + 1 &&
              item.resourceFiles >= previousItem.resourceFiles + 2 &&
              item.resourcePathBackingMax > 0,
          "the real item resolves a single root for its binary and albedo paths");
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
          {"item_peak_requested", itemPeak}, {"item_returned_requested", itemReturned},
          {"item_resource_roots", item.resourceRoots - previousItem.resourceRoots},
          {"item_resource_files", item.resourceFiles - previousItem.resourceFiles},
          {"legacy_json_prefix", true}, {"glTF_view_range_checked", true},
          {"extent_type_checked", true}, {"index_range_checked", true},
          {"constructors_checked", after.size()}};
}

} // namespace cutum::shared_visual_source_fixture

#pragma once

#include "Core/Memory/VisualAssetStorage.h"
#include "Core/Memory/VisualAssetCensus.h"
#include "Creatures/Influence/StatusEffectCatalog.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonCache.h"
#include "Creatures/Visual/CreatureMeshGpuCache.h"
#include "Creatures/Visual/CreatureRigidModelLoader.h"
#include "Creatures/Visual/Gltf/CreatureGltfCache.h"
#include "Items/ItemRenderAsset.h"
#include "Test/CreatureItemIconStorageFixture.h"
#include "WorldGen/Core/CatalogJsonSource.h"
#include <thread>

namespace cutum::shared_visual_source_fixture
{
using gui_context_fixture::Require;
using shared_content_preview_fixture::RefuseIndexUpload;
using shared_content_preview_fixture::ReadCallerState;
using Json = nlohmann::json;

inline Json CheckRetainedVisualOwners()
{
  const auto index = [](VisualAssetKind kind) { return static_cast<size_t>(kind); };
  const auto role = [](VisualAssetOwner owner) { return static_cast<size_t>(owner); };
  size_t assetsBytes = 0, queueBytes = 0, observerBytes = 0;
  {
    observation::Scope observed;
    CreatureDrawQueue queue;
    auto gltf = std::make_shared<CreatureGltfMeshAsset>();
    auto &primitive = gltf->primitives.emplace_back();
    primitive.mesh.interleavedPosUv.resize(24);
    auto bones = std::make_shared<CreatureBoneSkeletonMeshAsset>();
    auto &cube = bones->boneMeshes.emplace_back().cubes.emplace_back();
    cube.indices.resize(36);
    auto item = std::make_shared<ItemRenderAsset>();
    item->Primitives.emplace_back().Vertices.resize(96);
    auto request = std::make_shared<ItemDrawRequest>();
    request->Asset = item;
    request->Path = "models/items/retained_fixture_geometry.gltf";
    request->State.HiddenNodes.emplace("retained_fixture_hidden_node");
    auto palette = std::make_shared<std::vector<glm::mat4>>(3);
    auto legacy = std::make_shared<LegacyItemPartsMesh>();
    legacy->meshes.emplace_back().indices.resize(12);
    const auto push = [&](CreatureDrawRequest draw) { queue.Push(std::move(draw)); };
    CreatureDrawRequest mesh;
    mesh.SkeletalMesh = {gltf, &primitive.mesh};
    push(std::move(mesh));
    mesh.SkinnedPrimitive = {gltf, &primitive};
    mesh.BoneMatrices = palette;
    push(std::move(mesh));
    mesh.SkeletalMesh = {bones, &cube};
    push(std::move(mesh));
    mesh.SkeletalMesh = {legacy, &legacy->meshes.front()};
    push(std::move(mesh));
    mesh.ItemSurface = request;
    push(std::move(mesh));
    const auto live = observation::Snapshot().live;
    {
      VisualAssetCensus census;
      census.AddQueue(queue);
      census.Add(gltf, VisualAssetOwner::World);
      census.Add(item, VisualAssetOwner::Preview);
      census.Add(palette, VisualAssetOwner::World);
      const auto snapshot = census.Result();
      Require(snapshot.supported && !snapshot.unmeasuredReferences,
              "known owned and aliasing draws compose to one source snapshot");
      for (const auto &row : snapshot.rows)
        assetsBytes += row.detachedControlBytes + row.detachedPayloadBytes;
      queueBytes = snapshot.queueBytes;
      observerBytes = snapshot.observerBytes;
      Require(assetsBytes + queueBytes == live &&
                  observation::Snapshot().live == live + observerBytes,
              "retained asset, queue and observer requests match independent live allocation");
      const auto &gltfRow = snapshot.rows[index(VisualAssetKind::Gltf)];
      Require(gltfRow.detached == 1 && gltfRow.references[role(VisualAssetOwner::Queue)] == 2 &&
                  gltfRow.references[role(VisualAssetOwner::World)] == 1 &&
                  snapshot.rows[index(VisualAssetKind::Item)].detached == 1 &&
                  snapshot.rows[index(VisualAssetKind::Palette)].detached == 1,
              "aliases across requests and receivers are counted as one coallocation");
      census.Add(gltf, VisualAssetOwner::Cache);
      const auto resident = census.Result().rows[index(VisualAssetKind::Gltf)];
      Require(resident.cached == 1 && resident.shared == 1 && resident.detached == 0 &&
                  !resident.detachedControlBytes && !resident.detachedPayloadBytes,
              "cache accounting is joined without charging the same asset again");
    }
    Require(observation::Snapshot().live == live, "closing the observer retires its own table");
    queue.Clear();
    request.reset();
    gltf.reset();
    bones.reset();
    item.reset();
    palette.reset();
    legacy.reset();
    Require(observation::Snapshot().live == queueBytes,
            "clearing the receivers retires all detached allocations except retained queue backing");
  }
  Require(!observation::Snapshot().live && !observation::Snapshot().blocks,
          "the queue retires its backing on scope exit");
  {
    VisualAssetCensus census;
    const std::shared_ptr<ItemRenderAsset> external(new ItemRenderAsset);
    census.Add(external, VisualAssetOwner::Preview);
    const auto snapshot = census.Result();
    Require(!snapshot.supported && snapshot.unmeasuredReferences == 1 &&
                !snapshot.rows[index(VisualAssetKind::Item)].detached,
            "an external control is reported without assigning a default-factory bound");
  }
  {
    creature_item_icon_fixture::PlatformFixture platform;
    const auto path = IUPlatformPaths::Current().AssetRoot() / "models/items/bucket/model.gltf";
    auto &cache = CreatureGltfCache::Instance();
    cache.Clear();
    auto old = cache.LoadFromPath(path);
    Require(old && !old->primitives.empty(), "load the real glTF cache predecessor");
    std::weak_ptr<CreatureGltfMeshAsset> predecessor = old;
    CreatureDrawQueue queue;
    CreatureDrawRequest draw;
    draw.SkeletalMesh = {old, &old->primitives.front().mesh};
    queue.Push(std::move(draw));
    VisualAssetStorage resident;
    {
      VisualAssetCensus census;
      resident = cache.InspectOwnedStorage(&census);
      census.AddQueue(queue);
      const auto counts = census.Result().rows[index(VisualAssetKind::Gltf)];
      Require(resident.live == 1 && counts.cached == 1 && counts.shared == 1 && !counts.detached,
              "live cache publication overlaps the real queued predecessor");
    }
    old.reset();
    cache.Clear();
    auto current = cache.LoadFromPath(path);
    Require(current && predecessor.lock() != current, "reload publishes a different allocation");
    {
      VisualAssetCensus census;
      const auto loaded = cache.InspectOwnedStorage(&census);
      census.AddQueue(queue);
      const auto counts = census.Result().rows[index(VisualAssetKind::Gltf)];
      Require(loaded.live == 1 && counts.cached == 1 && !counts.shared && counts.detached == 1 &&
                  counts.detachedControlBytes == resident.controlBytes &&
                  counts.detachedPayloadBytes == resident.payloadBytes,
              "the real predecessor moves from shared to detached while the new cache stays live");
    }
    queue.Clear();
    Require(predecessor.expired(), "clearing the final real draw releases the earlier cache payload");
    cache.Clear();
  }
  return {{"status", "PASS"}, {"asset_bytes", assetsBytes}, {"queue_bytes", queueBytes},
          {"observer_bytes", observerBytes}, {"external_factory_reported", true},
          {"real_cache_reload_composed", true}};
}

inline Json CheckRigidModels(const std::filesystem::path &report)
{
  auto root = report;
  root += ".rigid-model";
  Require(std::filesystem::create_directory(root), "create isolated rigid model input");
  const auto directory = root.string();
  const std::string file = "rigid_model.json";
  const std::string aliasPath = "./rigid_model.json";
  const std::string id = "fixture_hinged_rigid_model_forelimb";
  const auto write = [&](float width)
  {
    std::ofstream output(root / file);
    output << Json{{"parts", Json::array({{{"id", id},
        {"texture", "fixture_external_texture_stem"}, {"size", {width, 1, 1}},
        {"pivot", {1, 2, 3}}, {"limb", "fixture_extended_limb_name"}}})}};
    Require(bool(output), "write rigid model input");
  };

  auto &cache = CreatureRigidModelCache::Instance();
  cache.Clear();
  Require(cache.InspectOwnedStorage().cpuBytes == 0, "rigid cache has no dormant table");
  write(2.f);

  size_t attempts = 0;
  VisualAssetStorage loaded;
  {
    observation::Scope observed;
    auto parts = cache.LoadParts(directory, file);
    attempts = observation::State.attempts;
    loaded = cache.InspectOwnedStorage();
    Require(loaded.supported && loaded.live == 1 &&
                loaded.cpuBytes == observation::State.live && parts &&
                parts->size() == 1 && parts->front().Id == id &&
                parts->front().HasPivot && parts->front().sizeBlocks.x == 2.f,
            "rigid table, control and payload match independent live requests");
    auto alias = cache.LoadParts(directory, aliasPath);
    Require(alias == parts && observation::State.live == loaded.cpuBytes,
            "a lexical path alias shares the original immutable payload");
    cache.Clear();
    const auto detachedBytes = loaded.controlBytes + loaded.payloadBytes;
    Require(cache.InspectOwnedStorage().cpuBytes == 0 &&
                observation::State.live == detachedBytes,
            "clearing the cache leaves exactly the bytes held by external readers");
    parts.reset();
    Require(observation::State.live == detachedBytes &&
                alias->front().sizeBlocks.x == 2.f,
            "the earlier generation remains usable until its final reader lets go");
    alias.reset();
    Require(observation::State.live == 0 && observation::State.blocks == 0,
            "the last reader retires all detached rigid storage");
  }

  Require(attempts > 0, "cold rigid publication performs allocations");
  {
    observation::Scope refused(attempts - 1);
    bool failed = false;
    try { cache.LoadParts(directory, file); }
    catch (const std::bad_alloc &) { failed = true; }
    Require(failed && cache.InspectOwnedStorage().cpuBytes == 0 &&
                observation::State.live == 0 && observation::State.blocks == 0,
            "refused rigid publication leaves no cache entry or payload");
  }
  auto predecessor = cache.LoadParts(directory, file);
  Require(bool(predecessor), "refused rigid publication can be retried");
  write(5.f);
  cache.Clear();
  auto successor = cache.LoadParts(directory, file);
  Require(successor && predecessor != successor &&
              predecessor->front().sizeBlocks.x == 2.f &&
              successor->front().sizeBlocks.x == 5.f,
          "replacement publishes new parts while existing readers retain their earlier data");
  cache.Clear();
  return {{"status", "PASS"}, {"table_bytes", loaded.tableBytes},
          {"control_bytes", loaded.controlBytes}, {"payload_bytes", loaded.payloadBytes},
          {"publication_refusal_index", attempts - 1},
          {"detached_bytes", loaded.controlBytes + loaded.payloadBytes}};
}

inline Json CheckStatusCatalog(const std::filesystem::path &report)
{
  const auto cold = UStatusEffectCatalog::InspectPublishedStorage();
  Require(cold.supported && !cold.published && !cold.tableBytes && cold.inlineBytes > 0,
          "observing the cold status catalog does not initialize its table");
  {
    observation::Scope refused(0);
    bool failed = false;
    try { UStatusEffectCatalog::Get(); }
    catch (const std::bad_alloc &) { failed = true; }
    Require(failed && !UStatusEffectCatalog::InspectPublishedStorage().published && !observation::State.live,
            "refused first status-catalog construction leaves no published state");
  }
  const UStatusEffectCatalog *first = nullptr, *second = nullptr;
  std::thread one([&] { first = &UStatusEffectCatalog::Get(); });
  std::thread two([&] { second = &UStatusEffectCatalog::Get(); });
  one.join();
  two.join();
  const auto warm = UStatusEffectCatalog::InspectPublishedStorage();
  Require(first && first == second && first->Find("bleed") && first->Find("slow") && warm.published &&
              warm.builtinsReady && warm.filesAttempted && warm.inlineBytes == cold.inlineBytes,
          "independent first-use callers share one complete status catalog");

  auto root = report;
  root += ".status-catalog";
  Require(std::filesystem::create_directory(root), "create isolated status-effect input folder");
  const std::string longer = "effect_catalog_fixture_owned_identifier";
  {
    std::ofstream(root / "override.json") << Json{{"id", "bleed"}, {"health_per_tick", -7.5f}};
    std::ofstream(root / "custom.json") << Json{{"id", longer}, {"stack", "stack"}, {"max_stacks", 3}};
  }
  size_t tableBytes = 0;
  const auto input = root.string();
  {
    const auto before = GetCatalogJsonSourceSnapshot();
    observation::Scope observed;
    {
      auto local = UStatusEffectCatalog::LoadFromDirectory(input);
      const auto status = local.InspectOwnedStorage();
      Require(status.supported && status.tableBytes == observation::State.live && status.definitions == 3 &&
                  local.Find("bleed") && local.Find("bleed")->HealthPerTick == -7.5f && local.Find("slow") &&
                  local.Find(longer) && local.Find(longer)->Stack == StatusStackPolicy::Stack,
              "status source equals live requests and preserves overrides and fallback definitions");
      auto replacement = *local.Find(longer);
      replacement.MaxStacks = 5;
      const auto original = local.Find(longer);
      observation::State.fail_after = observation::State.attempts;
      bool failed = false;
      try { local.Register(replacement); }
      catch (const std::bad_alloc &) { failed = true; }
      observation::State.fail_after = SIZE_MAX;
      Require(failed && local.Find(longer) == original && original->MaxStacks == 3,
              "refused definition copy preserves the live definition and address");
      local.Register(replacement);
      Require(local.Find(longer) == original && original->MaxStacks == 5, "same-key retry preserves borrowed definition address");
      tableBytes = status.tableBytes;
    }
    const auto after = GetCatalogJsonSourceSnapshot();
    const auto index = static_cast<size_t>(CatalogJsonKind::StatusEffects);
    Require(after.attempts[index] == before.attempts[index] + 2 && after.active == before.active &&
                !observation::State.live && !observation::State.blocks,
            "status parser records two actual files and all local source requests retire");
  }
  {
    std::ofstream(root / "unreadable_definition.json") << "{\"id\": [";
  }
  const auto withMalformed = UStatusEffectCatalog::LoadFromDirectory(input);
  Require(withMalformed.InspectOwnedStorage().definitions == 3 && withMalformed.Find("slow"),
          "malformed content preserves the remaining effects and builtins");
  return {{"status", "PASS"}, {"local_table_bytes", tableBytes}, {"local_definitions", 3},
          {"cold_preserved", true}, {"cold_refusal_recovered", true}, {"shared_first_use", true},
          {"override_replacement_recovered", true}};
}

template<class Cache, class Loader>
inline std::pair<size_t, size_t> CheckWeakAsset(Cache &cache, const Loader &load)
{
  cache.Clear();
  const auto baseline = cache.InspectOwnedStorage();
  observation::Scope epoch;
  auto asset = load();
  Require(bool(asset), "visual fixture loads the original model");
  Require(load().get() == asset.get(), "same visual key shares its live asset");
  const auto live = cache.InspectOwnedStorage();
  Require(live.supported && live.live == 1 && !live.expired && live.controlBytes && live.payloadBytes &&
              live.cpuBytes == baseline.cpuBytes + observation::State.live,
          "live weak model table, control and payload match independent allocation observations");
  asset.reset();
  const auto expired = cache.InspectOwnedStorage();
  Require(expired.supported && !expired.live && expired.expired == 1 && !expired.payloadBytes &&
              expired.controlBytes == live.controlBytes && expired.cpuBytes == baseline.cpuBytes + observation::State.live,
          "expired weak key retains only its node and original coallocation");
  cache.Clear();
  Require(!cache.InspectOwnedStorage().controlBytes && !observation::State.live && !observation::State.blocks,
          "clearing the weak cache retires the expired control allocation");
  return {live.payloadBytes, live.controlBytes};
}

inline void CheckRetiredGpuNames(const std::array<GLuint, 3> &names)
{
  Require(names[0] && names[1] && names[2] && !glIsVertexArray(names[0]) &&
              !glIsBuffer(names[1]) && !glIsBuffer(names[2]), "refused upload retires every provisional GL name");
}

inline std::pair<size_t, size_t> CheckGpu()
{
  auto &cache = CreatureMeshGpuCache::Instance();
  cache.DestroyAll();
  BoneSkeletonCubeMeshCpu skeletal;
  skeletal.interleavedPosUv = {0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
  skeletal.indices = {0, 1, 2};
  GltfPrimitiveCpu skinned;
  skinned.mesh = skeletal;
  skinned.jointIndices.assign(12, 0);
  skinned.jointWeights = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
  const auto caller = ReadCallerState();
  {
    RefuseIndexUpload refused;
    Require(!cache.GetOrCreateSkeletalMeshVao(skeletal), "refuse skeletal index upload");
  }
  CheckRetiredGpuNames(RefuseIndexUpload::Observed);
  size_t count = 99;
  {
    RefuseIndexUpload refused;
    Require(!cache.GetOrCreateGltfSkinnedMeshVao(skinned, count) && !count, "refuse skinned index upload");
  }
  CheckRetiredGpuNames(RefuseIndexUpload::Observed);
  const auto baseline = cache.InspectOwnedStorage();
  Require(!baseline.skeletal && !baseline.skinned && ReadCallerState() == caller, "refused work leaves the GPU cache unpublished and restores bindings");
  {
    RefuseIndexUpload upload(false);
    observation::Scope refused(0);
    bool failed = false;
    try { cache.GetOrCreateSkeletalMeshVao(skeletal); }
    catch (const std::bad_alloc &) { failed = true; }
    Require(failed && !cache.InspectOwnedStorage().skeletal && !observation::State.live,
            "GPU upload is provisional until table allocation succeeds");
    CheckRetiredGpuNames(RefuseIndexUpload::Observed);
  }

  observation::Scope epoch;
  const auto normal = cache.GetOrCreateSkeletalMeshVao(skeletal);
  const auto animated = cache.GetOrCreateGltfSkinnedMeshVao(skinned, count);
  Require(normal && animated && count == 3 && ReadCallerState() == caller, "both GPU uploads retry without disturbing caller bindings");
  Require(normal == cache.GetOrCreateSkeletalMeshVao(skeletal) && animated == cache.GetOrCreateGltfSkinnedMeshVao(skinned, count),
          "the same completed GPU work reuses both cache entries");
  const auto live = cache.InspectOwnedStorage();
  GLint64 vertex = 0, index = 0;
  for (GLuint vao : {normal, animated})
  {
    glBindVertexArray(vao);
    GLint vbo = 0;
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, GLuint(vbo));
    GLint64 component = 0;
    glGetBufferParameteri64v(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &component);
    vertex += component;
    glGetBufferParameteri64v(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &component);
    index += component;
  }
  glBindVertexArray(GLuint(caller.vao));
  glBindBuffer(GL_ARRAY_BUFFER, GLuint(caller.array));
  Require(live.supported && live.skeletal == 1 && live.skinned == 1 && live.tableBytes == baseline.tableBytes + observation::State.live &&
              live.vertexBytes == size_t(vertex) && live.indexBytes == size_t(index) && vertex > 0 && index > 0,
          "GPU cache table and both buffer totals match independent allocation and driver queries");
  cache.DestroyAll();
  Require(!observation::State.live && !glIsVertexArray(normal) && !glIsVertexArray(animated) && ReadCallerState() == caller,
          "cache teardown retires published GPU objects and selected CPU allocations");
  return {live.vertexBytes, live.indexBytes};
}

inline Json CheckVisualSources()
{
  creature_item_icon_fixture::PlatformFixture platform;
  const auto root = IUPlatformPaths::Current().AssetRoot();
  const auto path = root / "models/items/bucket/model.gltf";
  const auto alternate = path.parent_path() / "." / path.filename();
  ClearItemSurfaceAssets();
  const auto baseline = InspectItemSurfaceAssetStorage();
  size_t itemPayload = 0, itemControl = 0, attempts = 0;
  {
    observation::Scope epoch;
    auto asset = FindItemSurfaceAsset(path, UItemDefinitionStorage::ContentRevision());
    attempts = observation::State.attempts;
    Require(asset && FindItemSurfaceAsset(alternate, UItemDefinitionStorage::ContentRevision()) == asset,
            "canonical item asset is shared by both path spellings");
    const auto live = InspectItemSurfaceAssetStorage();
    Require(live.supported && live.live == 1 && live.aliases == 2 && live.controlBytes && live.payloadBytes &&
                live.cpuBytes == baseline.cpuBytes + observation::State.live,
            "item asset payload and alias stores agree with independent allocation observation");
    ClearItemSurfaceAssets();
    Require(!InspectItemSurfaceAssetStorage().live && observation::State.live == live.controlBytes + live.payloadBytes,
            "clearing the item cache preserves a separately held publication");
    asset.reset();
    Require(!observation::State.live && !observation::State.blocks, "last detached item reference retires its payload");
    itemPayload = live.payloadBytes;
    itemControl = live.controlBytes;
  }
  {
    Require(attempts > 0, "record item publication allocation count");
    observation::Scope epoch(attempts - 1);
    bool failed = false;
    try { FindItemSurfaceAsset(path, UItemDefinitionStorage::ContentRevision()); }
    catch (const std::bad_alloc &) { failed = true; }
    const auto rejected = InspectItemSurfaceAssetStorage();
    Require(failed && !rejected.live && !rejected.aliases && !observation::State.live,
            "refused final alias request retires the unpublished item and its key");
  }
  auto &gltf = CreatureGltfCache::Instance();
  const auto gltfStorage = CheckWeakAsset(gltf, [&] { return gltf.LoadFromPath(path); });
  auto &bones = CreatureBoneSkeletonCache::Instance();
  const auto sourceDir = (root / "models/creatures/human").string();
  const auto boneStorage = CheckWeakAsset(bones, [&] { return bones.Load("human", "geometry.geo.json", {}, sourceDir); });
  const auto buffers = CheckGpu();
  return {{"status", "PASS"}, {"item_payload_bytes", itemPayload}, {"item_control_bytes", itemControl},
          {"gltf_payload_bytes", gltfStorage.first}, {"gltf_expired_control_bytes", gltfStorage.second},
          {"skeleton_payload_bytes", boneStorage.first}, {"skeleton_expired_control_bytes", boneStorage.second},
          {"vertex_buffer_bytes", buffers.first}, {"index_buffer_bytes", buffers.second},
          {"alias_refusal_recovered", true}, {"detached_item_retired", true}, {"gpu_refusals_recovered", true}};
}

} // namespace cutum::shared_visual_source_fixture

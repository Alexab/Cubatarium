#pragma once

#include "Core/Memory/VisualAssetStorage.h"
#include "Creatures/Influence/StatusEffectCatalog.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonCache.h"
#include "Creatures/Visual/CreatureMeshGpuCache.h"
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

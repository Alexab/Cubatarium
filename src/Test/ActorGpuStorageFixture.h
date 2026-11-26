#pragma once

#include "Creatures/Visual/CreatureBonePaletteGpu.h"
#include "Creatures/Visual/CreatureMeshGpuCache.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Items/FpViewmodelRenderer.h"
#include "Items/ItemDrawRequest.h"
#include "Render/Engine/CreatureDrawPass.h"
#include "Test/PreviewGpuStorageFixture.h"

namespace cutum::actor_gpu_storage_fixture
{
using gui_context_fixture::Require;
using Role = TerrainGpuBufferRole;

inline nlohmann::json Run(const std::filesystem::path &reportPath, UWorld &world)
{
  creature_item_icon_fixture::PlatformFixture platform;
  const auto root = std::filesystem::absolute(reportPath.string() + ".actor-gpu");
  Require(std::filesystem::create_directory(root), "create isolated legacy actor meshes");
  const auto triangle = root / "triangle.gltf";
  const auto quad = root / "quad.gltf";
  const auto bucket = IUPlatformPaths::Current().AssetRoot() / "models/items/bucket/model.gltf";
  preview_gpu_storage_fixture::WriteLegacy(triangle, false);
  preview_gpu_storage_fixture::WriteLegacy(quad, true);
  for (const auto &[id, model] : {std::pair{"actor_fixture_triangle", triangle},
                                  std::pair{"actor_fixture_quad", quad}, std::pair{"bucket", bucket}})
  {
    std::ofstream out(root / (std::string(id) + ".json"));
    out << nlohmann::json{{"id", id}, {"model", model.generic_string()}};
    Require(bool(out), "write controlled held item definition");
  }
  auto items = std::make_shared<UItemDefinitionStorage>();
  items->Load(root.string());
  struct RestoreItems
  {
    const UItemDefinitionStorage *previous = WornEquipmentDrawer::ItemDefinitions();
    ~RestoreItems() { WornEquipmentDrawer::SetItemDefinitions(previous); }
  } restoreItems;
  WornEquipmentDrawer::SetItemDefinitions(items.get());

  const UGlStateScope state(kGlMaskFpViewmodel3D);
  PreviewRenderTarget target;
  Require(target.Initialize(96), "initialize a small real GL draw target");
  auto shaders = std::make_shared<UShaderManager>();
  auto shader = shaders->CreateShader("actor_gpu_fixture", "shaders/vshader.glsl", "shaders/fshader.glsl");
  Require(shader && shader->IsValid(), "load actual legacy mesh shader");
  UCreatureDrawPass worldDraw;
  auto &cache = CreatureMeshGpuCache::Instance();
  struct Retire
  {
    CreatureMeshGpuCache &cache;
    UCreatureDrawPass &pass;
    ~Retire() { pass.DestroyBuffers(); cache.DestroyAll(); }
  } cleanup{cache, worldDraw};
  cache.DestroyAll();
  Require(worldDraw.InitBuffers(*shaders), "initialize actual actor part and palette stores");
  const auto asset = FindItemSurfaceAsset(bucket, UItemDefinitionStorage::ContentRevision());
  Require(bool(asset), "load an authored held item from the live assets");
  size_t expectedVertices{}, expectedIndices{};
  for (const auto &primitive : asset->Primitives)
  {
    expectedVertices += primitive.Vertices.size() * sizeof(float);
    expectedIndices += primitive.Indices.size() * sizeof(unsigned);
  }
  const auto bytes = [](const TerrainGpuBufferSnapshot &s, Role role) {
    return s.roles[static_cast<size_t>(role)].bytes;
  };
  const auto worldBuffers = [&] {
    TerrainGpuBufferCensus out;
    worldDraw.InspectGpuBuffers(out);
    InspectCreatureBonePaletteGpu(out);
    return out;
  };
  const auto cold = worldBuffers().Read();
  Require(bytes(cold, Role::CreaturePartVertices) == 1440 && bytes(cold, Role::CreaturePartIndices) == 432 &&
              bytes(cold, Role::CreaturePalette) == 6144, "measure the real part buffers and shared palette before actor draw");
  glBindFramebuffer(GL_FRAMEBUFFER, target.Framebuffer);
  glViewport(0, 0, 96, 96);
  ItemDrawRequest request;
  request.Asset = asset;
  request.Path = bucket;
  request.State = EvaluateItemVisualState(*asset, {});
  worldDraw.DrawItemSurface(request, world);
  const auto held = worldBuffers();
  const auto drawn = held.Read();
  Require(bytes(drawn, Role::WorldHeldSurfaceVertices) == expectedVertices &&
              bytes(drawn, Role::WorldHeldSurfaceIndices) == expectedIndices && expectedVertices && expectedIndices,
          "world held-item renderer reports both stores from the exact authored item");

  const auto equip = [&](const char *id) {
    WornEquipmentDrawer::SubmitWieldedPreview(nullptr, shader.get(), id, "", items.get(), glm::mat4(1.f),
        glm::mat4(1.f), [](const std::string &, glm::mat4 &matrix) { matrix = glm::mat4(1.f); return true; }, true);
  };
  equip("actor_fixture_triangle");
  const auto equipment = [&] {
    TerrainGpuBufferCensus out;
    cache.InspectGpuBuffers(out);
    return out;
  }();
  const auto equipTriangle = equipment.Read();
  Require(equipTriangle.total.objects == 2 && bytes(equipTriangle, Role::EquipmentScratchVertices) == 60 &&
              bytes(equipTriangle, Role::EquipmentScratchIndices) == 12, "actual legacy equipment draw uses the shared stream");
  const auto sourceBindings = shared_content_preview_fixture::ReadCallerState();
  equip("actor_fixture_quad");
  const auto equipQuad = equipment.Read();
  Require(equipQuad.total.objects == 2 && bytes(equipQuad, Role::EquipmentScratchVertices) == 80 &&
              bytes(equipQuad, Role::EquipmentScratchIndices) == 24 &&
              shared_content_preview_fixture::ReadCallerState().array == sourceBindings.array,
          "larger legacy equipment reuses the same names while preserving the caller array binding");
  {
    auto &refused = preview_gpu_storage_fixture::RefuseStreamUpload::refused;
    refused = false;
    preview_gpu_storage_fixture::RefuseStreamUpload reject(GL_ELEMENT_ARRAY_BUFFER);
    GLuint query{};
    glGenQueries(1, &query);
    glBeginQuery(GL_PRIMITIVES_GENERATED, query);
    equip("actor_fixture_quad");
    glEndQuery(GL_PRIMITIVES_GENERATED);
    GLuint generated{};
    glGetQueryObjectuiv(query, GL_QUERY_RESULT, &generated);
    glDeleteQueries(1, &query);
    Require(refused && !generated && equipment.Read().total.bytes == equipQuad.total.bytes,
            "a real rejected same-size equipment upload issues no stale draw");
  }
  equip("actor_fixture_triangle");
  Require(equipment.Read().total.bytes == equipTriangle.total.bytes && glGetError() == GL_NO_ERROR,
          "the shared stream recovers after rejection");

  UFpViewmodelRenderer first(items, {}, {}, {}, shaders);
  UFpViewmodelRenderer second(items, {}, {}, {}, shaders);
  Require(first.Initialize() && second.Initialize(), "initialize two real first-person owners");
  const auto draw = [&](UFpViewmodelRenderer &renderer, const char *id) {
    InventoryEntryRef entry;
    entry.empty = false;
    entry.kind = InventoryEntryKind::Item;
    entry.count = 1;
    entry.Id = id;
    FpViewmodelDrawParams params;
    params.Active = &entry;
    params.FramebufferW = params.FramebufferH = 96;
    params.TargetFramebuffer = target.Framebuffer;
    renderer.DrawWorldOverlay(params);
  };
  const auto viewmodel = [](UFpViewmodelRenderer &renderer) {
    TerrainGpuBufferCensus out;
    renderer.InspectActorGpuBuffers(out);
    return out;
  };
  draw(first, "actor_fixture_triangle");
  draw(second, "actor_fixture_quad");
  const auto firstNames = viewmodel(first);
  const auto secondNames = viewmodel(second);
  const auto firstLive = firstNames.Read();
  const auto secondLive = secondNames.Read();
  Require(bytes(firstLive, Role::ViewmodelScratchVertices) == 60 && bytes(firstLive, Role::ViewmodelScratchIndices) == 12 &&
              bytes(secondLive, Role::ViewmodelScratchVertices) == 80 && bytes(secondLive, Role::ViewmodelScratchIndices) == 24,
          "two viewmodels own separate legacy geometry stores");
  draw(first, "bucket");
  const auto firstWithSurfaceNames = viewmodel(first);
  const auto firstWithSurface = firstWithSurfaceNames.Read();
  Require(bytes(firstWithSurface, Role::ViewmodelSurfaceVertices) == expectedVertices &&
              bytes(firstWithSurface, Role::ViewmodelSurfaceIndices) == expectedIndices,
          "first-person authored buffers match the same independent asset layout");
  first.Shutdown();
  Require(firstWithSurfaceNames.Read().namesWithoutObject == firstWithSurface.total.objects &&
              secondNames.Read().total.bytes == secondLive.total.bytes, "closing one viewmodel retires only its own names");
  cache.DestroyAll();
  Require(equipment.Read().namesWithoutObject == 2 && secondNames.Read().total.bytes == secondLive.total.bytes,
          "shared equipment retirement leaves a separate viewmodel alive");
  second.Shutdown();
  worldDraw.DestroyBuffers();
  const auto retired = held.Read();
  Require(secondNames.Read().namesWithoutObject == secondLive.total.objects &&
              retired.namesWithoutObject == drawn.total.objects && !worldBuffers().Read().references &&
              glGetError() == GL_NO_ERROR, "world teardown retires held surfaces, part meshes and the palette");

  return {{"status", "PASS"}, {"cold_world", DescribeActorGpuBuffers(cold)}, {"world_held", DescribeActorGpuBuffers(drawn)},
          {"equipment_triangle", DescribeActorGpuBuffers(equipTriangle)}, {"equipment_quad", DescribeActorGpuBuffers(equipQuad)},
          {"first_viewmodel", DescribeActorGpuBuffers(firstWithSurface)}, {"second_viewmodel", DescribeActorGpuBuffers(secondLive)},
          {"authored_extents", {expectedVertices, expectedIndices}}, {"retired_world_names", retired.namesWithoutObject},
          {"same_size_refusal_retried", true}, {"independent_viewmodel_retirement", true}};
}
} // namespace cutum::actor_gpu_storage_fixture

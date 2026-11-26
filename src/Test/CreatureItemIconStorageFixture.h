#pragma once

#include "App/Platform/DesktopPlatformPaths.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Gui/Cache/CreatureItemIconStorage.h"
#include "Gui/Cache/ItemIconFingerprint.h"
#include "Gui/Core/GuiIconSource.h"
#include "Gui/Preview/CreaturePreviewRenderer.h"
#include "Gui/Preview/ItemPreviewRenderer.h"
#include "Test/SharedContentPreviewStorageFixture.h"
#include "ThirdParty/stb_image_write.h"

namespace cutum::creature_item_icon_fixture
{
using gui_context_fixture::Require;
using shared_content_preview_fixture::ReadCallerState;
using shared_content_preview_fixture::RefuseFramebuffer;
using shared_content_preview_fixture::RefuseIndexUpload;
using Json = nlohmann::json;

struct PlatformFixture
{
  const bool Installed = !IUPlatformPaths::TryGet();
  PlatformFixture()
  {
    if (Installed)
      IUPlatformPaths::SetGlobal(std::make_shared<UDesktopPlatformPaths>());
  }
  ~PlatformFixture()
  {
    if (Installed)
      IUPlatformPaths::SetGlobal({});
  }
};

inline void CheckStagedTarget(const PreviewRendererStorage &before, const PreviewRendererStorage &after)
{
  Require(after.supported && before.framebuffer == after.framebuffer && before.colorTexture == after.colorTexture &&
              before.depthBuffer == after.depthBuffer && before.targetTexels == after.targetTexels &&
              glIsFramebuffer(before.framebuffer) && glIsTexture(before.colorTexture) && glIsRenderbuffer(before.depthBuffer),
          "refused preview replacement preserves the original target");
  const auto candidate = RefuseFramebuffer::Observed;
  Require(candidate[0] && candidate[1] && candidate[2] && !glIsFramebuffer(candidate[0]) &&
              !glIsTexture(candidate[1]) && !glIsRenderbuffer(candidate[2]),
          "refused preview candidate retires all three names");
}

inline Json Run(const std::filesystem::path &report)
{
  PlatformFixture platform;
  auto root = report;
  root += ".entity-icons";
  Require(std::filesystem::create_directory(root), "create isolated creature and item icon fixture");
  const std::string skinId = "borrowed_skin_diagnostic_long_identity";
  const std::string itemId = "bucket_icon_fixture_long_item_identity";
  constexpr auto model = "models/items/bucket/model.gltf";
  const auto files = root / "items";
  const auto skinsRoot = root / "skins";
  const auto skinFiles = skinsRoot / skinId / "textures";
  std::filesystem::create_directories(files);
  std::filesystem::create_directories(skinFiles);
  {
    std::ofstream file(files / "bucket.json");
    file << Json{{"id", itemId}, {"model", model}};
    Require(bool(file), "write isolated item definition");
  }
  constexpr unsigned char pixels[]{19, 71, 137, 255};
  Require(stbi_write_png((skinFiles / "diffuse.png").string().c_str(), 1, 1, 4, pixels, 4), "write test skin pixel");
  auto items = std::make_shared<UItemDefinitionStorage>();
  auto species = std::make_shared<UCreatureDefinitionStorage>();
  auto skins = std::make_shared<USkinDefinitionStorage>();
  auto textures = std::make_shared<UCreatureTextureStorage>();
  auto shaders = std::make_shared<UShaderManager>();
  items->Load(files.string());
  textures->MergeSkinRoot(skinsRoot.string());
  const auto skin = textures->GetTexture("skin/" + skinId + "/diffuse");
  Require(skin && glIsTexture(skin), "test skin has a real texture at its source");
  const auto assetRoot = IUPlatformPaths::Current().AssetRoot();
  const auto asset = FindItemSurfaceAsset(assetRoot / model, UItemDefinitionStorage::ContentRevision());
  Require(asset && std::any_of(asset->Bindings.begin(), asset->Bindings.end(), [](const auto &binding) {
    return binding.Channel == ItemVisualChannel::FillUnits && binding.Maximum >= 1.f;
  }), "fixture uses the live container surface with a fill channel");
  InventoryEntryRef filled;
  filled.Id = itemId;
  filled.contentsId = "water";
  filled.contentsUnits = 1;

  auto service = UInventoryIconService::Create();
  Require(service->Initialize(root / "disk"), "initialize isolated disk icon service");
  const auto fingerprint = BuildItemIconFingerprint(assetRoot, model, items->Get(itemId)->Visual.IconScale);
  {
    UCreaturePreviewRenderer painter(species, skins, textures, shaders);
    UGlTextureOwner seed(painter.CreateSolidColorTexture(64, .1f, .2f, .5f, 1.f));
    Require(seed.Get() && service->StoreIconTexture("item", itemId, "water:1", fingerprint, 64, seed.Get()),
            "seed the production item state disk path");
    glFinish();
    service->FlushManifest();
    Require(service->InspectOwnedStorage().jobs == 0 && service->InspectOwnedStorage().entries == 1,
            "seed completes before retained allocation observation");
  }

  size_t coldBytes = 0, pendingBytes = 0, publishedBytes = 0, attempts = 0;
  const auto inspect = [&](const UGuiIconSource *source, const UContentPreviewRenderer *content,
                           const std::shared_ptr<UCreaturePreviewRenderer> &creature)
  {
    return UGuiIconSource::InspectCreatureItemIconStorage(source, content, creature,
        species.get(), skins.get(), textures.get(), items.get(), shaders.get());
  };
  {
    observation::Scope epoch;
    {
      auto creature = UCreaturePreviewRenderer::Create(species, skins, textures, shaders);
      auto item = UItemPreviewRenderer::Create(items, shaders);
      UContentPreviewRenderer content({}, {}, {}, shaders, creature, item);
      UGuiIconSource source({}, {}, std::make_unique<UCreatureIconCache>(creature, service),
                           std::make_unique<UItemIconCache>(items, service, item));
      const auto cold = inspect(&source, &content, creature);
      Require(cold.supported && cold.cpuBytes == observation::State.live &&
                  cold.creaturePreviewAliases == 3 && cold.itemPreviewAliases == 2,
              "cold cache and shared renderer requests match independent observation");
      {
        const std::shared_ptr<UCreaturePreviewRenderer> foreign(creature.get(), [](auto *) {});
        Require(!inspect(&source, &content, foreign).bindingsMatch, "equal addresses with distinct control blocks fail the join");
      }
      coldBytes = cold.cpuBytes;
      const auto begin = observation::State.attempts;
      Require(!source.GetItemStateIcon(filled), "first state icon request queues work");
      attempts = observation::State.attempts - begin;
      Require(!source.GetItemStateIcon(filled), "duplicate state joins the queued work");
      const auto pending = inspect(&source, &content, creature);
      Require(pending.supported && pending.cpuBytes == observation::State.live && pending.item.pending == 1 &&
                  pending.item.stateRequests == 1 && pending.item.stateBytes > cold.item.stateBytes,
              "captured fill state and its FIFO name are charged once");
      pendingBytes = pending.cpuBytes;
      Require(source.GetSkinIconTexture(skinId) == skin, "skin cache uses the existing atlas texture");
      source.WarmupItemIcons(1);
      const auto icon = source.GetItemStateIcon(filled);
      const auto published = inspect(&source, &content, creature);
      Require(published.supported && published.cpuBytes == observation::State.live && published.item.icons.owned == 1 &&
                  published.item.fingerprints == 1 && !published.item.pending && !published.item.stateRequests &&
                  published.creature.skins.borrowed == 1 && !published.creature.skins.ownedTexels && icon && glIsTexture(icon),
              "completed disk hit retires state input and counts the fingerprint and owned texture");
      publishedBytes = published.cpuBytes;
      source.ClearItemIconCache();
      source.ClearCreatureIconCache();
      const auto cleared = inspect(&source, &content, creature);
      Require(cleared.supported && cleared.cpuBytes == observation::State.live && !cleared.item.fingerprints &&
                  !cleared.item.icons.owned && !cleared.creature.skins.borrowed && !glIsTexture(icon) && glIsTexture(skin),
              "invalidation retires owned icons and preserves borrowed skin storage");
    }
    Require(!observation::State.live && !observation::State.blocks, "observed cache and renderer requests all retire");
  }

  for (size_t cutoff = 0; cutoff < attempts; ++cutoff)
  {
    observation::Scope epoch;
    {
      UItemIconCache cache(items);
      observation::State.fail_after = observation::State.attempts + cutoff;
      bool refused = false;
      try { cache.GetIcon(filled); }
      catch (const std::bad_alloc &) { refused = true; }
      observation::State.fail_after = SIZE_MAX;
      const auto after = cache.InspectOwnedStorage();
      Require(refused && after.supported && !after.pending && !after.stateRequests &&
                  after.cpuBytes == sizeof(cache) + observation::State.live,
              "refused state admission retires the candidate without separating state from its queue");
      Require(!cache.GetIcon(filled) && cache.PendingWarmupCount() == 1, "same item state request retries successfully");
    }
    Require(!observation::State.live && !observation::State.blocks, "refused state fixture releases cache backing");
  }

  auto creature = UCreaturePreviewRenderer::Create(species, skins, textures, shaders);
  auto item = UItemPreviewRenderer::Create(items, shaders);
  Require(creature->Initialize() && item->Initialize(), "initialize both real preview renderers");
  const auto itemStorage = [&] { return item->InspectOwnedStorage(items.get(), shaders.get()); };
  const auto creatureStorage = [&] { return creature->InspectOwnedStorage(species.get(), skins.get(), textures.get(), shaders.get()); };
  const auto itemBefore = itemStorage();
  const auto creatureBefore = creatureStorage();
  const auto caller = ReadCallerState();
  {
    RefuseFramebuffer refuse;
    Require(!item->RenderToUniqueTexture(itemId, 128, 45.f, 20.f), "refuse replacement of the item target");
  }
  CheckStagedTarget(itemBefore, itemStorage());
  Require(ReadCallerState() == caller, "refused item target preserves caller bindings");
  {
    RefuseFramebuffer refuse;
    Require(!creature->Render("fixture_species", "", 128, 45.f, 20.f), "refuse replacement of the creature target");
  }
  CheckStagedTarget(creatureBefore, creatureStorage());
  Require(ReadCallerState() == caller, "refused creature target preserves caller bindings");

  UGlTextureOwner modelIcon(item->RenderToUniqueTexture(itemId, 128, 45.f, 20.f, MakeItemVisualInputs(&filled)));
  const auto uploaded = itemStorage();
  Require(modelIcon.Get() && uploaded.targetTexels == 128 * 128 && uploaded.colorTexture != itemBefore.colorTexture &&
              !glIsFramebuffer(itemBefore.framebuffer) && !glIsRenderbuffer(itemBefore.depthBuffer) &&
              (!glIsTexture(itemBefore.colorTexture) || modelIcon.Get() == itemBefore.colorTexture),
          "successful item resize retires the old target and transfers the separate output");
  size_t vertexBytes = 0, indexBytes = 0;
  for (const auto &primitive : asset->Primitives)
  {
    vertexBytes += primitive.Vertices.size() * sizeof(float);
    indexBytes += primitive.Indices.size() * sizeof(unsigned);
  }
  Require(uploaded.surface.assets == 1 && vertexBytes && indexBytes &&
              uploaded.surface.vertexBytes == vertexBytes && uploaded.surface.indexBytes == indexBytes,
          "item preview accounts the actual model upload against the independent asset layout");

  for (const std::string &name : {"wood_one", "wood_two", "leather_fixture", "iron_fixture", "stone_fixture", "gold_fixture", "diamond_fixture"})
  {
    UGlTextureOwner icon(item->RenderToUniqueTexture(name, 64, 45.f, 20.f));
    Require(icon.Get() != 0, "render each existing color through the item icon path");
  }
  Require(itemStorage().fallbackColorTexels == 6 && itemStorage().resolveFramebuffer,
          "different item identities reuse the six fallback colors");
  const auto lastItem = itemStorage();
  item->Invalidate();
  creature->Invalidate();
  const auto itemAfter = itemStorage();
  const auto creatureAfter = creatureStorage();
  Require(itemAfter.supported && creatureAfter.supported && !itemAfter.surface.assets && !itemAfter.targetTexels &&
              !itemAfter.resolveFramebuffer && !itemAfter.fallbackColorTexels && !creatureAfter.targetTexels &&
              !glIsFramebuffer(lastItem.framebuffer) && !glIsFramebuffer(lastItem.resolveFramebuffer) &&
              !glIsTexture(creatureBefore.colorTexture) && glIsTexture(modelIcon.Get()) && glIsTexture(skin),
          "preview invalidation frees retained attachments while transferred and borrowed outputs remain live");

  {
    ItemSurfaceRenderer surface;
    Require(surface.Initialize(*shaders), "initialize reusable surface shader");
    const auto before = ReadCallerState();
    {
      RefuseIndexUpload refuse;
      Require(!surface.Draw(asset, assetRoot / model, glm::mat4(1.f), glm::mat4(1.f)), "refuse GL index upload");
    }
    const auto rejected = RefuseIndexUpload::Observed;
    Require(!surface.InspectOwnedStorage().assets && rejected[0] && rejected[1] && rejected[2] &&
                !glIsVertexArray(rejected[0]) && !glIsBuffer(rejected[1]) && !glIsBuffer(rejected[2]) &&
                ReadCallerState() == before, "refused upload retires provisional names and preserves caller state");
  }
  textures->LoadFromCreatureAndSkinRoots((root / "absent").string(), {});
  return {{"status", "PASS"}, {"cold_cpu_bytes", coldBytes}, {"pending_cpu_bytes", pendingBytes},
          {"published_cpu_bytes", publishedBytes}, {"state_refusals", attempts},
          {"fallback_colors", lastItem.fallbackColorTexels}, {"item_surface_vertex_bytes", uploaded.surface.vertexBytes},
          {"item_surface_index_bytes", uploaded.surface.indexBytes}, {"staged_targets", true},
          {"retired_failed_upload", true}, {"borrowed_skin_preserved", true}};
}

} // namespace cutum::creature_item_icon_fixture

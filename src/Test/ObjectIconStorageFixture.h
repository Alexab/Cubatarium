#pragma once

#include "Gui/Core/GuiIconSource.h"
#include "Test/SharedContentPreviewStorageFixture.h"

namespace cutum::object_icon_storage_fixture
{
using gui_context_fixture::Require;
using shared_content_preview_fixture::ReadCallerState;
using shared_content_preview_fixture::RefuseFramebuffer;
using shared_content_preview_fixture::RefuseIndexUpload;
using Json = nlohmann::json;

inline Json Run(const std::filesystem::path &reportPath)
{
  auto fixture = reportPath;
  fixture += ".object-icons";
  Require(std::filesystem::create_directory(fixture), "create isolated icon fixture folder");
  const std::string blockName = "icon_fixture_block_with_owned_name";
  const std::string objectName = "icon_fixture_object_with_owned_name";
  {
    std::ofstream file(fixture / (objectName + ".json"));
    const Json voxel{{"dx", 0}, {"dy", 0}, {"dz", 0}, {"type", blockName}};
    file << Json{{"name", objectName}, {"blocks", Json::array({voxel})}};
    Require(bool(file), "write isolated icon object data");
  }
  auto bases = std::make_shared<UTextureBaseStorage>();
  auto textures = std::make_shared<UTextureCubeStorage>(bases);
  auto blocks = std::make_shared<UBlockDefinitionStorage>();
  auto objects = std::make_shared<UObjectLibrary>();
  auto shaders = std::make_shared<UShaderManager>();
  constexpr BlockId id = 77;
  BlockDefinition block;
  block.Id = id;
  block.Name = blockName;
  blocks->ReplaceAll({{id, block}}, {{blockName, id}});
  bases->RegisterPixels(blockName, {1, 1, {30, 83, 176, 255}});
  MergedCubeDesc cube;
  cube.Id = id;
  cube.Name = blockName;
  cube.Stems.fill(blockName);
  textures->BuildFromDescriptors({cube});
  const GLuint atlas = textures->GetTextures().at(id).GetTexture();
  UBlockRegistry registry(textures, blocks);
  objects->Load(fixture.string(), registry);
  Require(atlas && glIsTexture(atlas) && objects->GetShared(objectName), "icon fixture uses real atlas and object inputs");

  const auto inspect = [&](const UGuiIconSource &source)
  {
    const auto storage = source.InspectObjectIconStorage(objects.get(), textures.get(), blocks.get(), shaders.get());
    Require(storage.supported && storage.bindingsMatch && storage.cpuBytes == storage.sourceBytes +
                storage.cacheBytes + storage.objectTableBytes + storage.blockTableBytes +
                storage.objectQueueBytes + storage.blockQueueBytes,
            "icon source census charges every retained portion once");
    return storage;
  };
  size_t fallbackBytes = 0, coldBytes = 0, queuedBytes = 0;
  {
    observation::Scope epoch;
    auto source = std::make_unique<UGuiIconSource>(textures, nullptr);
    const auto fallback = inspect(*source);
    Require(!fallback.present && fallback.atlasFallback && !fallback.cacheBytes && fallback.cpuBytes == observation::State.live &&
                source->GetBlockIconTexture(blockName) == atlas, "atlas fallback charges only its own source shell");
    fallbackBytes = fallback.cpuBytes;
    source.reset();
    Require(!observation::State.live && glIsTexture(atlas), "fallback retirement preserves borrowed atlas");
  }
  {
    observation::Scope epoch;
    auto source = std::make_unique<UGuiIconSource>(textures,
        std::make_unique<UObjectIconCache>(objects, textures, blocks, shaders));
    const auto cold = inspect(*source);
    Require(cold.present && !cold.atlasFallback && !cold.objectIcons && !cold.framebuffer &&
                cold.cpuBytes == observation::State.live, "cold cache census agrees with independent retained allocations");
    source->QueueBlockIcon(blockName);
    const auto requests = observation::State.attempts;
    source->QueueBlockIcon(blockName);
    const auto queued = inspect(*source);
    Require(queued.pendingBlocks == 1 && queued.cpuBytes == observation::State.live &&
                queued.blockQueueBytes > cold.blockQueueBytes && observation::State.attempts == requests,
            "one owned pending name is charged and duplicate enqueue allocates nothing");
    Require(!source->InspectObjectIconStorage(objects.get(), nullptr, blocks.get(), shaders.get()).supported,
            "foreign atlas rejects the source join");
    coldBytes = cold.cpuBytes;
    queuedBytes = queued.cpuBytes;
    source.reset();
    Require(!observation::State.live && !observation::State.blocks && glIsTexture(atlas),
            "cold queue and owner storage retires while atlas stays live");
  }

  const auto original = ReadCallerState();
  UGlStateScope restore(kGlMaskIconFbo | GlStateBit::DepthMask);
  GLuint unpack = 0;
  glGenBuffers(1, &unpack);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpack);
  glBufferData(GL_PIXEL_UNPACK_BUFFER, 4, nullptr, GL_STATIC_DRAW);
  glDepthMask(GL_FALSE);
  glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
  const auto caller = ReadCallerState();

  UGuiIconSource source(textures, std::make_unique<UObjectIconCache>(objects, textures, blocks, shaders));
  auto &cache = source.GetObjectCache();
  Require(cache.Initialize() && ReadCallerState() == caller, "icon cache initializes without changing caller state");
  const auto ready = inspect(source);
  Require(ready.pendingObjects == 1 && ready.vertexBytes == 480 && ready.indexBytes == 144 && ready.targetTexels == 4096 &&
              ready.depthSamples == ready.targetTexels && glIsFramebuffer(ready.framebuffer) &&
              glIsVertexArray(ready.vertexArray) && glIsTexture(ready.colorTexture) && glIsRenderbuffer(ready.depthBuffer),
          "icon cache reports the live cube and persistent target");
  const auto firstBlock = source.GetBlockIconTexture(blockName);
  Require(firstBlock && firstBlock != atlas && glIsTexture(firstBlock) && ReadCallerState() == caller,
          "real block icon owns an independent texture and preserves unpack and clear state");
  source.ClearBlockIconCache();
  Require(!glIsTexture(firstBlock) && glIsTexture(atlas), "block invalidation retires only the cached icon");

  {
    RefuseFramebuffer observe(false);
    observation::Scope refused(0);
    bool failed = false;
    try { cache.WarmupNext(1); }
    catch (const std::bad_alloc &) { failed = true; }
    observation::State.fail_after = SIZE_MAX;
    const auto discarded = RefuseFramebuffer::Observed;
    Require(failed && discarded[0] == ready.framebuffer && discarded[1] && discarded[1] != ready.colorTexture &&
                !glIsTexture(discarded[1]) && !observation::State.live && inspect(source).pendingObjects == 1 &&
                !source.GetObjectIconTextureIfCached(objectName) && ReadCallerState() == caller,
            "refused object table publication retires the candidate and preserves FIFO retry");
  }

  const auto beforeEntries = inspect(source);
  GLuint objectIcon = 0, blockIcon = 0;
  size_t entryBytes = 0;
  {
    observation::Scope epoch;
    objectIcon = source.GetObjectIconTexture(objectName);
    blockIcon = source.GetBlockIconTexture(blockName);
    const auto contents = inspect(source);
    entryBytes = contents.cpuBytes - beforeEntries.cpuBytes;
    Require(objectIcon && blockIcon && objectIcon != blockIcon && objectIcon != atlas && blockIcon != atlas &&
                contents.objectIcons == 1 && contents.blockIcons == 1 && contents.cachedTexels == 8192 &&
                entryBytes == observation::State.live && ReadCallerState() == caller,
            "live cache entries match independent ordinary allocations and two owned textures");
    cache.WarmupNext(1);
    Require(inspect(source).pendingObjects == 0 && source.GetObjectIconTextureIfCached(objectName) == objectIcon,
            "successful retry retires only the pending name");
    source.ClearBlockIconCache();
    Require(!glIsTexture(blockIcon) && glIsTexture(objectIcon) && glIsTexture(atlas),
            "block invalidation preserves the object icon and atlas");
    const auto current = inspect(source);
    {
      RefuseFramebuffer refuse;
      Require(!cache.Initialize(), "refuse replacement framebuffer");
    }
    const auto fbo = RefuseFramebuffer::Observed;
    Require(fbo[0] && fbo[0] != current.framebuffer && fbo[1] && fbo[2] && !glIsFramebuffer(fbo[0]) &&
                !glIsTexture(fbo[1]) && !glIsRenderbuffer(fbo[2]), "refused target names retire");
    {
      RefuseFramebuffer observe(false);
      RefuseIndexUpload refuse;
      Require(!cache.Initialize(), "refuse mesh after preparing its target");
    }
    const auto target = RefuseFramebuffer::Observed;
    const auto mesh = RefuseIndexUpload::Observed;
    Require(target[0] && target[1] && target[2] && !glIsFramebuffer(target[0]) && !glIsTexture(target[1]) &&
                !glIsRenderbuffer(target[2]) && mesh[0] && mesh[1] && mesh[2] && !glIsVertexArray(mesh[0]) &&
                !glIsBuffer(mesh[1]) && !glIsBuffer(mesh[2]) && inspect(source).cpuBytes == current.cpuBytes &&
                inspect(source).framebuffer == current.framebuffer && glIsTexture(objectIcon) && ReadCallerState() == caller,
            "refused initialization preserves the active owner while retiring staged GL resources");
    cache.Shutdown();
    Require(!observation::State.live && !observation::State.blocks,
            "all observed cache entry allocations retire after invalidation and shutdown");
  }

  const auto retired = inspect(source);
  Require(!retired.objectIcons && !retired.blockIcons && !retired.cachedTexels && !retired.targetTexels &&
              !glIsTexture(objectIcon) && !glIsFramebuffer(ready.framebuffer) && !glIsTexture(ready.colorTexture) &&
              !glIsRenderbuffer(ready.depthBuffer) && !glIsVertexArray(ready.vertexArray) &&
              !glIsBuffer(ready.vertexBuffer) && !glIsBuffer(ready.indexBuffer) && glIsTexture(atlas) &&
              ReadCallerState() == caller && glGetError() == GL_NO_ERROR,
          "shutdown retires all owned names and preserves the caller and source atlas");
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, GLuint(original.unpack));
  glDeleteBuffers(1, &unpack);
  glClearColor(original.clear[0], original.clear[1], original.clear[2], original.clear[3]);
  UObjectLibrary::ReleaseThreadRead();
  textures->Clear();
  return {{"status", "PASS"}, {"fallback_bytes", fallbackBytes}, {"cold_bytes", coldBytes},
          {"cold_queue_bytes", queuedBytes}, {"entry_bytes", entryBytes}, {"object_cache_bytes", ready.cacheBytes},
          {"vertex_bytes", ready.vertexBytes}, {"index_bytes", ready.indexBytes}, {"target_texels", ready.targetTexels},
          {"depth_samples", ready.depthSamples}, {"duplicate_allocate0", true}, {"retained_queue_retry", true},
          {"failed_target_and_mesh_retired", true}, {"atlas_fallback_preserved", true}, {"block_invalidation", true},
          {"shutdown_retires_owned_gl", true}, {"caller_state_restored", true}};
}
} // namespace cutum::object_icon_storage_fixture

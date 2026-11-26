#pragma once

#include "Gui/Core/GuiIconSource.h"
#include "Render/Engine/TerrainGpuBufferReport.h"
#include "Test/IconLifecycleFixture.h"
#include <fstream>
#include <iterator>
#ifdef _WIN32
#include <windows.h>
#endif

namespace cutum::inventory_icon_storage_fixture
{
using gui_context_fixture::Require;
using Json = nlohmann::json;

inline std::string ReadFile(const std::filesystem::path &path)
{
  std::ifstream file(path, std::ios::binary);
  Require(bool(file), "read published icon manifest");
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

struct RejectMappedPackRead
{
  RejectMappedPackRead()
  {
    Original = __glewBufferData;
    Buffer = 0;
    Mapped = false;
    __glewBufferData = Allocate;
  }
  ~RejectMappedPackRead() { __glewBufferData = Original; }
  inline static GLuint Buffer{};
  inline static bool Mapped{};
private:
  inline static PFNGLBUFFERDATAPROC Original{};
  static void GLAPIENTRY Allocate(GLenum target, GLsizeiptr bytes, const void *data, GLenum usage)
  {
    Original(target, bytes, data, usage);
    if (target == GL_PIXEL_PACK_BUFFER && bytes > 0)
    {
      GLint current{};
      glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &current);
      Buffer = static_cast<GLuint>(current);
      Mapped = glMapBufferRange(target, 0, bytes, GL_MAP_READ_BIT) != nullptr;
    }
  }
};

inline Json CheckReadbackGpu(const std::filesystem::path &root, GLuint sourceTexture)
{
  constexpr int side = icon_lifecycle_fixture::Size;
  auto service = UInventoryIconService::Create();
  Require(service->Initialize(root), "initialize a separate GPU readback fixture");
  const auto initial = service->InspectOwnedStorage();
  Require(initial.jobLimit == 256 && initial.readbackSideLimit == 512 &&
              initial.packBufferBytesUpper == 268435456, "the admission bounds match the icon queue and extent limits");
  const auto store = [&](std::string id, int size, GLuint image) {
    return service->StoreIconTexture("item", id, "", "readback-fixture", size, image);
  };
  const auto census = [&] {
    TerrainGpuBufferCensus out;
    service->InspectReadbackGpuBuffers(out);
    return out;
  };
  const auto state = icon_lifecycle_fixture::ReadBindings();
  {
    RejectMappedPackRead refuse;
    Require(!store("refused", side, sourceTexture) && RejectMappedPackRead::Mapped,
            "the real GL read into a mapped pack buffer is rejected");
  }
  Require(RejectMappedPackRead::Buffer && !glIsBuffer(RejectMappedPackRead::Buffer) &&
              !service->InspectOwnedStorage().jobs && icon_lifecycle_fixture::ReadBindings() == state &&
              glGetError() == GL_NO_ERROR, "failed read retires its store and restores caller pack state");
  Require(!store("wrong-extent", side + 1, sourceTexture) &&
              !service->StoreIconTexture("block", "wrong-extent", "", "readback-fixture", side - 1, sourceTexture) &&
              !service->InspectOwnedStorage().entries, "both read routes reject a texture extent mismatch");

  UGlTextureOwner large;
  {
    UGlStateScope glScope(kGlMaskIconFbo);
    GLuint name{};
    glGenTextures(1, &name);
    large = UGlTextureOwner(name);
    glBindTexture(GL_TEXTURE_2D, name);
    const UGlPixelUnpackScope unpack;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 512, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  }
  Require(store("large", 512, large.Get()), "admit the largest supported single pack store");
  for (size_t i = 1; i < initial.jobLimit; ++i)
    Require(store("queued-" + std::to_string(i), side, sourceTexture), "admit every supported pack job slot");
  const auto fullCensus = census();
  const auto full = fullCensus.Read();
  const auto source = service->InspectOwnedStorage();
  Require(full.total.objects == 256 && full.total.bytes == 1309696 && !full.failedQueries &&
              source.jobs == full.total.objects && source.packBufferBytes == full.total.bytes &&
              !store("overflow", side, sourceTexture) && census().Read().total.bytes == full.total.bytes &&
              icon_lifecycle_fixture::ReadBindings() == state, "queue saturation confirms measured live bytes and rejects one more store");
  service->CancelPendingItemReadbacks();
  const auto cancelled = fullCensus.Read();
  Require(!census().Read().references && cancelled.namesWithoutObject == 256 &&
              !cancelled.total.bytes && glIsTexture(large.Get()), "cancellation retires all pack stores and keeps source textures alive");

  for (const char *id : {"retry-a", "retry-b", "retry-c"})
    Require(store(id, side, sourceTexture), "valid readback retries on the same service");
  const auto retryCensus = census();
  const auto queued = retryCensus.Read();
  glFinish();
  service->FlushManifest();
  const auto batch = census().Read();
  Require(queued.total.objects == 3 && queued.total.bytes == 3072 &&
              batch.total.objects == 1 && batch.total.bytes == 1024 &&
              retryCensus.Read().namesWithoutObject == 2, "one flush retires exactly two completed stores");
  service->FlushManifest();
  const auto retired = retryCensus.Read();
  const auto manifest = Json::parse(ReadFile(root / "manifest.json"));
  Require(retired.namesWithoutObject == 3 && !census().Read().references &&
              manifest.at("entries").size() == 3 && service->GetStats().RenderStores == 3 &&
              icon_lifecycle_fixture::ReadBindings() == state && glGetError() == GL_NO_ERROR,
          "successful batches persist exactly the retried icons and release the final pack store");

  return {{"status", "PASS"}, {"job_limit", initial.jobLimit}, {"side_limit", initial.readbackSideLimit},
          {"logical_bytes_upper", initial.packBufferBytesUpper}, {"full", DescribeIconReadbackGpuBuffers(full)},
          {"cancelled_names", cancelled.namesWithoutObject}, {"retry", DescribeIconReadbackGpuBuffers(queued)},
          {"after_first_flush", DescribeIconReadbackGpuBuffers(batch)}, {"retired_names", retired.namesWithoutObject},
          {"invalid_read_and_extent_rejected", true}, {"persisted", manifest.at("entries").size()}};
}

inline Json Run(const std::filesystem::path &report)
{
  auto fixture = report;
  fixture += ".icon-service";
  Require(std::filesystem::create_directory(fixture), "create independent icon service test root");
  const auto originalRoot = fixture / "original_cache_with_owned_path_backing";
  const auto nextRoot = fixture / "replacement_cache_with_owned_path_backing";
  const auto invalidRoot = fixture / "invalid_manifest_cache";
  const auto manifest = originalRoot / "manifest.json";
  const auto temporary = originalRoot / "manifest.json.tmp";
  auto texture = icon_lifecycle_fixture::MakeTexture();
  const auto gpu = CheckReadbackGpu(fixture / "gpu-readback", texture.Get());
  constexpr int size = icon_lifecycle_fixture::Size;
  const std::string longId(59, 'q'), fingerprint(63, 'f');
  const auto queue = [&](UInventoryIconService &service, const std::string &id)
  {
    return service.StoreIconTexture("item", id, "owned_fixture_variant", fingerprint, size, texture.Get());
  };
  const auto inspect = [](const UInventoryIconService &service)
  {
    const auto value = service.InspectOwnedStorage();
    Require(value.supported && value.present && value.factoryOwner && value.bindingsMatch &&
                value.cpuBytes == value.serviceRequest + value.readbackStateBytes + value.pathBytes +
                    value.manifestBytes + value.jobBytes, "icon service source components add up exactly once");
    return value;
  };

  InventoryIconStorage cold, idle, busy, loaded;
  {
    observation::Scope epoch;
    auto service = UInventoryIconService::Create();
    cold = inspect(*service);
    Require(!cold.initialized && !cold.pathBytes && cold.cpuBytes == observation::State.live,
            "cold factory coallocation and retained heap equal independent allocations");
    Require(service->Initialize(originalRoot), "initialize measured icon service");
    idle = inspect(*service);
    Require(idle.initialized && idle.pathBytes && !idle.jobs && idle.cpuBytes == observation::State.live,
            "initialized paths and manifest backing match live retained allocations");
    Require(queue(*service, longId), "queue real readback for storage measurement");
    busy = inspect(*service);
    const auto readback = service->InspectReadbackStorage();
    Require(busy.jobs == 1 && busy.packBufferBytes == size_t(size) * size * 4 &&
                busy.jobBytes > idle.jobBytes && busy.cpuBytes == observation::State.live &&
                glIsBuffer(readback.first_buffer), "readback owns exact retained CPU storage and a separate pack buffer");
    service->InvalidateKind("item");
    Require(!glIsBuffer(readback.first_buffer) && inspect(*service).cpuBytes == idle.cpuBytes &&
                observation::State.live == idle.cpuBytes, "item invalidation retires both the CPU record and GL buffer");
    service.reset();
    Require(!observation::State.live && !observation::State.blocks, "all measured factory storage retires");
  }

  auto service = UInventoryIconService::Create();
  Require(service->Initialize(originalRoot), "prepare persisted icon cache");
  const auto store = [&](const std::string &kind, const std::string &id)
  {
    Require(service->StoreIconTexture(kind, id, "", fingerprint, size, texture.Get()), "store real synchronous icon");
  };
  store("block", "fixture_block_icon");
  store("creature", "fixture_creature_icon");
  service->FlushManifest();
  const auto firstManifest = ReadFile(manifest);
  Require(!inspect(*service).dirty && Json::parse(firstManifest)["entries"].size() == 2,
          "flush publishes existing versioned manifest");

  store("skin", "fixture_skin_icon");
#ifdef _WIN32
  {
    const auto lock = CreateFileW(manifest.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(lock != INVALID_HANDLE_VALUE, "hold test manifest open during replacement");
    const std::unique_ptr<void, decltype(&CloseHandle)> handle(lock, CloseHandle);
    service->FlushManifest();
    Require(inspect(*service).dirty && !std::filesystem::exists(temporary),
            "refused manifest replacement preserves dirty state and retires the temporary file");
  }
#else
  Require(std::filesystem::create_directory(temporary), "block temp path in fixture");
  service->FlushManifest();
  Require(inspect(*service).dirty && std::filesystem::remove(temporary), "failed staging leaves manifest dirty");
#endif
  Require(ReadFile(manifest) == firstManifest, "refused flush preserves the prior manifest byte-for-byte");
  service->FlushManifest();
  Require(!inspect(*service).dirty && Json::parse(ReadFile(manifest))["entries"].size() == 3,
          "next flush replaces the manifest and publishes the staged entry");

  {
    observation::Scope epoch;
    auto reopened = UInventoryIconService::Create();
    Require(reopened->Initialize(originalRoot), "reload complete manifest for retained allocation comparison");
    loaded = inspect(*reopened);
    Require(loaded.entries == 3 && loaded.manifestBytes > idle.manifestBytes &&
                loaded.cpuBytes == observation::State.live, "reloaded path and hash backing match independent allocations");
    reopened.reset();
    Require(!observation::State.live, "reloaded manifest backing retires");
  }

  Require(queue(*service, longId), "queue item before invalidating another kind");
  const auto pending = service->InspectReadbackStorage().first_buffer;
  service->InvalidateKind("block");
  const auto afterBlock = Json::parse(ReadFile(manifest));
  Require(afterBlock["entries"].size() == 2 && !afterBlock["entries"].contains("block:fixture_block_icon") &&
              afterBlock["entries"].contains("creature:fixture_creature_icon") &&
              service->InspectReadbackStorage().first_buffer == pending && glIsBuffer(pending),
          "block invalidation preserves creature and skin entries and pending item work");
  Require(std::filesystem::create_directory(invalidRoot), "create malformed cache test root");
  {
    std::ofstream bad(invalidRoot / "manifest.json", std::ios::binary);
    bad << "{invalid fixture manifest";
    Require(bool(bad), "write malformed cache fixture");
  }
  Require(!service->Initialize(invalidRoot) && inspect(*service).entries == 2 &&
              service->InspectReadbackStorage().first_buffer == pending && glIsBuffer(pending),
          "refused cache-root replacement preserves both published entries and pending readback");
  Require(service->Initialize(nextRoot) && !glIsBuffer(pending) && !inspect(*service).jobs &&
              !inspect(*service).entries, "accepted root replacement retires jobs tied to the old root");
  service->FlushManifest();
  Require(!std::filesystem::exists(nextRoot / "manifest.json") && Json::parse(ReadFile(manifest)) == afterBlock,
          "a cancelled job never writes into the replacement root");

  const auto source = [](const std::shared_ptr<UInventoryIconService> &objects,
                         const std::shared_ptr<UInventoryIconService> &creatures,
                         const std::shared_ptr<UInventoryIconService> &items)
  {
    return std::make_unique<UGuiIconSource>(nullptr,
        std::make_unique<UObjectIconCache>(nullptr, nullptr, nullptr, nullptr, objects),
        std::make_unique<UCreatureIconCache>(nullptr, creatures), std::make_unique<UItemIconCache>(nullptr, items));
  };
  {
    auto another = UInventoryIconService::Create();
    Require(!source(service, another, service)->InspectInventoryIconStorage().supported,
            "distinct service owners cannot be charged as one");
    auto unsupported = std::make_shared<UInventoryIconService>();
    Require(!source(unsupported, unsupported, unsupported)->InspectInventoryIconStorage().supported,
            "unknown factory history rejects coallocation proof");
  }
  auto aliases = source(service, service, service);
  const auto joined = aliases->InspectInventoryIconStorage();
  Require(joined.supported && joined.aliases == 3 && joined.cpuBytes == inspect(*service).cpuBytes,
          "object, creature and item cache routes charge their common service once");
  Require(queue(*service, longId), "queue final jobs under the shared service owner");
  TerrainGpuBufferCensus sharedGpu;
  aliases->InspectIconReadbackGpuBuffers(sharedGpu);
  const auto joinedGpu = sharedGpu.Read();
  Require(joinedGpu.total.objects == 1 && joinedGpu.references == 3 && joinedGpu.total.bytes == size_t(size) * size * 4,
          "the three cache routes share one measured pack store");
  const auto finalBuffer = service->InspectReadbackStorage().first_buffer;
  std::weak_ptr<UInventoryIconService> lifetime = service;
  service.reset();
  Require(!lifetime.expired() && glIsBuffer(finalBuffer), "cache references keep the shared service alive");
  aliases.reset();
  Require(lifetime.expired() && !glIsBuffer(finalBuffer) && glIsTexture(texture.Get()),
          "final shared owner retires pending buffers and preserves the borrowed texture");

  return {{"status", "PASS"}, {"cold_cpu_bytes", cold.cpuBytes}, {"path_bytes", idle.pathBytes},
          {"service_request", cold.serviceRequest}, {"pending_job_bytes", busy.jobBytes - idle.jobBytes},
          {"pack_buffer_bytes", busy.packBufferBytes}, {"reloaded_manifest_bytes", loaded.manifestBytes},
          {"shared_aliases", joined.aliases}, {"selective_invalidation", true}, {"root_replacement", true},
          {"manifest_replacement_retry", true}, {"retired_last_owner", true},
          {"gpu_readback", gpu}, {"shared_gpu_readback", DescribeIconReadbackGpuBuffers(joinedGpu)}};
}
} // namespace cutum::inventory_icon_storage_fixture

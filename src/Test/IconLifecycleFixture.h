#pragma once

#include "Gui/Cache/InventoryIconService.h"
#include "Gui/Cache/ItemIconCache.h"
#include "Render/Pipeline/GlStateScope.h"
#include "Render/Textures/GlTextureOwner.h"
#include "Test/GuiContextStorageFixture.h"
#include "Test/OrdinaryNewObservation.h"
#include <array>

namespace cutum
{
namespace icon_lifecycle_fixture
{
using gui_context_fixture::Require;
constexpr int Size = 16;
constexpr size_t Bytes = Size * Size * 4;

inline UGlTextureOwner MakeTexture()
{
  static constexpr std::array<unsigned char, Bytes> pixels = [] {
    std::array<unsigned char, Bytes> result{};
    for (size_t i = 0; i < Bytes; i += 4)
    {
      result[i] = 19;
      result[i + 1] = 71;
      result[i + 2] = 137;
      result[i + 3] = 255;
    }
    return result;
  }();
  UGlStateScope state(kGlMaskIconFbo);
  GLuint name{};
  glGenTextures(1, &name);
  UGlTextureOwner texture(name);
  glBindTexture(GL_TEXTURE_2D, name);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, Size, Size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  return texture;
}

inline std::array<GLint, 8> ReadBindings()
{
  std::array<GLint, 8> out{};
  constexpr std::array<GLenum, 8> queries{GL_READ_FRAMEBUFFER_BINDING, GL_DRAW_FRAMEBUFFER_BINDING,
      GL_PIXEL_PACK_BUFFER_BINDING, GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS,
      GL_PACK_SKIP_PIXELS, GL_ACTIVE_TEXTURE};
  for (size_t i = 0; i < queries.size(); ++i)
    glGetIntegerv(queries[i], &out[i]);
  return out;
}

inline void CheckTextureOwnership()
{
  auto source = MakeTexture();
  UGlFramebufferOwner framebuffer;
  {
    UGlStateScope state(kGlMaskIconFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer.Get());
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, source.Get(), 0);
    const auto candidate = MakeTexture();
    try
    {
      UGlIconColorAttachment attachment(framebuffer.Get(), candidate.Get(), source.Get());
      throw std::bad_alloc{};
    }
    catch (const std::bad_alloc &) {}
    GLint attachment{};
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                         GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &attachment);
    Require(GLuint(attachment) == source.Get(), "failed icon render restores original color attachment");
  }

  std::unordered_map<std::string, UGlTextureOwner> cache;
  const std::string key(84, 'k');
  GLuint rejected{};
  {
    auto owned = MakeTexture();
    rejected = owned.Get();
    observation::Scope epoch(0);
    bool refused = false;
    try { cache.try_emplace(key, UGlTextureOwner(owned.Release())); }
    catch (const std::bad_alloc &) { refused = true; }
    Require(refused && cache.empty() && !glIsTexture(rejected), "refused publication deletes its candidate GL texture");
  }
  cache.try_emplace(key, UGlTextureOwner::Borrow(source.Get()));
  cache.clear();
  Require(glIsTexture(source.Get()), "borrowed atlas texture survives cache retirement");
}

inline size_t CheckItemQueue(const std::array<GLint, 8> &bindings)
{
  const std::string first = "fixture_iron_pickaxe_with_long_identifier";
  const std::string second = "fixture_iron_shovel_with_long_identifier";
  size_t attempts{};
  {
    observation::Scope epoch;
    UItemIconCache cache(nullptr);
    Require(!cache.GetIcon(first) && cache.PendingWarmupCount() == 1, "item queues an icon before rendering");
    const auto before = observation::State.attempts;
    cache.WarmupNext(1);
    attempts = observation::State.attempts - before;
    Require(attempts >= 2 && cache.GetIcon(first) && !cache.PendingWarmupCount(),
            "production item warmup publishes an actual texture");
  }
  for (size_t prefix = 0; prefix < attempts; ++prefix)
  {
    GLuint firstTexture{}, secondTexture{};
    observation::Scope epoch;
    {
      UItemIconCache cache(nullptr);
      cache.GetIcon(first);
      cache.GetIcon(second);
      observation::State.fail_after = observation::State.attempts + prefix;
      bool refused = false;
      try { cache.WarmupNext(1); }
      catch (const std::bad_alloc &) { refused = true; }
      observation::State.fail_after = SIZE_MAX;
      Require(refused && cache.PendingWarmupCount() == 2 && !cache.GetIcon(first) &&
                  cache.PendingWarmupCount() == 2 && ReadBindings() == bindings,
              "refused item work preserves ordering, deduplication and GL bindings");
      cache.WarmupNext(1);
      firstTexture = cache.GetIcon(first);
      Require(firstTexture && glIsTexture(firstTexture) && !cache.GetIcon(second) &&
                  cache.PendingWarmupCount() == 1,
              "retry completes exactly the original front before its successor");
      cache.WarmupNext(1);
      secondTexture = cache.GetIcon(second);
      Require(secondTexture && secondTexture != firstTexture && ReadBindings() == bindings,
              "successor publishes separately and restores GL bindings");
    }
    Require(!glIsTexture(firstTexture) && !glIsTexture(secondTexture) && !observation::State.live,
            "item cache destruction retires textures and ordinary ownership");
  }
  return attempts;
}

inline size_t CheckReadback(const std::filesystem::path &directory, const std::array<GLint, 8> &bindings)
{
  Require(std::filesystem::create_directory(directory), "readback fixture owns a fresh cache directory");
  const auto source = MakeTexture();
  const std::string kind = "item", id(44, 'n'), variant(28, 'v'), fingerprint(60, 'f');
  const auto store = [&](UInventoryIconService &service) {
    return service.StoreIconTexture(kind, id, variant, fingerprint, Size, source.Get());
  };
  size_t attempts{};
  {
    UInventoryIconService service;
    Require(service.Initialize(directory), "initialize the readback cache");
    {
      observation::Scope epoch;
      Require(store(service), "schedule a real readback");
      attempts = observation::State.attempts;
    }
    const auto pending = service.InspectReadbackStorage();
    Require(pending.supported && pending.jobs == 1 && pending.buffer_bytes == Bytes &&
                glIsBuffer(pending.first_buffer) && ReadBindings() == bindings,
            "accepted readback has one real pack buffer and preserves caller state");
    service.InvalidateKind(kind);
    Require(!glIsBuffer(pending.first_buffer) && !service.InspectReadbackStorage().jobs,
            "kind invalidation retires queued GPU buffers");
  }
  for (size_t prefix = 0; prefix < attempts; ++prefix)
  {
    UInventoryIconService service;
    Require(service.Initialize(directory), "initialize readback refusal fixture");
    observation::Scope epoch(prefix);
    bool refused = false;
    try { store(service); }
    catch (const std::bad_alloc &) { refused = true; }
    observation::State.fail_after = SIZE_MAX;
    Require(refused && !service.InspectReadbackStorage().jobs && ReadBindings() == bindings,
            "failed readback admission leaves no partial job or altered bindings");
  }

  GLuint cancelled{};
  {
    UInventoryIconService service;
    Require(service.Initialize(directory) && store(service), "retry readback after refused admission");
    const auto pending = service.InspectReadbackStorage();
    glFinish();
    {
      observation::Scope epoch(0);
      bool refused = false;
      try { service.FlushManifest(); }
      catch (const std::bad_alloc &) { refused = true; }
      Require(refused && service.InspectReadbackStorage().first_buffer == pending.first_buffer &&
                  ReadBindings() == bindings,
              "CPU readback staging refusal preserves the queued GPU buffer and GL state");
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pending.first_buffer);
    GLint mapped{};
    glGetBufferParameteriv(GL_PIXEL_PACK_BUFFER, GL_BUFFER_MAPPED, &mapped);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, GLuint(bindings[2]));
    Require(!mapped, "CPU allocation refusal leaves the pack buffer unmapped");
    service.FlushManifest();
    GLuint loaded{};
    Require(!glIsBuffer(pending.first_buffer) && !service.InspectReadbackStorage().jobs,
            "same readback drains and retires the GPU buffer");
    Require(service.GetStats().RenderStores == 1, "completed readback persists a PNG");
    Require(service.TryLoadIconTexture(kind, id, variant, fingerprint, Size, loaded),
            "persisted PNG reloads into a texture");
    UGlTextureOwner reloaded(loaded);
    std::array<unsigned char, Bytes> pixels{};
    {
      UGlStateScope state(kGlMaskIconFbo);
      glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glPixelStorei(GL_PACK_ROW_LENGTH, 0);
      glPixelStorei(GL_PACK_SKIP_ROWS, 0);
      glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
      glBindTexture(GL_TEXTURE_2D, reloaded.Get());
      glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      glBindBuffer(GL_PIXEL_PACK_BUFFER, GLuint(bindings[2]));
      glPixelStorei(GL_PACK_ALIGNMENT, bindings[3]);
      glPixelStorei(GL_PACK_ROW_LENGTH, bindings[4]);
      glPixelStorei(GL_PACK_SKIP_ROWS, bindings[5]);
      glPixelStorei(GL_PACK_SKIP_PIXELS, bindings[6]);
    }
    for (size_t i = 0; i < pixels.size(); i += 4)
      Require(pixels[i] == 19 && pixels[i + 1] == 71 && pixels[i + 2] == 137 && pixels[i + 3] == 255,
              "PNG round trip preserves independent RGBA witness");
    Require(store(service), "schedule work for service destruction control");
    cancelled = service.InspectReadbackStorage().first_buffer;
  }
  Require(cancelled && !glIsBuffer(cancelled) && ReadBindings() == bindings,
          "cache service destruction cancels unfinished readbacks");
  auto shared = std::make_shared<UInventoryIconService>();
  Require(shared->Initialize(directory) && store(*shared), "schedule work for item revision invalidation");
  const auto invalidated = shared->InspectReadbackStorage().first_buffer;
  UItemIconCache cache(nullptr, shared);
  cache.Invalidate();
  Require(invalidated && !glIsBuffer(invalidated) && !shared->InspectReadbackStorage().jobs,
          "item revision invalidation cancels pending GPU work");
  return attempts;
}

inline nlohmann::json Run(const std::filesystem::path &report)
{
  auto directory = report;
  directory += ".icon-cache";
  const auto original = ReadBindings();
  auto texture0 = MakeTexture();
  auto texture2 = MakeTexture();
  UGlFramebufferOwner read, draw;
  GLuint pack{};
  glGenBuffers(1, &pack);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, read.Get());
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw.Get());
  glBindBuffer(GL_PIXEL_PACK_BUFFER, pack);
  glBufferData(GL_PIXEL_PACK_BUFFER, 4096, nullptr, GL_STREAM_READ);
  glPixelStorei(GL_PACK_ALIGNMENT, 8);
  glPixelStorei(GL_PACK_ROW_LENGTH, 23);
  glPixelStorei(GL_PACK_SKIP_ROWS, 2);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 3);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture0.Get());
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, texture2.Get());
  const auto bindings = ReadBindings();
  CheckTextureOwnership();
  const auto itemCases = CheckItemQueue(bindings);
  const auto readbackCases = CheckReadback(directory, bindings);
  GLint unit2{}, unit0{};
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &unit2);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &unit0);
  Require(GLuint(unit0) == texture0.Get() && GLuint(unit2) == texture2.Get() && glGetError() == GL_NO_ERROR,
          "icon exception and completion preserve both texture units and clean GL status");
  glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(original[0]));
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(original[1]));
  glBindBuffer(GL_PIXEL_PACK_BUFFER, GLuint(original[2]));
  glDeleteBuffers(1, &pack);
  glPixelStorei(GL_PACK_ALIGNMENT, original[3]);
  glPixelStorei(GL_PACK_ROW_LENGTH, original[4]);
  glPixelStorei(GL_PACK_SKIP_ROWS, original[5]);
  glPixelStorei(GL_PACK_SKIP_PIXELS, original[6]);
  glActiveTexture(GLenum(original[7]));
  return {{"status", "PASS"}, {"item_processing_refusals", itemCases}, {"readback_admission_refusals", readbackCases},
          {"readback_cpu_retry", true}, {"rgba_bytes", Bytes}, {"owned_and_borrowed_texture_controls", true},
          {"buffer_cancellation", true}, {"caller_gl_state_restored", true}};
}
}
}

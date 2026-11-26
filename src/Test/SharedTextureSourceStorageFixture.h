#pragma once

#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Items/ItemGltfTextureCache.h"
#include "Render/Textures/VisualTextureUpload.h"
#include "Render/Engine/RenderImageStorageReport.h"
#include "Test/InventoryIconServiceStorageFixture.h"
#include "Test/SharedContentPreviewStorageFixture.h"
#include "ThirdParty/stb_image_write.h"

namespace cutum::shared_texture_source_fixture
{
using gui_context_fixture::Require;
using shared_content_preview_fixture::Binding;
using shared_content_preview_fixture::ReadCallerState;
using Json = nlohmann::json;

struct Uploads
{
  std::array<GLuint, 12> Names{};
  std::array<size_t, 12> Bytes{};
  size_t Count{};
  const VisualTextureUploadProbe Probe{this, Record};

  static void Record(void *state, GLuint name, size_t bytes) noexcept
  {
    auto &self = *static_cast<Uploads *>(state);
    const auto index = self.Count++;
    if (index < self.Names.size())
    {
      self.Names[index] = name;
      self.Bytes[index] = bytes;
    }
  }

  bool Retired() const
  {
    return Count <= Names.size() && std::none_of(Names.begin(), Names.begin() + Count,
        [](GLuint name) { return glIsTexture(name) != 0; });
  }
};

inline void WritePng(const std::filesystem::path &file, int width, int height, unsigned char red)
{
  std::filesystem::create_directories(file.parent_path());
  std::vector<stbi_uc> pixels(size_t(width) * height * 4, 255);
  for (size_t i = 0; i < pixels.size(); i += 4)
    pixels[i] = red;
  Require(stbi_write_png(file.string().c_str(), width, height, 4, pixels.data(), width * 4),
          "write independent test RGBA image");
}

class RefuseMipmaps
{
public:
  RefuseMipmaps()
  {
    Original = __glewGenerateMipmap;
    __glewGenerateMipmap = Record;
    Name = 0;
  }
  ~RefuseMipmaps() { __glewGenerateMipmap = Original; }
  inline static GLuint Name{};

private:
  inline static PFNGLGENERATEMIPMAPPROC Original{};
  static void GLAPIENTRY Record(GLenum) { Name = GLuint(Binding(GL_TEXTURE_BINDING_2D)); }
};

inline Json CheckItems(const std::filesystem::path &root)
{
  auto &cache = ItemGltfTextureCache::Instance();
  cache.Clear();
  const auto dir = root / "model";
  const auto input = dir / "model.gltf";
  const auto red = dir / "images/old.png";
  const auto image = dir / "images/current.png";
  const auto unused = dir / "images/unused.png";
  WritePng(red, 1, 3, 17);
  WritePng(image, 3, 5, 123);
  WritePng(unused, 2, 2, 37);
  const auto embedded = inventory_icon_storage_fixture::ReadFile(unused);
  {
    std::ofstream bin(dir / "image.bin", std::ios::binary);
    bin.write(embedded.data(), embedded.size());
    Require(bool(bin), "write actual glTF embedded image bytes");
  }
  {
    const auto material = [](const char *name, int index) -> Json
    {
      return {{"name", name}, {"pbrMetallicRoughness", {{"baseColorTexture", {{"index", index}}}}}};
    };
    std::ofstream out(input);
    out << Json{{"images", Json::array({{{"uri","images/old.png"}}, {{"bufferView",0}}, {{"uri","images/current.png"}}})},
                {"textures", Json::array({{{"source",0}}, {{"source",2}}})},
                {"bufferViews", Json::array({{{"buffer",0}, {"byteOffset",0}, {"byteLength",embedded.size()}}})},
                {"buffers", Json::array({{{"uri","image.bin"}}})},
                {"materials", Json::array({material("body",0), material("body",1), material("side",1),
                    {{"name","color"}, {"pbrMetallicRoughness", {{"baseColorFactor", Json::array({1,0,1,1})}}}}})}};
    Require(bool(out), "write isolated material and image aliases");
  }
  const auto caller = ReadCallerState();
  const auto baseline = ItemGltfTextureCache::InspectCurrentStorage();
  size_t tableBytes = 0, rgbaBytes = 0, legacyAttempts = 0;
  {
    observation::Scope observed;
    Uploads records;
    UVisualTextureUploadProbeScope probe(records.Probe);
    const auto body = cache.Get(input, "body");
    legacyAttempts = observation::State.attempts;
    const auto factor = cache.Get(input, "color_factor");
    const auto live = ItemGltfTextureCache::InspectCurrentStorage();
    Require(records.Count == 4 && live.supported && live.images == 2 && live.aliases == 4 && live.models == 1 &&
                live.tableBytes == baseline.tableBytes + observation::State.live && live.rgbaBytes == 64,
            "model counts one owned image per resource and all alias backing");
    Require(!glIsTexture(records.Names[0]) && !glIsTexture(records.Names[1]) && records.Names[2] == body &&
                records.Names[3] == factor && glIsTexture(body) && glIsTexture(factor) &&
                cache.Get(input, "side") == body && cache.Get(input, "color") == factor,
            "unused and overwritten uploads retire and aliases share their surviving image");
    Require(ReadCallerState() == caller, "legacy model uploads preserve the caller GL bindings");
    tableBytes = live.tableBytes;
    rgbaBytes = live.rgbaBytes;
    ItemGltfTextureCache::ClearForContextShutdown();
    Require(records.Retired() && !observation::State.live && !observation::State.blocks,
            "context retirement deletes both item images without separate alias retirement");
  }
  {
    Require(legacyAttempts > 0, "record complete model publication request count");
    Uploads records;
    UVisualTextureUploadProbeScope probe(records.Probe);
    observation::Scope refused(legacyAttempts - 1);
    bool failed = false;
    try { cache.Get(input, "body"); }
    catch (const std::bad_alloc &) { failed = true; }
    Require(failed && records.Count == 4 && records.Retired() && !observation::State.live &&
                !ItemGltfTextureCache::InspectCurrentStorage().models,
            "refused image-table publication retires every provisional model upload");
  }
  {
    auto ledger = std::make_shared<UStbiAllocationLedger>(SIZE_MAX, 0);
    UStbiAllocationScope refused(ledger);
    Require(!cache.Get(input, "body") && ledger->GetSnapshot().refusals &&
                !ItemGltfTextureCache::InspectCurrentStorage().models, "temporary decoder refusal preserves a retryable model miss");
  }
  Require(cache.Get(input, "body") != 0, "identical model request recovers after admission and decode refusals");
  cache.Clear();

  {
    RefuseMipmaps refused;
    Require(!cache.GetLinearAlbedo(image) && !ItemGltfTextureCache::InspectCurrentStorage().linearImages,
            "refused mip generation cannot publish a partially allocated image");
  }
  Require(RefuseMipmaps::Name && !glIsTexture(RefuseMipmaps::Name) && ReadCallerState() == caller,
          "refused mip candidate retires and restores the caller bindings");
  const auto renamed = image.parent_path() / "." / image.filename();
  const auto albedo = cache.GetLinearAlbedo(image);
  Require(albedo && cache.GetLinearAlbedo(renamed) == albedo, "canonical albedo path has two aliases and one image");
  const auto linear = ItemGltfTextureCache::InspectCurrentStorage();
  const auto actual = cache.InspectMemory();
  Require(linear.supported && linear.linearImages == 1 && linear.linearAliases == 2 && linear.rgbaBytes == 72 &&
              actual.Textures == 1 && actual.InvalidTextures == 0 && actual.RgbaMipBytes == linear.rgbaBytes &&
              actual.Dimensions.front() == std::array<int, 3>{3,5,3},
          "NPOT sRGB mip storage matches an independent OpenGL query");
  {
    RestoreTextureBinding binding;
    glBindTexture(GL_TEXTURE_2D, albedo);
    GLint format = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
    Require(format == GL_SRGB8_ALPHA8, "linear sampling uses sRGB decode before filtering");
  }
  RenderImageCensus retained;
  ItemGltfTextureCache::InspectRenderImages(retained);
  const auto images = retained.Read();
  Require(images.contextAvailable && !images.failedQueries && images.references == 1 &&
              images.images.size() == 1 && images.logicalBytes == 72 && images.images[0].levels.size() == 3,
          "one cache-owned albedo contributes all actual levels through both file aliases");
  cache.Clear();
  Require(!glIsTexture(albedo) && ReadCallerState() == caller && retained.Read().namesWithoutObject == 1,
          "linear alias retirement leaves no GL texture or changed binding");
  return {{"legacy_table_bytes", tableBytes}, {"legacy_rgba_bytes", rgbaBytes},
          {"linear_rgba_bytes", linear.rgbaBytes}, {"unused_upload_retired", true},
          {"item_refusals_recovered", true}, {"allocated_mips", DescribeRenderImages(images)}};
}

inline Json CheckCreatures(const std::filesystem::path &root)
{
  const auto basePath = root / "creatures";
  const auto skinPath = root / "skins";
  const auto overlay = root / "overlay";
  const std::string speciesId = "texture_source_long_creature_identity";
  const std::string skinId = "texture_source_long_skin_identity";
  const std::string speciesKey = speciesId + "/diffuse", skinKey = "skin/" + skinId + "/diffuse";
  const auto base = basePath.string(), skins = skinPath.string();
  WritePng(basePath / speciesId / "textures/diffuse.png", 3, 5, 12);
  WritePng(skinPath / skinId / "textures/diffuse.png", 2, 1, 26);
  WritePng(overlay / "creatures" / speciesId / "textures/diffuse.png", 2, 2, 51);
  const std::vector<std::filesystem::path> layers{overlay};
  auto ledger = std::make_shared<UStbiAllocationLedger>();
  const auto caller = ReadCallerState();
  size_t tableBytes = 0, controlBytes = 0;
  {
    observation::Scope observed;
    const auto storage = UCreatureTextureStorage::Create();
    {
      UStbiAllocationScope decoder(ledger);
      Require(storage->LoadFromCreatureAndSkinRoots(base, skins), "publish base creature and skin images");
    }
    auto stats = storage->InspectOwnedStorage();
    Require(stats.supported && stats.tableBytes + stats.controlBytes == observation::State.live &&
                stats.images == 2 && stats.rgbaBytes == 68, "creature tables and ownership equal independent requests");
    Require(!ledger->GetSnapshot().live_request_bytes, "decoded pixels retire before retained colour storage is sampled");
    tableBytes = stats.tableBytes;
    controlBytes = stats.controlBytes;
    const auto oldSpecies = storage->GetTexture(speciesKey), oldSkin = storage->GetTexture(skinKey);
    {
      Uploads records;
      const auto allocationCount = ledger->GetSnapshot().allocation_attempts;
      auto fail = std::make_shared<UStbiAllocationLedger>(SIZE_MAX, allocationCount);
      UStbiAllocationScope refused(fail);
      UVisualTextureUploadProbeScope probe(records.Probe);
      Require(!storage->LoadFromCreatureAndSkinRoots(base, skins, layers) && fail->GetSnapshot().refusals &&
                  records.Count == 2 && records.Retired() && storage->GetTexture(speciesKey) == oldSpecies &&
                  storage->GetTexture(skinKey) == oldSkin && glIsTexture(oldSpecies) && glIsTexture(oldSkin),
              "refused overlay retires staged base and preserves both published names");
    }
    GLuint replaced{}, replacedSkin{};
    {
      Uploads records;
      UVisualTextureUploadProbeScope probe(records.Probe);
      Require(storage->LoadFromCreatureAndSkinRoots(base, skins, layers) && records.Count == 3,
              "retry reload with the same base and overlay succeeds");
      replaced = storage->GetTexture(speciesKey);
      replacedSkin = storage->GetTexture(skinKey);
      stats = storage->InspectOwnedStorage();
      Require(replaced == records.Names[2] && replacedSkin == records.Names[1] && !glIsTexture(records.Names[0]) &&
                  !glIsTexture(oldSpecies) && !glIsTexture(oldSkin) && stats.images == 2 && stats.rgbaBytes == 24 &&
                  stats.tableBytes + stats.controlBytes == observation::State.live,
              "successful overlay replaces one image and retires previous and shadowed names");
    }
    {
      RenderImageCensus retained;
      storage->InspectRenderImages(retained);
      storage->InspectRenderImages(retained);
      const auto images = retained.Read();
      Require(images.contextAvailable && !images.failedQueries && images.references == 4 &&
                  images.images.size() == 2 && images.logicalBytes == stats.rgbaBytes,
              "shared creature roots contribute each current uploaded image once");
    }
    const auto mergeRoot = (overlay / "creatures").string();
    Require(storage->MergeCreatureRoot(mergeRoot) && storage->GetTexture(skinKey) == replacedSkin && !glIsTexture(replaced),
            "single-root merge preserves the unrelated skin name");
    const auto lastCreature = storage->GetTexture(speciesKey);
    observation::State.fail_after = observation::State.attempts;
    storage->Clear();
    observation::State.fail_after = SIZE_MAX;
    Require(!glIsTexture(replacedSkin) && !glIsTexture(lastCreature) && !storage->Count() && ReadCallerState() == caller,
            "creature clear retires final names without allocation or binding change");
  }
  return {{"creature_table_bytes", tableBytes}, {"creature_control_bytes", controlBytes},
          {"creature_rgba_bytes", 24}, {"overlay_refusal_recovered", true}, {"context_retirement", true}};
}

inline Json Run(const std::filesystem::path &report)
{
  auto root = report;
  root += ".shared-textures";
  Require(std::filesystem::create_directory(root), "create isolated shared texture fixture directory");
  UGlStateScope restore(kGlMaskIconFbo);
  const auto original = Binding(GL_PIXEL_UNPACK_BUFFER_BINDING);
  const auto alignment = Binding(GL_UNPACK_ALIGNMENT), rowLength = Binding(GL_UNPACK_ROW_LENGTH);
  GLuint buffer = 0;
  glGenBuffers(1, &buffer);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer);
  glBufferData(GL_PIXEL_UNPACK_BUFFER, 4, nullptr, GL_STATIC_DRAW);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 32);
  const auto items = CheckItems(root);
  const auto creatures = CheckCreatures(root);
  Require(Binding(GL_PIXEL_UNPACK_BUFFER_BINDING) == GLint(buffer) && Binding(GL_UNPACK_ALIGNMENT) == 8 &&
              Binding(GL_UNPACK_ROW_LENGTH) == 32 && glGetError() == GL_NO_ERROR,
          "shared texture uploads restore hostile unpack state and produce no GL error");
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, GLuint(original));
  glDeleteBuffers(1, &buffer);
  glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
  Require(!observation::State.live && !observation::State.blocks, "all independently tracked texture backing retires");
  return {{"status", "PASS"}, {"items", items}, {"creatures", creatures}, {"unpack_state_restored", true}};
}

} // namespace cutum::shared_texture_source_fixture

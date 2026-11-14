#include "Gui/Cache/InventoryIconService.h"
#include "Gui/Cache/ItemIconFingerprint.h"

#include <chrono>
#include <stdexcept>

#include <iostream>

using cutum::UInventoryIconService;

namespace
{
void TestItemDependencies()
{
  const auto root =
      std::filesystem::temp_directory_path() /
      ("cubatarium-icon-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root / "shaders");
  struct Cleanup
  {
    std::filesystem::path Root;
    ~Cleanup()
    {
      std::error_code ec;
      for (const char *name :
           {"model.gltf", "model.bin", "albedo.png", "unrelated.txt",
            "shaders/item_surface.vert", "shaders/item_surface.frag",
            "shaders/vshader.glsl", "shaders/fshader.glsl"})
        std::filesystem::remove(Root / name, ec);
      std::filesystem::remove(Root / "shaders", ec);
      std::filesystem::remove(Root, ec);
    }
  } cleanup{root};
  const auto write = [&](const char *name, const std::string &value)
  {
    std::ofstream out(root / name, std::ios::binary);
    out << value;
    if (!out)
      throw std::runtime_error("cannot write icon dependency fixture");
  };
  auto model = nlohmann::json{{"buffers", {{{"uri", "model.bin"}}}},
                              {"images", {{{"uri", "albedo.png"}}}},
                              {"extras", {{"icon_euler_deg", {0, 0, 0}}}}};
  write("model.gltf", model.dump());
  for (const char *name :
       {"model.bin", "albedo.png", "shaders/item_surface.vert",
        "shaders/item_surface.frag", "shaders/vshader.glsl",
        "shaders/fshader.glsl"})
    write(name, "original");
  const auto fingerprint = [&](float scale = 1.f)
  { return cutum::BuildItemIconFingerprint(root, "model.gltf", scale); };
  const auto require = [](bool ok, const char *reason)
  {
    if (!ok)
      throw std::runtime_error(reason);
  };
  const auto original = fingerprint();
  require(!original.empty(), "fixture has no icon identity");
  require(original ==
              cutum::BuildItemIconFingerprint(root / ".", "model.gltf", 1.f),
          "path aliases changed icon identity");
  require(original != fingerprint(1.25f), "icon scale did not invalidate icon");
  write("unrelated.txt", "ignored");
  require(original == fingerprint(), "unrelated file invalidated icon");
  for (const char *name :
       {"model.bin", "albedo.png", "shaders/item_surface.vert",
        "shaders/item_surface.frag", "shaders/vshader.glsl",
        "shaders/fshader.glsl"})
  {
    const auto time = std::filesystem::last_write_time(root / name);
    write(name, "modified"); // Same byte count and timestamp as original.
    std::filesystem::last_write_time(root / name, time);
    require(original != fingerprint(), "content replacement reused stale icon");
    write(name, "original");
    std::filesystem::last_write_time(root / name, time);
    require(original == fingerprint(),
            "identical bytes did not restore identity");
  }
  model["extras"]["icon_euler_deg"] = {0, 30, 0};
  write("model.gltf", model.dump());
  require(original != fingerprint(),
          "model presentation did not invalidate icon");
  std::filesystem::remove(root / "albedo.png");
  require(fingerprint().empty(), "missing dependency reused cached icon");
  write("albedo.png", "original");
  model["images"][0]["uri"] = "../unread.png";
  write("model.gltf", model.dump());
  require(fingerprint().empty(), "escaping resource URI was accepted");
  write("model.gltf", "invalid JSON");
  require(fingerprint().empty(), "malformed model reused cached icon");
}
} // namespace

int main()
{
  try
  {
    TestItemDependencies();
  }
  catch (const std::exception &e)
  {
    std::cerr << e.what() << '\n';
    return 1;
  }
  const std::string k1 =
      UInventoryIconService::BuildCacheKey("creature", "puffin", "");
  const std::string k2 =
      UInventoryIconService::BuildCacheKey("creature", "puffin", "");
  const std::string k3 =
      UInventoryIconService::BuildCacheKey("creature", "puffin", "skin_a");
  if (k1 != k2 || k1 == k3)
  {
    std::cerr << "BuildCacheKey is unstable\n";
    return 1;
  }

  const std::string h1 = UInventoryIconService::HashFingerprint("v2|a");
  const std::string h2 = UInventoryIconService::HashFingerprint("v2|a");
  const std::string h3 = UInventoryIconService::HashFingerprint("v2|b");
  if (h1 != h2 || h1 == h3)
  {
    std::cerr << "HashFingerprint is unstable\n";
    return 1;
  }

  UInventoryIconService service;
  if (!service.Initialize())
  {
    std::cerr << "Initialize failed\n";
    return 1;
  }
  service.InvalidateKind("creature");
  service.InvalidateKind("block");
  const UInventoryIconService::Stats &stats = service.GetStats();
  if (stats.ManifestVersionMismatches != 0)
  {
    std::cerr << "unexpected manifest version mismatch after init\n";
    return 1;
  }
  return 0;
}

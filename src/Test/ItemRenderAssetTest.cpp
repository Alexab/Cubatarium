#include "Items/ItemRenderAsset.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Items/ItemActionPulse.h"
#include "Items/ItemInspectionSource.h"
#include "Items/ItemLabAssets.h"
#include "Items/ItemPresentationMath.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace
{
void Expect(bool value, const char *reason)
{
  if (!value)
    throw std::runtime_error(reason);
}
void TestInspectionIsolation()
{
  using namespace cutum;
  auto first = std::make_shared<ItemRenderAsset>(),
       second = std::make_shared<ItemRenderAsset>();
  Expect(!FindItemInspectionSource("bucket"),
         "world starts without inspection override");
  {
    UItemInspectionScope outer({"bucket", "reference.gltf", first});
    Expect(FindItemInspectionSource("bucket")->Asset == first &&
               !FindItemInspectionSource("apple"),
           "override is item-specific");
    const auto key = ItemInspectionCacheKey("bucket");
    try
    {
      UItemInspectionScope nested({"bucket", "candidate.gltf", second});
      Expect(FindItemInspectionSource("bucket")->Asset == second &&
                 key != ItemInspectionCacheKey("bucket"),
             "candidate has independent render cache key");
      throw std::runtime_error("cancel render");
    }
    catch (const std::runtime_error &)
    {
    }
    Expect(FindItemInspectionSource("bucket")->Asset == first,
           "failed nested preview restores reference");
  }
  Expect(!FindItemInspectionSource("bucket") &&
             ItemInspectionCacheKey("bucket") == "bucket",
         "preview cannot leak into world rendering");
}

void TestLabPaths(const std::filesystem::path &folder)
{
  namespace fs = std::filesystem;
  const auto parent = fs::weakly_canonical(folder);
  const auto root = parent / "lab";
  struct Cleanup
  {
    fs::path Root, Parent;
    ~Cleanup()
    {
      std::error_code ec;
      if (!fs::is_symlink(Root) &&
          fs::weakly_canonical(Root).parent_path() == Parent &&
          Root.filename() == "lab")
        fs::remove_all(Root, ec);
    }
  } cleanup{root, parent};
  const auto assets = root / "bin";
  const auto published = assets / "models/items/bucket/model.gltf";
  const auto reference =
      published.parent_path() / "_itemgen_reference/model.gltf";
  const auto staged = root / "assetgen_output/items/redesign/bucket/model.gltf";
  fs::create_directories(published.parent_path());
  std::ofstream(published) << "published";
  auto paths = cutum::ResolveItemLabAssets(
      assets, "models/items/bucket/model.gltf", "bucket");
  Expect(paths.Reference == published && !paths.Archived && !paths.Staged,
         "development reference resolves published model");
  fs::create_directories(staged.parent_path());
  std::ofstream(staged) << "candidate";
  paths = cutum::ResolveItemLabAssets(assets, "models/items/bucket/model.gltf",
                                      "bucket");
  Expect(paths.Staged && paths.Candidate == staged,
         "development candidate resolves staging");
  fs::create_directories(reference.parent_path());
  std::ofstream(reference) << "baseline";
  paths = cutum::ResolveItemLabAssets(assets, "models/items/bucket/model.gltf",
                                      "bucket");
  Expect(paths.Archived && paths.Reference == reference &&
             paths.Candidate == staged,
         "archive comparison retains staged candidate precedence");
  fs::remove(staged);
  paths = cutum::ResolveItemLabAssets(assets, "models/items/bucket/model.gltf",
                                      "bucket");
  Expect(paths.Archived && !paths.Staged && paths.Candidate == published &&
             paths.Reference != paths.Candidate,
         "packaged comparison uses baseline and current production");
  for (const auto &id : {"../bucket", ""})
  {
    bool rejected = false;
    try
    {
      cutum::ResolveItemLabAssets(assets, "models/items/bucket/model.gltf", id);
    }
    catch (const std::invalid_argument &)
    {
      rejected = true;
    }
    Expect(rejected, "unsafe Lab item ID rejected");
  }
  bool rejected = false;
  try
  {
    cutum::ResolveItemLabAssets(assets, "../outside.gltf", "bucket");
  }
  catch (const std::invalid_argument &)
  {
    rejected = true;
  }
  Expect(rejected, "Lab published path cannot escape asset root");
}

void TestVisualState()
{
  using namespace cutum;
  ItemWearSection section;
  section.Low = {-.1f, 0, -.2f};
  section.High = {.1f, .3f, .2f};
  section.EulerDeg = {0, 180, 0};
  const glm::vec3 regionLow(.4f, 0, .2f), regionHigh(.6f, .3f, .6f);
  const auto wear = ItemWearSectionMatrix(section, regionLow, regionHigh);
  Expect(glm::length(glm::vec3(wear * glm::vec4(0, .15f, 0, 1)) -
                     (regionLow + regionHigh) * .5f) < 1e-5f,
         "wear orientation keeps its anatomical reference center fixed");
  Expect(std::abs(glm::vec3(wear * glm::vec4(0, 0, .2f, 1)).z - .2f) < 1e-5f &&
             std::abs(glm::vec3(wear * glm::vec4(0, 0, .2f, 1)).y) < 1e-5f,
         "front-facing wear rotation preserves the sole plane");
  ItemRenderAsset a;
  a.Nodes = {{"root", glm::mat4(1), glm::mat4(1), -1},
             {"tip", glm::translate(glm::mat4(1), {0, 1, 0}),
              glm::translate(glm::mat4(1), {0, 1, 0}), 0},
             {"string", glm::translate(glm::mat4(1), {0, .5f, 0}),
              glm::translate(glm::mat4(1), {0, .5f, 0}), 0}};
  ItemRenderPrimitive p;
  p.Node = 2;
  p.Low = {-.01f, -.5f, -.01f};
  p.High = {.01f, .5f, .01f};
  a.Primitives.push_back(p);
  a.Low = {-.01f, 0, -.01f};
  a.High = {.01f, 1, .01f};
  ItemNodeBinding tip;
  tip.Node = 1;
  tip.TranslationTo = {0, 0, .4f};
  ItemNodeBinding span;
  span.Node = 2;
  span.Span = true;
  span.StartNode = 0;
  span.EndNode = 1;
  a.Bindings = {tip, span};
  ItemVisualInputs full;
  full.BowCharge = 1;
  const auto state = EvaluateItemVisualState(a, full);
  const auto transforms = ResolveItemNodeTransforms(a, state);
  Expect(glm::length(glm::vec3(transforms[2] * glm::vec4(0, -.5f, 0, 1))) <
             1e-5f,
         "string remains connected to its first anchor");
  Expect(glm::length(glm::vec3(transforms[2] * glm::vec4(0, .5f, 0, 1)) -
                     glm::vec3(0, 1, .4f)) < 1e-5f,
         "string follows its moving second anchor");
  const auto bounds = ItemVisualBounds(a, state);
  Expect(bounds.second.z >= .399f, "posed bounds include the moved string");
  const auto cancelled =
      ResolveItemNodeTransforms(a, EvaluateItemVisualState(a, {}));
  Expect(glm::length(cancelled[2][3] - a.Nodes[2].Transform[3]) < 1e-5f,
         "cancel restores the rest pivot");
  a.Bindings.clear();
  ItemNodeBinding fill;
  fill.Node = 2;
  fill.Channel = ItemVisualChannel::FillUnits;
  fill.Maximum = 4;
  fill.VisibleAbove = .001f;
  fill.TranslationFrom = {0, -.6f, 0};
  a.Bindings.push_back(fill);
  ItemVisualInputs water;
  water.HasWater = true;
  water.ContentsUnits = 2;
  const auto half = EvaluateItemVisualState(a, water);
  Expect(ItemNodeVisible(a, half, 2), "nonempty water surface is visible");
  Expect(!ItemNodeVisible(a, EvaluateItemVisualState(a, {}), 2),
         "empty container hides its water surface");
  water.HasWater = false;
  Expect(!ItemNodeVisible(a, EvaluateItemVisualState(a, water), 2),
         "non-water contents do not fabricate water");
  ItemVisualState hidden;
  hidden.VisibleRoots.insert("tip");
  Expect(ItemNodeVisible(a, hidden, 1) && !ItemNodeVisible(a, hidden, 2) &&
             !ItemNodeVisible(a, hidden, 0),
         "selected armor section excludes siblings and ancestor meshes");
  Expect(ItemVisualBounds(a, hidden).first == glm::vec3(0),
         "section bounds do not reuse the whole item bounds");
  hidden.VisibleRoots.clear();
  hidden.HiddenNodes.insert("root");
  Expect(!ItemNodeVisible(a, hidden, 2),
         "node visibility inherits from parents");
  auto loaded = fill;
  loaded.Channel = ItemVisualChannel::BowLoaded;
  loaded.Maximum = 1;
  a.Bindings = {loaded};
  full.HasAmmo = false;
  Expect(!ItemNodeVisible(a, EvaluateItemVisualState(a, full), 2),
         "drawing without ammunition cannot display a nocked arrow");
  full.HasAmmo = true;
  Expect(ItemNodeVisible(a, EvaluateItemVisualState(a, full), 2),
         "drawing with ammunition displays the nocked arrow");
  full.BowCharge = 0;
  Expect(!ItemNodeVisible(a, EvaluateItemVisualState(a, full), 2),
         "cancelling draw removes the nocked arrow");
  fill.Channel = ItemVisualChannel::UsePhase;
  fill.Maximum = 1;
  fill.VisibleAbove = -1;
  fill.Pulse = true;
  fill.TranslationFrom = {0, 0, 0};
  fill.RotationTo = {0, 0, 20};
  a.Bindings = {fill};
  ItemVisualInputs cut;
  cut.UsePhase = .5f;
  const auto peak =
      ResolveItemNodeTransforms(a, EvaluateItemVisualState(a, cut));
  Expect(std::abs(peak[2][0][1]) > .3f,
         "successful-use pulse reaches its working pose");
  cut.UsePhase = 1;
  const auto end =
      ResolveItemNodeTransforms(a, EvaluateItemVisualState(a, cut));
  Expect(std::abs(end[2][0][1]) < 1e-5f, "completed pulse returns to rest");
  InventoryEntryRef brokenContainer;
  brokenContainer.empty = false;
  brokenContainer.broken = true;
  brokenContainer.contentsId = "water";
  brokenContainer.contentsUnits = 1;
  const auto preserved = MakeItemVisualInputs(&brokenContainer, 1, .5f, true);
  Expect(preserved.HasWater && preserved.ContentsUnits == 1 &&
             preserved.BowCharge == 0,
         "broken containers retain their visible contents without an active "
         "use gesture");
  ItemActionPulse pulse;
  ItemUseProgress held;
  held.Update("apple", 7, 12, .4f, .8f);
  Expect(std::abs(held.Phase("apple", 7, 12) - .5f) < 1e-5f,
         "held action follows the authoritative duration");
  Expect(held.Phase("apple", 8, 12) == 0 && held.Phase("apple", 7, 13) == 0,
         "held use cannot transfer across a source or mode selection");
  held.Update("apple", 7, 12, 10.f, .8f);
  Expect(held.Phase("apple", 7, 12) == 1.f,
         "ready hold stays at completion until release");
  held.Clear();
  Expect(held.Phase("apple", 7, 12) == 0,
         "cancel removes held state without a successful-action pulse");
  pulse.Begin("shears", 7, 12);
  pulse.Tick(.15f);
  Expect(pulse.Phase("shears", 7, 12) > .4f,
         "successful event advances with the actor clock");
  Expect(pulse.Phase("shears", 8, 12) == 0 && pulse.Phase("shears", 7, 13) == 0,
         "switching instance or selection cannot transfer an action pulse");
  pulse.Tick(1);
  Expect(pulse.Phase("shears", 7, 12) == 0,
         "finished action has no persistent visual state");
}
} // namespace

int main()
{
  TestVisualState();
  namespace fs = std::filesystem;
  using Json = nlohmann::json;
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto folder = fs::temp_directory_path() /
                      ("cubatarium-item-render-test-" + std::to_string(stamp));
  fs::create_directory(folder);
  // The test owns only these three explicitly named temporary files.
  struct Cleanup
  {
    fs::path Folder;
    ~Cleanup()
    {
      std::error_code ec;
      for (const auto *name : {"model.gltf", "model.bin", "albedo.png"})
        fs::remove(Folder / name, ec);
      fs::remove(Folder, ec);
    }
  } cleanup{folder};
  try
  {
    TestLabPaths(folder);
    TestInspectionIsolation();
    const float vertices[] = {0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1,
                              0, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1};
    std::vector<unsigned char> bytes(sizeof(vertices) + 3);
    std::memcpy(bytes.data(), vertices, sizeof(vertices));
    bytes[sizeof(vertices)] = 0;
    bytes[sizeof(vertices) + 1] = 1;
    bytes[sizeof(vertices) + 2] = 2;
    std::ofstream bin(folder / "model.bin", std::ios::binary);
    bin.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    bin.close();
    std::ofstream(folder / "albedo.png") << "external-image-exists";
    Json g = {
        {"asset", {{"version", "2.0"}}},
        {"buffers",
         Json::array({{{"uri", "model.bin"}, {"byteLength", bytes.size()}}})},
        {"bufferViews", Json::array({{{"buffer", 0},
                                      {"byteLength", sizeof(vertices)},
                                      {"byteStride", 32}},
                                     {{"buffer", 0},
                                      {"byteOffset", sizeof(vertices)},
                                      {"byteLength", 3}}})},
        {"accessors", Json::array({{{"bufferView", 0},
                                    {"componentType", 5126},
                                    {"type", "VEC3"},
                                    {"count", 3}},
                                   {{"bufferView", 0},
                                    {"byteOffset", 12},
                                    {"componentType", 5126},
                                    {"type", "VEC2"},
                                    {"count", 3}},
                                   {{"bufferView", 0},
                                    {"byteOffset", 20},
                                    {"componentType", 5126},
                                    {"type", "VEC3"},
                                    {"count", 3}},
                                   {{"bufferView", 1},
                                    {"componentType", 5121},
                                    {"type", "SCALAR"},
                                    {"count", 3}}})},
        {"images", Json::array({{{"uri", "albedo.png"}}})},
        {"textures", Json::array({{{"source", 0}}})},
        {"materials", Json::array({{{"name", "forged"},
                                    {"pbrMetallicRoughness",
                                     {{"baseColorTexture", {{"index", 0}}},
                                      {"roughnessFactor", .6},
                                      {"metallicFactor", .7}}}}})},
        {"meshes",
         Json::array(
             {{{"primitives",
                Json::array(
                    {{{"attributes",
                       {{"POSITION", 0}, {"TEXCOORD_0", 1}, {"NORMAL", 2}}},
                      {"indices", 3},
                      {"material", 0}}})}}})},
        {"nodes",
         Json::array(
             {{{"name", "root"}, {"translation", {2, 3, 4}}, {"children", {1}}},
              {{"name", "blade"}, {"mesh", 0}, {"scale", {2, 1, 1}}}})},
        {"scenes", Json::array({{{"nodes", {0}}}})},
        {"scene", 0}};
    auto write = [&](const Json &data)
    { std::ofstream(folder / "model.gltf") << data.dump(); };
    g["materials"][0]["emissiveFactor"] = {.25, .10, 0};
    g["extras"]["wear_sections"] = Json::array({{{"node", "blade"},
                                                 {"bones", {"body"}},
                                                 {"slot", 1},
                                                 {"region", "full"},
                                                 {"low", {0, 0, 0}},
                                                 {"high", {1, 1, 1}}}});
    g["extras"]["presentation"] = {
        {"right_grip", {0, .25, 0}},      {"fp_euler_deg", {62, 0, 0}},
        {"fp_aim_euler_deg", {0, 0, -8}}, {"tp_aim_euler_deg", {0, 12, 0}},
        {"support_grip_node", "blade"},   {"fp_scale", .65}};
    write(g);
    const auto asset = cutum::LoadItemRenderAsset(folder / "model.gltf");
    Expect(asset->WearSections.size() == 1 &&
               asset->WearSections[0].Node == "blade" &&
               asset->WearSections[0].Region == "full",
           "anatomical wear sections are retained");
    Expect(asset->Materials[0].Emissive == glm::vec3(.25f, .10f, 0),
           "linear emissive factor is retained independently of albedo");
    Expect(asset->Primitives.size() == 1 && asset->Nodes.size() == 2,
           "scene hierarchy is preserved");
    Expect(asset->Primitives[0].Vertices.size() == 24,
           "item vertex layout includes normals and UVs");
    Expect(asset->Primitives[0].Node == 1, "primitive retains node ownership");
    Expect(asset->Low == glm::vec3(2, 3, 4) &&
               asset->High == glm::vec3(4, 4, 4),
           "bounds include hierarchical transforms");
    Expect(std::abs(asset->Materials[0].Metallic - .7f) < .001f,
           "material factors are retained");
    Expect(asset->Grip.HasRightGrip &&
               asset->Grip.RightGrip == glm::vec3(0, .25, 0),
           "authored grip anchor is retained");
    Expect(asset->Grip.HasFpEuler && asset->Grip.FpEulerDeg.x == 62,
           "authored FP orientation is retained");
    Expect(cutum::ItemHasSupportGrip(*asset) &&
               asset->Grip.SupportGripIndex == 1 &&
               asset->Grip.FpAimEulerDeg.z == -8 &&
               asset->Grip.TpAimEulerDeg.y == 12,
           "bow contact resolves a named node and independent aim frames");
    Expect(!cutum::FindItemSurfaceAsset(folder / "model.gltf", 0),
           "legacy cache entries remain on the legacy path");
    auto versioned = g;
    versioned["extras"]["design_version"] = 3;
    write(versioned);
    Expect(bool(cutum::FindItemSurfaceAsset(folder / "model.gltf", 1)),
           "content revision invalidates a cached legacy result");
    const auto beforeLookups = cutum::GetItemAssetLookupStats();
    for (int i = 0; i < 10; ++i)
      Expect(bool(cutum::FindItemSurfaceAsset(folder / "model.gltf", 1)),
             "warm asset lookup retains the loaded model");
    const auto afterLookups = cutum::GetItemAssetLookupStats();
    Expect(afterLookups.CanonicalResolutions ==
                   beforeLookups.CanonicalResolutions &&
               afterLookups.FastHits == beforeLookups.FastHits + 10,
           "warm asset lookup does not resolve filesystem paths again");
    const auto cached = cutum::FindItemSurfaceAsset(folder / "model.gltf", 1);
    Expect(cutum::FindItemSurfaceAsset(folder / "." / "model.gltf", 1) ==
               cached,
           "alternate spelling shares canonical ownership");
    cutum::ClearItemSurfaceAssets();
    Expect(cutum::FindItemSurfaceAsset(folder / "model.gltf", 1) != cached,
           "explicit reload invalidates spelling aliases as well as assets");
    cutum::ClearItemSurfaceAssets();
    auto grip = asset->Grip;
    grip.FpHandOffset = glm::vec3(.1, .2, .3);
    grip.FpArmEulerDeg = glm::vec3(45, 10, 5);
    const glm::vec3 socket(.04, -.62, -.14);
    const auto moved =
        cutum::ApplyItemHandProfile(glm::mat4(1), socket, grip, false);
    Expect(glm::length(glm::vec3(moved * glm::vec4(socket, 1)) - socket -
                       grip.FpHandOffset) < 1e-5f,
           "arm pose preserves palm contact after rotation");
    auto physicalAsset = *asset;
    physicalAsset.Low = glm::vec3(-.2f, -.1f, -.05f);
    physicalAsset.High = glm::vec3(.2f, .1f, .05f);
    physicalAsset.Grip.TpEulerDeg = glm::vec3(15, 30, 12);
    physicalAsset.Grip.TpScale = 2.f;
    physicalAsset.Grip.LeftGrip = glm::vec3(.02f, .08f, 0);
    physicalAsset.Grip.HasLeftGrip = true;
    const glm::vec3 palm(.3f, -.5f, .1f);
    const auto body = glm::translate(glm::mat4(1), glm::vec3(3, 2, 1)) *
                      glm::rotate(glm::mat4(1), .6f, glm::vec3(0, 1, 0)) *
                      glm::scale(glm::mat4(1), glm::vec3(.9f));
    const auto bowLocal =
        cutum::ItemBowModelMatrix(physicalAsset, palm, glm::mat4(1), 1.2f);
    const auto bowWorld = cutum::ItemBowModelMatrix(
        physicalAsset, glm::vec3(body * glm::vec4(palm, 1)), body, 1.2f);
    for (const auto &point : {glm::vec3(0), glm::vec3(.2, .4, -.1)})
      Expect(glm::distance(glm::vec3(body * bowLocal * glm::vec4(point, 1)),
                           glm::vec3(bowWorld * glm::vec4(point, 1))) < 1e-5f,
             "world-space bow frames preserve the carrier scale like local "
             "frames");
    for (bool left : {false, true})
    {
      const auto local =
          cutum::ItemWieldLocalMatrix(physicalAsset, palm, left, 1.4f);
      const auto anchor =
          left ? physicalAsset.Grip.LeftGrip : physicalAsset.Grip.RightGrip;
      Expect(glm::length(glm::vec3(local * glm::vec4(anchor, 1)) - palm) <
                 1e-5f,
             "TP rotation and scale keep either grip on the palm");
    }
    const auto ground = cutum::ItemLooseLocalMatrix(
        physicalAsset, glm::vec3(.5f, .8f, .2f), true);
    Expect(std::abs(glm::vec3(ground * glm::vec4(physicalAsset.Low, 1)).y +
                    .4f) < 1e-5f,
           "the visual bottom rests on the collision proxy bottom");
    Expect(glm::length(glm::vec3(ground * glm::vec4(1, 0, 0, 0))) == 1.f,
           "loose items retain authored physical units without longest-axis "
           "enlargement");
    const float day = cutum::ItemWorldLightIntensity({1, 0}, 1, 0, 1, .12f);
    const float night = cutum::ItemWorldLightIntensity({1, 0}, 0, .2f, 1, .12f);
    const float interior =
        cutum::ItemWorldLightIntensity({0, 0}, 0, .2f, 1, .12f);
    const float torch = cutum::ItemWorldLightIntensity({0, 1}, 0, .2f, 1, .12f);
    Expect(day > night && night > interior && torch > night,
           "world item illumination distinguishes day, night, interior and "
           "block light");
    cutum::ItemVisualState pose;
    pose.NodeTransforms["root"] =
        glm::translate(glm::mat4(1), glm::vec3(1, 0, 0));
    const auto animated = cutum::ResolveItemNodeTransforms(*asset, pose);
    Expect(glm::vec3(animated[1][3]) == glm::vec3(3, 3, 4),
           "parent pose propagates to child geometry");
    const auto rest = cutum::ResolveItemNodeTransforms(*asset, {});
    Expect(rest[1] == asset->Nodes[1].Transform,
           "cancelled pose restores authored hierarchy");
    auto rejected = [&](Json invalid)
    {
      write(invalid);
      bool failed = false;
      try
      {
        cutum::LoadItemRenderAsset(folder / "model.gltf");
      }
      catch (const std::exception &)
      {
        failed = true;
      }
      Expect(failed, "invalid glTF was accepted");
    };
    auto bad = g;
    bad["extras"]["wear_sections"][0]["node"] = "missing";
    rejected(bad);
    bad = g;
    bad["extras"]["wear_sections"][0]["high"] = {0, 0, 0};
    rejected(bad);
    bad = g;
    bad["extras"]["wear_sections"].push_back(bad["extras"]["wear_sections"][0]);
    rejected(bad);
    bad = g;
    bad["materials"][0]["emissiveFactor"] = {1.1, 0, 0};
    rejected(bad);
    bad = g;
    bad["materials"][0]["emissiveFactor"] = {0, -1, 0};
    rejected(bad);
    bad = g;
    bad["materials"][0]["emissiveFactor"] = {0, 0};
    rejected(bad);
    bad = g;
    bad["extras"]["visual_bindings"] =
        Json::array({{{"node", "missing"}, {"channel", "bow_charge"}}});
    rejected(bad);
    bad = g;
    bad["extras"]["presentation"]["tp_scale"] = -2;
    rejected(bad);
    bad = g;
    bad["extras"]["presentation"]["support_grip_node"] = "missing";
    rejected(bad);
    bad = g;
    bad["extras"]["presentation"]["support_grip_node"] = 3;
    rejected(bad);
    bad = g;
    bad["extras"]["presentation"]["fp_aim_euler_deg"] = {1, 2};
    rejected(bad);
    bad = g;
    bad["accessors"][0]["count"] = 1000;
    rejected(bad);
    bad = g;
    bad["images"][0]["uri"] = "../outside.png";
    rejected(bad);
    bad = g;
    bad["nodes"][1]["children"] = {0};
    rejected(bad);
    bad = g;
    bad["nodes"][1]["scale"] = {0, 1, 1};
    rejected(bad);
    bad = g;
    bad["materials"][0]["pbrMetallicRoughness"]["metallicFactor"] = 2;
    rejected(bad);
    bad = g;
    bad["meshes"][0]["primitives"][0]["mode"] = 1;
    rejected(bad);
    bad = g;
    bad["meshes"][0]["primitives"][0]["attributes"].erase("NORMAL");
    rejected(bad);
    const auto legacy = cutum::LoadItemRenderAsset(folder / "model.gltf", true);
    Expect(legacy->Primitives[0].Vertices[7] == 1.f,
           "legacy inspection computes normals without relaxing production "
           "loading");
    bad["extras"]["design_version"] = 3;
    write(bad);
    bool rejectedPreview = false;
    try
    {
      cutum::LoadItemRenderAsset(folder / "model.gltf", true);
    }
    catch (const std::exception &)
    {
      rejectedPreview = true;
    }
    Expect(rejectedPreview, "inspection must not mask missing v3 normals");
    bad = g;
    bad["accessors"][3]["count"] = 2;
    rejected(bad);
    std::cout << "ItemRenderAsset: hierarchy, bounds, materials, stride and "
                 "rejection cases pass\n";
    return 0;
  }
  catch (const std::exception &ex)
  {
    std::cerr << ex.what() << '\n';
    return 1;
  }
}

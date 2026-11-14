#include "Items/ItemRenderAsset.h"

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
} // namespace

int main()
{
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
    write(g);
    const auto asset = cutum::LoadItemRenderAsset(folder / "model.gltf");
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

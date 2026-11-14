#include "Items/ItemRenderAsset.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_map>

namespace cutum
{
namespace
{
using Json = nlohmann::json;
std::mutex AssetMutex;
uint64_t AssetsRevision = 0;
std::unordered_map<std::string, std::shared_ptr<const ItemRenderAsset>> Assets;
void Require(bool condition, const char *reason)
{
  if (!condition)
    throw std::runtime_error(std::string("Item glTF: ") + reason);
}

std::filesystem::path Resource(const std::filesystem::path &folder,
                               const std::string &uri)
{
  Require(!uri.empty() && uri.find(':') == std::string::npos,
          "only relative external resources are supported");
  const auto root = std::filesystem::weakly_canonical(folder);
  const auto path = std::filesystem::weakly_canonical(folder / uri);
  const auto relative = path.lexically_relative(root);
  Require(!relative.empty() && *relative.begin() != ".." &&
              !relative.is_absolute(),
          "resource escapes the item directory");
  Require(std::filesystem::is_regular_file(path), "missing external resource");
  return path;
}

struct Reader
{
  Json Data;
  std::vector<std::vector<unsigned char>> Buffers;

  std::vector<double> Read(size_t index, const char *type) const
  {
    const auto &a = Data.at("accessors").at(index);
    Require(!a.contains("sparse") && !a.value("normalized", false),
            "sparse/normalized item accessors are unsupported");
    Require(a.at("type") == type, "accessor has the wrong vector width");
    const size_t width = std::string(type) == "VEC3"   ? 3
                         : std::string(type) == "VEC2" ? 2
                                                       : 1;
    const int component = a.at("componentType");
    Require((width != 1 && component == 5126) ||
                (width == 1 &&
                 (component == 5121 || component == 5123 || component == 5125)),
            "unsupported item accessor component type");
    const size_t bytes = component == 5121 ? 1 : component == 5123 ? 2 : 4;
    const auto &view =
        Data.at("bufferViews").at(a.at("bufferView").get<size_t>());
    const auto &buffer = Buffers.at(view.at("buffer").get<size_t>());
    const size_t viewOffset = view.value("byteOffset", size_t(0));
    const size_t viewLength = view.at("byteLength");
    Require(viewOffset <= buffer.size() &&
                viewLength <= buffer.size() - viewOffset,
            "bufferView exceeds buffer");
    const size_t offset = a.value("byteOffset", size_t(0));
    const size_t stride = view.value("byteStride", bytes * width);
    const size_t count = a.at("count");
    Require(count > 0 && count <= 1000000 && stride >= bytes * width &&
                stride % bytes == 0 && offset % bytes == 0 &&
                offset <= viewLength && bytes * width <= viewLength - offset &&
                count - 1 <= (viewLength - offset - bytes * width) / stride,
            "accessor exceeds bufferView or item budget");
    std::vector<double> out;
    out.reserve(count * width);
    for (size_t i = 0; i < count; ++i)
      for (size_t j = 0; j < width; ++j)
      {
        const auto *p =
            buffer.data() + viewOffset + offset + i * stride + j * bytes;
        double value = 0;
        if (component == 5126)
        {
          float number;
          std::memcpy(&number, p, 4);
          value = number;
        }
        else
        {
          unsigned number = 0;
          for (size_t k = 0; k < bytes; ++k)
            number |= unsigned(p[k]) << (8 * k);
          value = number;
        }
        Require(std::isfinite(value), "non-finite vertex value");
        out.push_back(value);
      }
    return out;
  }
};

glm::mat4 LocalMatrix(const Json &node)
{
  if (node.contains("matrix"))
  {
    Require(!node.contains("translation") && !node.contains("rotation") &&
                !node.contains("scale"),
            "matrix and TRS cannot coexist");
    const auto values = node.at("matrix").get<std::vector<float>>();
    Require(values.size() == 16, "matrix must contain 16 values");
    glm::mat4 result(1.f);
    for (size_t i = 0; i < 16; ++i)
    {
      Require(std::isfinite(values[i]), "non-finite node matrix");
      result[int(i / 4)][int(i % 4)] = values[i];
    }
    Require(std::abs(glm::determinant(result)) > 1e-8f, "singular node matrix");
    return result;
  }
  const auto t = node.value("translation", std::vector<float>{0, 0, 0});
  const auto s = node.value("scale", std::vector<float>{1, 1, 1});
  const auto r = node.value("rotation", std::vector<float>{0, 0, 0, 1});
  Require(t.size() == 3 && s.size() == 3 && r.size() == 4, "invalid node TRS");
  for (const auto &values : {t, s, r})
    for (float v : values)
      Require(std::isfinite(v), "non-finite node TRS");
  const glm::quat rotation(r[3], r[0], r[1], r[2]);
  Require(std::abs(glm::length(rotation) - 1.f) < 1e-3f && s[0] > 0 &&
              s[1] > 0 && s[2] > 0,
          "node requires unit rotation and positive scale");
  return glm::translate(glm::mat4(1), glm::vec3(t[0], t[1], t[2])) *
         glm::mat4_cast(rotation) *
         glm::scale(glm::mat4(1), glm::vec3(s[0], s[1], s[2]));
}
} // namespace

std::shared_ptr<ItemRenderAsset>
LoadItemRenderAsset(const std::filesystem::path &path,
                    bool legacyPreviewNormals)
{
  Reader reader;
  std::ifstream in(path);
  Require(bool(in), "cannot open model");
  in >> reader.Data;
  const auto &g = reader.Data;
  Require(g.at("asset").at("version") == "2.0", "expected glTF 2.0");
  Require(!g.contains("skins") && !g.contains("animations") &&
              g.value("extensionsRequired", Json::array()).empty(),
          "skins, animation clips and required extensions are unsupported");
  for (const auto &b : g.at("buffers"))
  {
    std::ifstream bin(Resource(path.parent_path(), b.at("uri")),
                      std::ios::binary);
    reader.Buffers.emplace_back(std::istreambuf_iterator<char>(bin),
                                std::istreambuf_iterator<char>());
    Require(reader.Buffers.back().size() == b.at("byteLength").get<size_t>(),
            "buffer length mismatch");
  }
  auto asset = std::make_shared<ItemRenderAsset>();
  const auto presentation =
      g.value("extras", Json::object()).value("presentation", Json::object());
  auto readVector = [&](const char *key, glm::vec3 &result)
  {
    if (!presentation.contains(key))
      return false;
    const auto values = presentation.at(key).get<std::vector<float>>();
    Require(values.size() == 3,
            "presentation vector requires three components");
    for (size_t i = 0; i < 3; ++i)
    {
      Require(std::isfinite(values[i]), "non-finite presentation vector");
      result[int(i)] = values[i];
    }
    return true;
  };
  asset->Grip.HasRightGrip = readVector("right_grip", asset->Grip.RightGrip);
  asset->Grip.HasLeftGrip = readVector("left_grip", asset->Grip.LeftGrip);
  asset->Grip.HasFpEuler = readVector("fp_euler_deg", asset->Grip.FpEulerDeg);
  readVector("fp_hand_offset", asset->Grip.FpHandOffset);
  readVector("icon_euler_deg", asset->Grip.IconEulerDeg);
  readVector("fp_arm_euler_deg", asset->Grip.FpArmEulerDeg);
  asset->Grip.FpScale = presentation.value("fp_scale", 1.f);
  Require(std::isfinite(asset->Grip.FpScale) && asset->Grip.FpScale >= .1f &&
              asset->Grip.FpScale <= 3.f,
          "invalid FP presentation scale");
  for (const auto &m : g.at("materials"))
  {
    Require(!m.value("doubleSided", false) &&
                m.value("alphaMode", "OPAQUE") == "OPAQUE" &&
                !m.contains("normalTexture") && !m.contains("extensions"),
            "only opaque base-color item materials are supported");
    const auto &pbr = m.at("pbrMetallicRoughness");
    ItemRenderMaterial material;
    material.Name = m.value("name", "");
    material.Metallic = pbr.value("metallicFactor", 1.f);
    material.Roughness = pbr.value("roughnessFactor", 1.f);
    const auto color =
        pbr.value("baseColorFactor", std::vector<float>{1, 1, 1, 1});
    Require(color.size() == 4, "base color requires RGBA");
    for (size_t i = 0; i < 4; ++i)
    {
      Require(std::isfinite(color[i]) && color[i] >= 0 && color[i] <= 1,
              "invalid base color");
      material.Color[int(i)] = color[i];
    }
    Require(std::isfinite(material.Metallic) && material.Metallic >= 0 &&
                material.Metallic <= 1 && std::isfinite(material.Roughness) &&
                material.Roughness >= 0 && material.Roughness <= 1,
            "material factors must be in 0..1");
    const auto &info = pbr.at("baseColorTexture");
    Require(info.value("texCoord", 0) == 0 && !info.contains("extensions"),
            "only TEXCOORD_0 without texture transforms is supported");
    const auto &tex = g.at("textures").at(info.at("index").get<size_t>());
    const auto &image = g.at("images").at(tex.at("source").get<size_t>());
    material.Albedo = Resource(path.parent_path(), image.at("uri"));
    asset->Materials.push_back(std::move(material));
  }
  asset->Low = glm::vec3(std::numeric_limits<float>::max());
  asset->High = -asset->Low;
  std::vector<bool> visited(g.at("nodes").size(), false);
  std::function<void(size_t, const glm::mat4 &, int)> visit;
  visit = [&](size_t index, const glm::mat4 &parent, int parentIndex)
  {
    Require(index < visited.size() && !visited[index],
            "cyclic or shared scene node");
    visited[index] = true;
    const auto &node = g.at("nodes").at(index);
    Require(!node.contains("skin"), "skinned item nodes are unsupported");
    const glm::mat4 matrix = parent * LocalMatrix(node);
    const size_t nodeIndex = asset->Nodes.size();
    asset->Nodes.push_back(
        {node.value("name", ""), matrix, LocalMatrix(node), parentIndex});
    if (node.contains("mesh"))
      for (const auto &p :
           g.at("meshes").at(node.at("mesh").get<size_t>()).at("primitives"))
      {
        Require(p.value("mode", 4) == 4 && !p.contains("targets"),
                "only static triangles are supported");
        const auto &attrs = p.at("attributes");
        const auto positions = reader.Read(attrs.at("POSITION"), "VEC3");
        const auto indices = reader.Read(p.at("indices"), "SCALAR");
        Require(indices.size() % 3 == 0, "triangle indices must be triples");
        for (double index : indices)
          Require(index >= 0 && index < positions.size() / 3 &&
                      std::floor(index) == index,
                  "index outside vertex buffer");
        std::vector<double> normals;
        if (attrs.contains("NORMAL"))
          normals = reader.Read(attrs.at("NORMAL"), "VEC3");
        else
        {
          Require(legacyPreviewNormals &&
                      g.value("extras", Json::object())
                              .value("design_version", 0) != 3,
                  "item requires authored normals");
          std::vector<glm::vec3> sums(positions.size() / 3, glm::vec3(0));
          auto point = [&](size_t i)
          {
            return glm::vec3(positions[i * 3], positions[i * 3 + 1],
                             positions[i * 3 + 2]);
          };
          for (size_t i = 0; i < indices.size(); i += 3)
          {
            const auto a = size_t(indices[i]), b = size_t(indices[i + 1]),
                       c = size_t(indices[i + 2]);
            const auto n = glm::cross(point(b) - point(a), point(c) - point(a));
            sums[a] += n;
            sums[b] += n;
            sums[c] += n;
          }
          for (const auto &n : sums)
          {
            Require(glm::length(n) > 1e-8f,
                    "legacy preview has degenerate normals");
            const auto unit = glm::normalize(n);
            normals.insert(normals.end(), {unit.x, unit.y, unit.z});
          }
        }
        const auto uv = reader.Read(attrs.at("TEXCOORD_0"), "VEC2");
        Require(normals.size() == positions.size() &&
                    uv.size() / 2 == positions.size() / 3,
                "vertex attribute counts differ");
        ItemRenderPrimitive primitive;
        primitive.Node = nodeIndex;
        primitive.Material = p.at("material");
        Require(primitive.Material < asset->Materials.size(),
                "invalid material index");
        for (size_t i = 0; i < positions.size() / 3; ++i)
        {
          const glm::vec3 point(positions[i * 3], positions[i * 3 + 1],
                                positions[i * 3 + 2]);
          const glm::vec3 normal(normals[i * 3], normals[i * 3 + 1],
                                 normals[i * 3 + 2]);
          Require(std::abs(glm::length(normal) - 1.f) < 1e-3f,
                  "normals must be unit length");
          const auto world = glm::vec3(matrix * glm::vec4(point, 1));
          asset->Low = glm::min(asset->Low, world);
          asset->High = glm::max(asset->High, world);
          primitive.Vertices.insert(primitive.Vertices.end(),
                                    {point.x, point.y, point.z,
                                     float(uv[i * 2]), float(uv[i * 2 + 1]),
                                     normal.x, normal.y, normal.z});
        }
        Require(indices.size() % 3 == 0, "triangle indices must be triples");
        for (double value : indices)
        {
          Require(value >= 0 && value < positions.size() / 3 &&
                      std::floor(value) == value,
                  "index outside vertex buffer");
          primitive.Indices.push_back(unsigned(value));
        }
        asset->Primitives.push_back(std::move(primitive));
      }
    for (const auto &child : node.value("children", Json::array()))
      visit(child.get<size_t>(), matrix, int(nodeIndex));
  };
  for (const auto &index :
       g.at("scenes").at(g.value("scene", size_t(0))).at("nodes"))
    visit(index.get<size_t>(), glm::mat4(1), -1);
  Require(!asset->Primitives.empty(), "scene contains no geometry");
  return asset;
}
std::shared_ptr<const ItemRenderAsset>
FindItemSurfaceAsset(const std::filesystem::path &path, uint64_t revision)
{
  const auto key = std::filesystem::weakly_canonical(path).generic_string();
  std::lock_guard<std::mutex> lock(AssetMutex);
  if (revision != AssetsRevision)
  {
    Assets.clear();
    AssetsRevision = revision;
  }
  const auto found = Assets.find(key);
  if (found != Assets.end())
    return found->second;
  std::ifstream in(path);
  Json data;
  if (!in)
    return nullptr;
  in >> data;
  std::shared_ptr<const ItemRenderAsset> asset;
  if (data.value("extras", Json::object()).value("design_version", 0) == 3)
    asset = LoadItemRenderAsset(path);
  Assets.emplace(key, asset);
  return asset;
}

glm::mat4 ApplyItemHandProfile(const glm::mat4 &root, const glm::vec3 &socket,
                               const ItemGripProfile &profile, bool left)
{
  auto offset = profile.FpHandOffset;
  auto angles = glm::radians(profile.FpArmEulerDeg);
  if (left)
  {
    offset.x = -offset.x;
    angles.y = -angles.y;
    angles.z = -angles.z;
  }
  const auto translated = glm::translate(glm::mat4(1), offset) * root;
  const auto palm = glm::vec3(translated * glm::vec4(socket, 1));
  const auto rotation =
      glm::rotate(glm::mat4(1), angles.x, glm::vec3(1, 0, 0)) *
      glm::rotate(glm::mat4(1), angles.y, glm::vec3(0, 1, 0)) *
      glm::rotate(glm::mat4(1), angles.z, glm::vec3(0, 0, 1));
  return glm::translate(glm::mat4(1), palm) * rotation *
         glm::translate(glm::mat4(1), -palm) * translated;
}

void ClearItemSurfaceAssets()
{
  std::lock_guard<std::mutex> lock(AssetMutex);
  Assets.clear();
}
std::vector<glm::mat4> ResolveItemNodeTransforms(const ItemRenderAsset &asset,
                                                 const ItemVisualState &state)
{
  std::vector<glm::mat4> result;
  result.reserve(asset.Nodes.size());
  for (const auto &node : asset.Nodes)
  {
    Require(node.Parent < 0 || size_t(node.Parent) < result.size(),
            "node parent must precede its child");
    const auto transform = state.NodeTransforms.find(node.Name);
    result.push_back(
        (node.Parent < 0 ? glm::mat4(1) : result[size_t(node.Parent)]) *
        node.Local *
        (transform == state.NodeTransforms.end() ? glm::mat4(1)
                                                 : transform->second));
  }
  return result;
}
} // namespace cutum

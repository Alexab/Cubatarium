#include "Items/ItemRenderAsset.h"
#include "Core/Memory/VisualAssetCensus.h"
#include "Core/Memory/VisualLoaderSource.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace cutum
{
namespace
{
using Json = nlohmann::json;
std::mutex AssetMutex;
uint64_t AssetsRevision = 0;
std::unordered_map<std::string, std::shared_ptr<const ItemRenderAsset>> Assets;
std::unordered_map<std::string, std::string> AssetAliases;
ItemAssetLookupStats AssetLookupStats;
} // namespace

void MeasureVisualPayload(const ItemRenderAsset &asset, StlSourceStorage &out) noexcept
{
  out.Buffer(asset.Primitives.capacity(), sizeof(ItemRenderPrimitive));
  for (const auto &primitive : asset.Primitives)
  {
    out.Buffer(primitive.Vertices.capacity(), sizeof(float));
    out.Buffer(primitive.Indices.capacity(), sizeof(unsigned));
  }
  out.Buffer(asset.Materials.capacity(), sizeof(ItemRenderMaterial));
  for (const auto &material : asset.Materials)
  {
    out.Text(material.Name);
    out.Text(material.Albedo.native());
  }
  out.Buffer(asset.Nodes.capacity(), sizeof(ItemRenderNode));
  for (const auto &node : asset.Nodes)
    out.Text(node.Name);
  out.Text(asset.Grip.SupportGripNode);
  out.Buffer(asset.Bindings.capacity(), sizeof(ItemNodeBinding));
  out.Buffer(asset.WearSections.capacity(), sizeof(ItemWearSection));
  for (const auto &section : asset.WearSections)
  {
    out.Text(section.Node);
    out.Text(section.Region);
    out.Buffer(section.Bones.capacity(), sizeof(std::string));
    for (const auto &bone : section.Bones)
      out.Text(bone);
  }
  out.Hash(asset.StaticWearSections);
  for (const auto &[name, section] : asset.StaticWearSections)
  {
    out.Text(name);
    out.Buffer(section.Primitives.capacity(), sizeof(size_t));
  }
}

namespace
{
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
  CatalogJsonDocument Data;
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

template<size_t N>
std::array<float, N> ReadFloatArray(const Json &object, const char *key,
                                   std::array<float, N> result, const char *reason)
{
  const auto values = object.find(key);
  if (values == object.end())
    return result;
  Require(values->is_array() && values->size() == N, reason);
  for (size_t i = 0; i < N; ++i)
  {
    const float value = (*values)[i].get<float>();
    Require(std::isfinite(value), reason);
    result[i] = value;
  }
  return result;
}

const Json *ItemExtras(const Json &gltf)
{
  Require(gltf.is_object(), "expected a glTF document");
  const auto extras = gltf.find("extras");
  if (extras == gltf.end())
    return nullptr;
  Require(extras->is_object(), "item extras must be an object");
  return &*extras;
}

glm::mat4 LocalMatrix(const Json &node)
{
  if (node.contains("matrix"))
  {
    Require(!node.contains("translation") && !node.contains("rotation") &&
                !node.contains("scale"),
            "matrix and TRS cannot coexist");
    const auto values = ReadFloatArray<16>(node, "matrix", {},
                                          "matrix must contain 16 finite values");
    glm::mat4 result(1.f);
    for (size_t i = 0; i < 16; ++i)
      result[int(i / 4)][int(i % 4)] = values[i];
    Require(std::abs(glm::determinant(result)) > 1e-8f, "singular node matrix");
    return result;
  }
  const auto t = ReadFloatArray<3>(node, "translation", {}, "invalid node translation");
  const auto s = ReadFloatArray<3>(node, "scale", {1, 1, 1}, "invalid node scale");
  const auto r = ReadFloatArray<4>(node, "rotation", {0, 0, 0, 1}, "invalid node rotation");
  const glm::quat rotation(r[3], r[0], r[1], r[2]);
  Require(std::abs(glm::length(rotation) - 1.f) < 1e-3f && s[0] > 0 &&
              s[1] > 0 && s[2] > 0,
          "node requires unit rotation and positive scale");
  return glm::translate(glm::mat4(1), glm::vec3(t[0], t[1], t[2])) *
         glm::mat4_cast(rotation) *
         glm::scale(glm::mat4(1), glm::vec3(s[0], s[1], s[2]));
}
void ReadVisualBindings(const Json &data, ItemRenderAsset &asset)
{
  Require(data.is_array(), "visual bindings must be an array");
  auto nodeIndex = [&](const std::string &name)
  {
    size_t found = asset.Nodes.size();
    for (size_t i = 0; i < asset.Nodes.size(); ++i)
      if (asset.Nodes[i].Name == name)
      {
        Require(found == asset.Nodes.size(), "binding node name is ambiguous");
        found = i;
      }
    Require(found < asset.Nodes.size(), "binding references missing node");
    return found;
  };
  auto vector = [&](const Json &row, const char *key, glm::vec3 fallback)
  {
    if (!row.contains(key))
      return fallback;
    const auto values = row.at(key).get<std::vector<float>>();
    Require(values.size() == 3, "binding vector needs three components");
    for (int i = 0; i < 3; ++i)
    {
      Require(std::isfinite(values[i]), "binding vector must be finite");
      fallback[i] = values[i];
    }
    return fallback;
  };
  std::unordered_set<size_t> controlled;
  for (const auto &row : data)
  {
    ItemNodeBinding b;
    b.Node = nodeIndex(row.at("node"));
    Require(controlled.insert(b.Node).second,
            "node has duplicate visual bindings");
    if (row.contains("span"))
    {
      b.Span = true;
      const auto &span = row.at("span");
      b.StartNode = nodeIndex(span.at("start_node"));
      b.EndNode = nodeIndex(span.at("end_node"));
      b.StartPoint = vector(span, "start_point", glm::vec3(0));
      b.EndPoint = vector(span, "end_point", glm::vec3(0));
      const auto a = glm::vec3(asset.Nodes[b.StartNode].Transform *
                               glm::vec4(b.StartPoint, 1));
      const auto z = glm::vec3(asset.Nodes[b.EndNode].Transform *
                               glm::vec4(b.EndPoint, 1));
      Require(glm::length(z - a) > 1e-5f, "span has coincident rest anchors");
      const auto parent = asset.Nodes[b.Node].Parent;
      Require(parent >= 0 && asset.Nodes[size_t(parent)].Parent < 0,
              "span output must be a root child");
    }
    else
    {
      const std::string channel = row.at("channel");
      if (channel == "bow_charge")
        b.Channel = ItemVisualChannel::BowCharge;
      else if (channel == "bow_loaded")
        b.Channel = ItemVisualChannel::BowLoaded;
      else if (channel == "use_phase")
        b.Channel = ItemVisualChannel::UsePhase;
      else if (channel == "fill_units")
        b.Channel = ItemVisualChannel::FillUnits;
      else
        Require(false, "unsupported visual input channel");
      const auto range = row.value("range", std::vector<float>{0, 1});
      Require(range.size() == 2 && std::isfinite(range[0]) &&
                  std::isfinite(range[1]) && range[1] > range[0],
              "invalid visual input range");
      b.Minimum = range[0];
      b.Maximum = range[1];
      const std::string curve = row.value("curve", std::string("linear"));
      Require(curve == "linear" || curve == "pulse",
              "unsupported visual input curve");
      b.Pulse = curve == "pulse";
      b.TranslationFrom = vector(row, "translation_from", glm::vec3(0));
      b.TranslationTo = vector(row, "translation_to", glm::vec3(0));
      b.RotationFrom = vector(row, "rotation_from", glm::vec3(0));
      b.RotationTo = vector(row, "rotation_to", glm::vec3(0));
      b.ScaleFrom = vector(row, "scale_from", glm::vec3(1));
      b.ScaleTo = vector(row, "scale_to", glm::vec3(1));
      Require(glm::all(glm::greaterThan(b.ScaleFrom, glm::vec3(.001f))) &&
                  glm::all(glm::greaterThan(b.ScaleTo, glm::vec3(.001f))),
              "binding scale must stay positive");
      b.VisibleAbove = row.value("visible_above", -1.f);
      Require(std::isfinite(b.VisibleAbove),
              "visibility threshold must be finite");
    }
    asset.Bindings.push_back(b);
  }
  for (const auto &span : asset.Bindings)
    if (span.Span)
      for (const auto source : {span.StartNode, span.EndNode})
        for (int node = int(source); node >= 0;
             node = asset.Nodes[size_t(node)].Parent)
          for (const auto &other : asset.Bindings)
            Require(!other.Span || other.Node != size_t(node),
                    "span links must not depend on span outputs");
}
void ReadWearSections(const Json &rows, ItemRenderAsset &asset)
{
  Require(rows.is_array(), "wear sections must be an array");
  std::unordered_set<std::string> assigned;
  for (const auto &row : rows)
  {
    ItemWearSection section;
    section.Node = row.at("node").get<std::string>();
    section.Slot = row.at("slot").get<size_t>();
    section.Region = row.value("region", std::string("slot"));
    section.Bones = row.at("bones").get<std::vector<std::string>>();
    Require(section.Slot < 4 && !section.Bones.empty(),
            "invalid wear slot or bones");
    Require(section.Region == "slot" || section.Region == "full" ||
                section.Region == "waist",
            "unknown wear region");
    const auto count =
        std::count_if(asset.Nodes.begin(), asset.Nodes.end(),
                      [&](const auto &n) { return n.Name == section.Node; });
    Require(count == 1, "wear section node is missing or ambiguous");
    bool geometry = false;
    for (const auto &primitive : asset.Primitives)
      for (int node = int(primitive.Node); node >= 0;
           node = asset.Nodes[size_t(node)].Parent)
        geometry = geometry || asset.Nodes[size_t(node)].Name == section.Node;
    Require(geometry, "wear section has no descendant geometry");
    for (const auto &bone : section.Bones)
      Require(
          !bone.empty() &&
              assigned.insert(std::to_string(section.Slot) + ":" + bone).second,
          "duplicate or empty wear bone");
    const auto low = row.at("low").get<std::vector<float>>();
    const auto high = row.at("high").get<std::vector<float>>();
    const auto euler = row.value("euler_deg", std::vector<float>{0, 0, 0});
    Require(euler.size() == 3, "wear orientation requires XYZ");
    Require(low.size() == 3 && high.size() == 3, "wear bounds require XYZ");
    for (int i = 0; i < 3; ++i)
    {
      Require(std::isfinite(low[size_t(i)]) && std::isfinite(high[size_t(i)]) &&
                  low[size_t(i)] < high[size_t(i)],
              "invalid wear reference volume");
      section.Low[i] = low[size_t(i)];
      section.High[i] = high[size_t(i)];
      Require(std::isfinite(euler[size_t(i)]), "non-finite wear orientation");
      section.EulerDeg[i] = euler[size_t(i)];
    }
    asset.WearSections.push_back(std::move(section));
  }
}
} // namespace

namespace
{
std::shared_ptr<ItemRenderAsset>
DecodeItemRenderAsset(const std::filesystem::path &path, CatalogJsonDocument data,
                      bool legacyPreviewNormals, VisualLoaderSource &source)
{
  Reader reader;
  reader.Data = std::move(data);
  const auto &g = reader.Data;
  Require(g.at("asset").at("version") == "2.0", "expected glTF 2.0");
  const auto requiredExtensions = g.find("extensionsRequired");
  Require(!g.contains("skins") && !g.contains("animations") &&
              (requiredExtensions == g.end() || requiredExtensions->empty()),
          "skins, animation clips and required extensions are unsupported");
  const auto &buffers = g.at("buffers");
  reader.Buffers.reserve(buffers.size());
  source.BinaryTable(reader.Buffers);
  for (const auto &b : buffers)
  {
    auto bytes = ReadVisualModelBuffer(Resource(path.parent_path(), b.at("uri")), source,
                                       b.at("byteLength").get<size_t>());
    Require(bytes.has_value(), "could not read declared buffer length");
    reader.Buffers.push_back(std::move(*bytes));
  }
  auto asset = std::make_shared<ItemRenderAsset>();
  const auto *extras = ItemExtras(g);
  const auto *presentation = extras && extras->contains("presentation") ?
      &extras->at("presentation") : nullptr;
  Require(!presentation || presentation->is_object(), "item presentation must be an object");
  auto readVector = [&](const char *key, glm::vec3 &result)
  {
    if (!presentation || !presentation->contains(key))
      return false;
    const auto values = ReadFloatArray<3>(*presentation, key, {},
                                          "presentation vector requires three finite components");
    result = glm::vec3(values[0], values[1], values[2]);
    return true;
  };
  asset->Grip.HasRightGrip = readVector("right_grip", asset->Grip.RightGrip);
  asset->Grip.HasLeftGrip = readVector("left_grip", asset->Grip.LeftGrip);
  asset->Grip.HasFpEuler = readVector("fp_euler_deg", asset->Grip.FpEulerDeg);
  readVector("fp_hand_offset", asset->Grip.FpHandOffset);
  readVector("icon_euler_deg", asset->Grip.IconEulerDeg);
  readVector("tp_euler_deg", asset->Grip.TpEulerDeg);
  asset->Grip.HasTpLeftEuler =
      readVector("tp_left_euler_deg", asset->Grip.TpLeftEulerDeg);
  asset->Grip.HasFpUseHandOffset =
      readVector("fp_use_hand_offset", asset->Grip.FpUseHandOffset);
  asset->Grip.HasFpUseEuler =
      readVector("fp_use_euler_deg", asset->Grip.FpUseEulerDeg);
  readVector("fp_arm_euler_deg", asset->Grip.FpArmEulerDeg);
  readVector("ground_euler_deg", asset->Grip.GroundEulerDeg);
  readVector("carry_euler_deg", asset->Grip.CarryEulerDeg);
  asset->Grip.HasCarryRightGrip =
      readVector("carry_right_grip", asset->Grip.CarryRightGrip);
  asset->Grip.HasCarryLeftGrip =
      readVector("carry_left_grip", asset->Grip.CarryLeftGrip);
  readVector("fp_aim_euler_deg", asset->Grip.FpAimEulerDeg);
  readVector("tp_aim_euler_deg", asset->Grip.TpAimEulerDeg);
  if (presentation)
  {
    asset->Grip.SupportGripNode = presentation->value("support_grip_node", std::string());
    asset->Grip.TpScale = presentation->value("tp_scale", 1.f);
    asset->Grip.FpScale = presentation->value("fp_scale", 1.f);
  }
  Require(std::isfinite(asset->Grip.TpScale) && asset->Grip.TpScale >= .1f &&
              asset->Grip.TpScale <= 3.f,
          "invalid TP presentation scale");
  Require(std::isfinite(asset->Grip.FpScale) && asset->Grip.FpScale >= .1f &&
              asset->Grip.FpScale <= 3.f,
          "invalid FP presentation scale");
  const auto &materials = g.at("materials");
  asset->Materials.reserve(materials.size());
  for (const auto &m : materials)
  {
    Require(!m.value("doubleSided", false) &&
                m.value("alphaMode", "OPAQUE") == "OPAQUE" &&
                !m.contains("normalTexture") && !m.contains("extensions"),
            "only opaque base-color item materials are supported");
    const auto &pbr = m.at("pbrMetallicRoughness");
    Require(!pbr.contains("metallicRoughnessTexture") &&
                !m.contains("emissiveTexture") &&
                !m.contains("occlusionTexture"),
            "item material maps beyond albedo are not supported yet");
    ItemRenderMaterial material;
    material.Name = m.value("name", "");
    material.Metallic = pbr.value("metallicFactor", 1.f);
    material.Roughness = pbr.value("roughnessFactor", 1.f);
    const auto emissive = ReadFloatArray<3>(m, "emissiveFactor", {},
                                             "emissive factor requires finite RGB");
    for (int i = 0; i < 3; ++i)
    {
      Require(std::isfinite(emissive[size_t(i)]) && emissive[size_t(i)] >= 0 &&
                  emissive[size_t(i)] <= 1,
              "emissive factors must be in 0..1");
      material.Emissive[i] = emissive[size_t(i)];
    }
    const auto color = ReadFloatArray<4>(pbr, "baseColorFactor", {1, 1, 1, 1},
                                         "base color requires finite RGBA");
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
  const auto visit = [&](const auto &self, size_t index, const glm::mat4 &parent,
                         int parentIndex) -> void
  {
    Require(index < visited.size() && !visited[index],
            "cyclic or shared scene node");
    visited[index] = true;
    const auto &node = g.at("nodes").at(index);
    Require(!node.contains("skin"), "skinned item nodes are unsupported");
    const glm::mat4 local = LocalMatrix(node);
    const glm::mat4 matrix = parent * local;
    const size_t nodeIndex = asset->Nodes.size();
    asset->Nodes.push_back(
        {node.value("name", ""), matrix, local, parentIndex});
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
          Require(legacyPreviewNormals && (!extras || extras->value("design_version", 0) != 3),
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
          normals.reserve(positions.size());
          for (const auto &n : sums)
          {
            Require(glm::length(n) > 1e-8f,
                    "legacy preview has degenerate normals");
            const auto unit = glm::normalize(n);
            normals.insert(normals.end(), {unit.x, unit.y, unit.z});
          }
          source.Scratch(positions, indices, sums, normals);
        }
        const auto uv = reader.Read(attrs.at("TEXCOORD_0"), "VEC2");
        source.Scratch(positions, indices, normals, uv);
        Require(normals.size() == positions.size() &&
                    uv.size() / 2 == positions.size() / 3,
                "vertex attribute counts differ");
        ItemRenderPrimitive primitive;
        primitive.Low = glm::vec3(std::numeric_limits<float>::max());
        primitive.High = -primitive.Low;
        primitive.Node = nodeIndex;
        primitive.Material = p.at("material");
        Require(primitive.Material < asset->Materials.size(),
                "invalid material index");
        primitive.Vertices.reserve((positions.size() / 3) * 8);
        primitive.Indices.reserve(indices.size());
        for (size_t i = 0; i < positions.size() / 3; ++i)
        {
          const glm::vec3 point(positions[i * 3], positions[i * 3 + 1],
                                positions[i * 3 + 2]);
          const glm::vec3 normal(normals[i * 3], normals[i * 3 + 1],
                                 normals[i * 3 + 2]);
          Require(std::abs(glm::length(normal) - 1.f) < 1e-3f,
                  "normals must be unit length");
          primitive.Low = glm::min(primitive.Low, point);
          primitive.High = glm::max(primitive.High, point);
          const auto world = glm::vec3(matrix * glm::vec4(point, 1));
          asset->Low = glm::min(asset->Low, world);
          asset->High = glm::max(asset->High, world);
          primitive.Vertices.insert(primitive.Vertices.end(),
                                    {point.x, point.y, point.z,
                                     float(uv[i * 2]), float(uv[i * 2 + 1]),
                                     normal.x, normal.y, normal.z});
        }
        for (double value : indices)
          primitive.Indices.push_back(unsigned(value));
        asset->Primitives.push_back(std::move(primitive));
      }
    if (const auto children = node.find("children"); children != node.end())
      for (const auto &child : *children)
        self(self, child.template get<size_t>(), matrix, int(nodeIndex));
  };
  for (const auto &index :
       g.at("scenes").at(g.value("scene", size_t(0))).at("nodes"))
    visit(visit, index.get<size_t>(), glm::mat4(1), -1);
  Require(!asset->Primitives.empty(), "scene contains no geometry");
  if (!asset->Grip.SupportGripNode.empty())
  {
    const auto found = std::find_if(
        asset->Nodes.begin(), asset->Nodes.end(), [&](const auto &node)
        { return node.Name == asset->Grip.SupportGripNode; });
    Require(found != asset->Nodes.end(),
            "support grip references missing node");
    asset->Grip.SupportGripIndex = size_t(found - asset->Nodes.begin());
  }
  if (extras && extras->contains("visual_bindings"))
    ReadVisualBindings(extras->at("visual_bindings"), *asset);
  if (extras && extras->contains("wear_sections"))
    ReadWearSections(extras->at("wear_sections"), *asset);
  reader.Data = CatalogJsonDocument{};
  decltype(reader.Buffers){}.swap(reader.Buffers);
  const auto visible =
      ItemVisualBounds(*asset, EvaluateItemVisualState(*asset, {}));
  asset->Low = visible.first;
  asset->High = visible.second;
  Require(glm::length(asset->High - asset->Low) > 1e-6f,
          "default item state has no visible geometry");
  for (const auto &section : asset->WearSections)
  {
    if (asset->StaticWearSections.count(section.Node))
      continue;
    ItemVisualState selection;
    selection.VisibleRoots.insert(section.Node);
    ItemStaticWearSection cached;
    const auto bounds = ItemVisualBounds(*asset, selection);
    cached.Low = bounds.first;
    cached.High = bounds.second;
    for (size_t i = 0; i < asset->Primitives.size(); ++i)
      if (ItemNodeVisible(*asset, selection, asset->Primitives[i].Node))
        cached.Primitives.push_back(i);
    asset->StaticWearSections.emplace(section.Node, std::move(cached));
  }
  source.Success();
  return asset;
}
} // namespace

std::shared_ptr<ItemRenderAsset>
LoadItemRenderAsset(const std::filesystem::path &path,
                    bool legacyPreviewNormals)
{
  VisualLoaderSource source(VisualLoaderKind::Item);
  source.Path(path);
  std::ifstream in(path);
  Require(bool(in), "cannot open model");
  auto data = source.ReadDocument(in, CatalogJsonKind::ItemSurface);
  return DecodeItemRenderAsset(path, std::move(data), legacyPreviewNormals, source);
}

std::shared_ptr<const ItemRenderAsset>
FindItemSurfaceAsset(const std::filesystem::path &path, uint64_t revision)
{
  std::lock_guard<std::mutex> lock(AssetMutex);
  if (revision != AssetsRevision)
  {
    Assets.clear();
    AssetAliases.clear();
    AssetsRevision = revision;
  }
  const auto spelling = path.generic_string();
  if (const auto alias = AssetAliases.find(spelling);
      alias != AssetAliases.end())
  {
    ++AssetLookupStats.FastHits;
    return Assets.at(alias->second);
  }
  ++AssetLookupStats.CanonicalResolutions;
  const auto key = std::filesystem::weakly_canonical(path).generic_string();
  const auto found = Assets.find(key);
  if (found != Assets.end())
  {
    AssetAliases.emplace(spelling, key);
    return found->second;
  }
  VisualLoaderSource source(VisualLoaderKind::Item);
  source.Path(path);
  std::ifstream in(path);
  if (!in)
    return nullptr;
  auto data = source.ReadDocument(in, CatalogJsonKind::ItemSurface);
  std::shared_ptr<const ItemRenderAsset> asset;
  const auto *extras = ItemExtras(data);
  if (extras && extras->value("design_version", 0) == 3)
    asset = DecodeItemRenderAsset(path, std::move(data), false, source);
  const auto inserted = Assets.emplace(key, std::move(asset)).first;
  try
  {
    AssetAliases.emplace(spelling, key);
  }
  catch (...)
  {
    Assets.erase(inserted);
    throw;
  }
  return inserted->second;
}

glm::mat4 ApplyItemHandProfile(const glm::mat4 &root, const glm::vec3 &socket,
                               const ItemGripProfile &profile, bool left,
                               float usePhase)
{
  auto offset = profile.FpHandOffset;
  if (!left && profile.HasFpUseHandOffset)
  {
    const float raised = std::clamp(usePhase * 3.f, 0.f, 1.f);
    const float weight = raised * raised * (3.f - 2.f * raised);
    offset = glm::mix(offset, profile.FpUseHandOffset, weight);
  }
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
  AssetAliases.clear();
}

VisualAssetStorage InspectItemSurfaceAssetStorage(VisualAssetCensus *census)
{
  std::lock_guard<std::mutex> lock(AssetMutex);
  VisualAssetStorage out;
  out.inlineBytes = sizeof(AssetMutex) + sizeof(AssetsRevision) + sizeof(Assets) +
                    sizeof(AssetAliases) + sizeof(AssetLookupStats);
  StlSourceStorage table, control, payload;
  table.Hash(Assets);
  table.Hash(AssetAliases);
  for (const auto &[key, asset] : Assets)
  {
    table.Text(key);
    if (asset)
    {
      ++out.live;
      if (census)
        census->Add(asset, VisualAssetOwner::Cache);
      AddVisualAssetControl<ItemRenderAsset>(control);
      MeasureVisualPayload(*asset, payload);
    }
    else
      ++out.negative;
  }
  for (const auto &[spelling, key] : AssetAliases)
  {
    table.Text(spelling);
    table.Text(key);
    table.bytes.supported &= Assets.find(key) != Assets.end();
  }
  out.aliases = AssetAliases.size();
  FinishVisualAssetStorage(out, table, control, payload);
  return out;
}
ItemAssetLookupStats GetItemAssetLookupStats()
{
  std::lock_guard<std::mutex> lock(AssetMutex);
  return AssetLookupStats;
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

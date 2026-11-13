#include "Creatures/Visual/WornEquipmentDrawer.h"

#include "App/Platform/IUPlatformPaths.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonTypes.h"
#include "Creatures/Visual/CreatureDrawQueue.h"
#include "Creatures/Visual/CreatureDrawRequest.h"
#include "Creatures/Visual/CreatureEquipmentFit.h"
#include "Creatures/Visual/Gltf/CreatureGltfLoader.h"
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ItemGltfTextureCache.h"
#include "Items/ItemVisualDefaults.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/GlIncludes.h"

#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{
namespace
{

bool g_HidePossessedWield = false;
const UItemDefinitionStorage *g_ItemDefinitions = nullptr;

constexpr const char *kSlotBones[4][3] = {
    {"hat", "head", nullptr},         // head
    {"body", nullptr, nullptr},       // chest
    {"leftLeg", "rightLeg", nullptr}, // legs
    {"leftLeg", "rightLeg", nullptr}, // feet
};

struct WearLocal
{
  std::vector<std::string> bones;
  glm::vec3 offset{0.f};
  glm::vec3 eulerDeg{0.f};
  float scale{1.f};
  bool boneVolume{false};
  bool hasFitBounds{false};
  glm::vec3 fitLow{0.f}, fitHigh{0.f};
};

WearLocal DefaultWear(size_t slot)
{
  WearLocal w;
  for (int i = 0; i < 3 && kSlotBones[slot][i]; ++i)
  {
    w.bones.emplace_back(kSlotBones[slot][i]);
  }
  if (slot == 3) // feet
  {
    w.offset = glm::vec3(0.f, -0.15f, 0.f);
    w.scale = 0.85f;
  }
  return w;
}

WearLocal LoadWear(const std::string &itemId, size_t slot)
{
  WearLocal w = DefaultWear(slot);
  IUPlatformPaths *paths = IUPlatformPaths::TryGet();
  if (!paths)
  {
    return w;
  }
  const std::string rel = "models/items/" + itemId + "/wear.json";
  std::string text;
  if (!paths->ReadAssetText(rel, text))
  {
    return w;
  }
  try
  {
    const nlohmann::json j = nlohmann::json::parse(text);
    w.boneVolume = j.value("fit_mode", std::string()) == "bone_volume";
    if (j.contains("fit_bounds"))
    {
      const auto &b = j["fit_bounds"];
      if (b.contains("low") && b.contains("high") && b["low"].size() == 3 &&
          b["high"].size() == 3)
      {
        for (size_t axis = 0; axis < 3; ++axis)
        {
          w.fitLow[axis] = b["low"][axis].get<float>();
          w.fitHigh[axis] = b["high"][axis].get<float>();
        }
        w.hasFitBounds =
            glm::all(glm::greaterThan(w.fitHigh, w.fitLow)) &&
            glm::all(glm::lessThan(glm::abs(w.fitLow), glm::vec3(1e6f))) &&
            glm::all(glm::lessThan(glm::abs(w.fitHigh), glm::vec3(1e6f)));
      }
    }
    if (j.contains("bones") && j["bones"].is_array())
    {
      w.bones.clear();
      for (const auto &b : j["bones"])
      {
        w.bones.push_back(b.get<std::string>());
      }
    }
    if (j.contains("offset") && j["offset"].is_array() &&
        j["offset"].size() >= 3)
    {
      w.offset =
          glm::vec3(j["offset"][0].get<float>(), j["offset"][1].get<float>(),
                    j["offset"][2].get<float>());
    }
    if (j.contains("euler_deg") && j["euler_deg"].is_array() &&
        j["euler_deg"].size() >= 3)
    {
      w.eulerDeg = glm::vec3(j["euler_deg"][0].get<float>(),
                             j["euler_deg"][1].get<float>(),
                             j["euler_deg"][2].get<float>());
    }
    w.scale = j.value("scale", w.scale);
  }
  catch (...)
  {
  }
  return w;
}

std::string ResolveWornGltfRel(const std::string &itemId,
                               IUPlatformPaths &paths)
{
  static std::mutex mutex;
  static std::unordered_map<std::string, std::string> cache;
  static uint64_t revision = 0;
  std::lock_guard<std::mutex> lock(mutex);
  SyncItemModelCache(cache, revision);
  const auto found = cache.find(itemId);
  if (found != cache.end())
    return found->second;
  std::string result, text;
  if (paths.ReadAssetText("models/items/" + itemId + "/wear.json", text))
  {
    try
    {
      const auto j = nlohmann::json::parse(text);
      const std::filesystem::path model(j.value("model", std::string()));
      // A worn variant is an explicit sibling asset, never a root/path
      // override.
      if (!model.empty() && model == model.filename() &&
          model.extension() == ".gltf")
      {
        const auto rel =
            (std::filesystem::path("models/items") / itemId / model)
                .generic_string();
        if (paths.AssetExists(rel))
          result = rel;
      }
    }
    catch (...)
    {
    }
  }
  cache[itemId] = result;
  return result;
}

std::string ResolveGltfRel(const std::string &itemId, bool worn = false)
{
  IUPlatformPaths *paths = IUPlatformPaths::TryGet();
  if (!paths || itemId.empty())
  {
    return {};
  }
  if (worn)
  {
    const auto rel = ResolveWornGltfRel(itemId, *paths);
    if (!rel.empty())
      return rel;
  }
  auto endsWithIgnoreCase = [](const std::string &s, const char *suf)
  {
    const size_t n = std::char_traits<char>::length(suf);
    if (s.size() < n)
    {
      return false;
    }
    for (size_t i = 0; i < n; ++i)
    {
      const char a = static_cast<char>(
          std::tolower(static_cast<unsigned char>(s[s.size() - n + i])));
      const char b =
          static_cast<char>(std::tolower(static_cast<unsigned char>(suf[i])));
      if (a != b)
      {
        return false;
      }
    }
    return true;
  };
  if (g_ItemDefinitions)
  {
    if (const ItemDefinition *def = g_ItemDefinitions->Get(itemId))
    {
      if (!def->ModelPath.empty() &&
          (endsWithIgnoreCase(def->ModelPath, ".gltf") ||
           endsWithIgnoreCase(def->ModelPath, ".glb")) &&
          paths->AssetExists(def->ModelPath))
      {
        return def->ModelPath;
      }
    }
  }
  const std::string sibling =
      (std::filesystem::path("models/items") / itemId / "model.gltf")
          .generic_string();
  if (paths->AssetExists(sibling))
  {
    return sibling;
  }
  return {};
}

std::filesystem::path ResolveGltfAbs(const std::string &itemId,
                                     bool worn = false)
{
  IUPlatformPaths *paths = IUPlatformPaths::TryGet();
  const std::string rel = ResolveGltfRel(itemId, worn);
  if (!paths || rel.empty())
  {
    return {};
  }
  return paths->AssetRoot() / rel;
}

BoneSkeletonCubeMeshCpu BuildBoxMesh(const glm::vec3 &center,
                                     const glm::vec3 &size)
{
  BoneSkeletonCubeMeshCpu mesh;
  const float hx = std::max(0.01f, size.x) * 0.5f;
  const float hy = std::max(0.01f, size.y) * 0.5f;
  const float hz = std::max(0.01f, size.z) * 0.5f;
  const glm::vec3 c = center;
  const float corners[8][3] = {
      {c.x - hx, c.y - hy, c.z - hz}, {c.x + hx, c.y - hy, c.z - hz},
      {c.x + hx, c.y + hy, c.z - hz}, {c.x - hx, c.y + hy, c.z - hz},
      {c.x - hx, c.y - hy, c.z + hz}, {c.x + hx, c.y - hy, c.z + hz},
      {c.x + hx, c.y + hy, c.z + hz}, {c.x - hx, c.y + hy, c.z + hz},
  };
  const int faces[6][4] = {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 5, 1, 0},
                           {3, 2, 6, 7}, {4, 0, 3, 7}, {1, 5, 6, 2}};
  const float uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  mesh.interleavedPosUv.reserve(6 * 4 * 5);
  mesh.indices.reserve(6 * 6);
  unsigned base = 0;
  for (const auto &face : faces)
  {
    for (int i = 0; i < 4; ++i)
    {
      const auto &p = corners[face[i]];
      mesh.interleavedPosUv.push_back(p[0]);
      mesh.interleavedPosUv.push_back(p[1]);
      mesh.interleavedPosUv.push_back(p[2]);
      mesh.interleavedPosUv.push_back(uvs[i][0]);
      mesh.interleavedPosUv.push_back(uvs[i][1]);
    }
    mesh.indices.push_back(base);
    mesh.indices.push_back(base + 1);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 3);
    base += 4;
  }
  return mesh;
}

std::vector<BoneSkeletonCubeMeshCpu> LoadPartsMeshes(const std::string &itemId)
{
  std::vector<BoneSkeletonCubeMeshCpu> out;
  IUPlatformPaths *paths = IUPlatformPaths::TryGet();
  if (!paths || itemId.empty())
  {
    return out;
  }
  std::string rel;
  if (g_ItemDefinitions)
  {
    if (const ItemDefinition *def = g_ItemDefinitions->Get(itemId))
    {
      if (!def->ModelPath.empty() && def->ModelPath.size() > 5 &&
          def->ModelPath.substr(def->ModelPath.size() - 5) == ".json" &&
          paths->AssetExists(def->ModelPath))
      {
        rel = def->ModelPath;
      }
    }
  }
  if (rel.empty())
  {
    const std::string sibling =
        (std::filesystem::path("models/items") / (itemId + ".json"))
            .generic_string();
    if (paths->AssetExists(sibling))
    {
      rel = sibling;
    }
  }
  if (rel.empty())
  {
    return out;
  }
  std::string text;
  if (!paths->ReadAssetText(rel, text))
  {
    return out;
  }
  try
  {
    const nlohmann::json data = nlohmann::json::parse(text);
    if (!data.contains("parts") || !data["parts"].is_array())
    {
      return out;
    }
    for (const auto &part : data["parts"])
    {
      glm::vec3 off(0.f);
      glm::vec3 sz(0.2f);
      if (part.contains("offset") && part["offset"].is_array() &&
          part["offset"].size() >= 3)
      {
        off = glm::vec3(part["offset"][0].get<float>(),
                        part["offset"][1].get<float>(),
                        part["offset"][2].get<float>());
      }
      if (part.contains("size") && part["size"].is_array() &&
          part["size"].size() >= 3)
      {
        sz = glm::vec3(part["size"][0].get<float>(),
                       part["size"][1].get<float>(),
                       part["size"][2].get<float>());
      }
      out.push_back(BuildBoxMesh(off, sz));
    }
  }
  catch (...)
  {
    out.clear();
  }
  return out;
}

void DrawMeshImmediateOrQueue(CreatureDrawQueue *queue, UShaderProgram *shader,
                              const glm::mat4 &mvp, GLuint tex,
                              const BoneSkeletonCubeMeshCpu &mesh,
                              bool immediate)
{
  if (mesh.interleavedPosUv.empty() || mesh.indices.empty())
  {
    return;
  }
  if (immediate && shader)
  {
    static GLuint vao = 0, vbo = 0, ebo = 0;
    if (vao == 0)
    {
      glGenVertexArrays(1, &vao);
      glGenBuffers(1, &vbo);
      glGenBuffers(1, &ebo);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    shader->Use();
    shader->SetInt("texture0", 0);
    shader->SetMat4("mvp_matrix", mvp);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(mesh.interleavedPosUv.size() * sizeof(float)),
        mesh.interleavedPosUv.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(unsigned)),
        mesh.indices.data(), GL_STREAM_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          reinterpret_cast<void *>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()),
                   GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    return;
  }
  if (queue)
  {
    CreatureDrawRequest req;
    req.Kind = CreatureDrawKind::SkeletalMesh;
    req.Mvp = mvp;
    req.Texture = tex;
    req.SkeletalMesh = &mesh;
    queue->Push(std::move(req));
  }
}

std::shared_ptr<CreatureGltfMeshAsset> LoadItemGltf(const std::string &itemId,
                                                    bool worn = false)
{
  static std::mutex mu;
  static std::unordered_map<std::string, std::shared_ptr<CreatureGltfMeshAsset>>
      cache;
  static uint64_t cacheRevision = 0;
  std::lock_guard<std::mutex> lock(mu);
  SyncItemModelCache(cache, cacheRevision);
  const std::string key = itemId + (worn ? "|worn" : "|held");
  const auto it = cache.find(key);
  if (it != cache.end())
  {
    return it->second;
  }
  const std::string rel = ResolveGltfRel(itemId, worn);
  if (rel.empty())
  {
    return nullptr;
  }
  IUPlatformPaths *paths = IUPlatformPaths::TryGet();
  if (!paths)
  {
    return nullptr;
  }
  const auto abs = (paths->AssetRoot() / rel).string();
  auto asset = CreatureGltfLoader::LoadFromFile(abs);
  if (asset)
  {
    cache[key] = asset;
  }
  return asset;
}

GLuint SolidColorTex(const std::string &itemId)
{
  static std::mutex mu;
  static std::unordered_map<std::string, GLuint> cache;
  std::lock_guard<std::mutex> lock(mu);
  const auto it = cache.find(itemId);
  if (it != cache.end())
  {
    return it->second;
  }
  unsigned char r = 160, g = 160, b = 170;
  if (itemId.find("leather") != std::string::npos)
  {
    r = 120;
    g = 78;
    b = 42;
  }
  else if (itemId.find("iron") != std::string::npos)
  {
    r = 170;
    g = 175;
    b = 185;
  }
  else if (itemId.find("copper") != std::string::npos)
  {
    r = 184;
    g = 115;
    b = 51;
  }
  const unsigned char rgba[4] = {r, g, b, 255};
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               rgba);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  cache[itemId] = tex;
  return tex;
}

glm::mat4 WearLocalMatrix(const WearLocal &w)
{
  glm::mat4 m = glm::translate(glm::mat4(1.f), w.offset);
  m = glm::rotate(m, glm::radians(w.eulerDeg.x), glm::vec3(1, 0, 0));
  m = glm::rotate(m, glm::radians(w.eulerDeg.y), glm::vec3(0, 1, 0));
  m = glm::rotate(m, glm::radians(w.eulerDeg.z), glm::vec3(0, 0, 1));
  m = glm::scale(m, glm::vec3(w.scale));
  return m;
}

glm::mat4 FitGltfToBone(const CreatureGltfMeshAsset &asset, float targetSize)
{
  glm::vec3 minV(1e9f), maxV(-1e9f);
  for (const GltfPrimitiveCpu &prim : asset.primitives)
  {
    const auto &posUv = prim.mesh.interleavedPosUv;
    for (size_t i = 0; i + 4 < posUv.size(); i += 5)
    {
      const glm::vec3 p(posUv[i], posUv[i + 1], posUv[i + 2]);
      minV = glm::min(minV, p);
      maxV = glm::max(maxV, p);
    }
  }
  if (minV.x > maxV.x)
  {
    return glm::mat4(1.f);
  }
  const glm::vec3 center = (minV + maxV) * 0.5f;
  const glm::vec3 ext = maxV - minV;
  const float maxExtent =
      std::max(1e-5f, std::max(ext.x, std::max(ext.y, ext.z)));
  const float scale = targetSize / maxExtent;
  return glm::scale(glm::mat4(1.f), glm::vec3(scale)) *
         glm::translate(glm::mat4(1.f), -center);
}

struct WornItemLocal
{
  WearLocal wear;
  glm::mat4 fit{1.f};
  glm::vec3 itemLow{0.f}, itemHigh{0.f};
};

// wear.json parsing and the mesh-extent fit walk are per-item constants:
// build them once per (item, slot), drop on item definition reloads like the
// other item model caches (SyncItemModelCache).
const WornItemLocal &CachedWornItemLocal(const std::string &itemId, size_t slot)
{
  static std::mutex mu;
  static std::unordered_map<std::string, std::shared_ptr<WornItemLocal>> cache;
  static uint64_t cacheRevision = 0;
  std::lock_guard<std::mutex> lock(mu);
  SyncItemModelCache(cache, cacheRevision);
  const std::string key = itemId + "|" + std::to_string(slot);
  const auto it = cache.find(key);
  if (it != cache.end())
  {
    return *it->second;
  }
  auto entry = std::make_shared<WornItemLocal>();
  entry->wear = LoadWear(itemId, slot);
  if (const auto asset = LoadItemGltf(itemId, true))
  {
    entry->fit = FitGltfToBone(*asset, slot == 0 ? 0.55f : 0.7f);
    entry->itemLow = glm::vec3(1e9f);
    entry->itemHigh = glm::vec3(-1e9f);
    for (const auto &primitive : asset->primitives)
      for (const auto index : primitive.mesh.indices)
      {
        const size_t i = static_cast<size_t>(index) * 5;
        const auto &v = primitive.mesh.interleavedPosUv;
        const glm::vec3 p(v[i], v[i + 1], v[i + 2]);
        entry->itemLow = glm::min(entry->itemLow, p);
        entry->itemHigh = glm::max(entry->itemHigh, p);
      }
  }
  if (entry->wear.hasFitBounds)
  {
    entry->itemLow = entry->wear.fitLow;
    entry->itemHigh = entry->wear.fitHigh;
  }
  cache[key] = entry;
  return *entry;
}

void SubmitItemOnBones(CreatureDrawQueue *queue, UShaderProgram *shader,
                       const std::string &itemId, size_t slot,
                       const glm::mat4 &viewProj, const glm::mat4 &bodyMat,
                       const BoneMatrixLookupFn &bones, bool immediate,
                       const BoneBoundsLookupFn &bounds)
{
  if (itemId.empty())
  {
    return;
  }
  auto asset = LoadItemGltf(itemId, true);
  if (!asset || asset->primitives.empty())
  {
    return;
  }
  const WornItemLocal &worn = CachedWornItemLocal(itemId, slot);
  const std::filesystem::path gltfAbs = ResolveGltfAbs(itemId, true);
  const GLuint fallbackTex = SolidColorTex(itemId);

  auto wearBones = worn.wear.bones;
  bool articulatedLegs = false;
  if (slot == 2)
    for (const auto &name : worn.wear.bones)
    {
      const std::string shin = name == "rightLeg"  ? "rightShin"
                               : name == "leftLeg" ? "leftShin"
                                                   : "";
      glm::mat4 probe(1.f);
      if (!shin.empty() && bones(shin, probe))
      {
        wearBones.push_back(shin);
        articulatedLegs = true;
      }
    }
  for (const std::string &originalBone : wearBones)
  {
    std::string boneName = originalBone;
    glm::mat4 boneMat(1.f);
    if (slot == 3 && (boneName == "rightLeg" || boneName == "leftLeg"))
    {
      const std::string foot =
          boneName == "rightLeg" ? "rightFoot" : "leftFoot";
      if (bones(foot, boneMat))
        boneName = foot;
    }
    if (!bones(boneName, boneMat))
    {
      continue;
    }
    glm::mat4 fit = worn.fit;
    glm::vec3 low, high;
    if (worn.wear.boneVolume && bounds && bounds(boneName, low, high))
    {
      auto region = CreatureWearRegion(slot, low, high);
      if (articulatedLegs ||
          (slot == 3 && boneName.find("Foot") != std::string::npos))
      {
        const float pad = articulatedLegs ? .03f : .026f;
        region = {low - glm::vec3(pad, 0.f, pad),
                  high + glm::vec3(pad, 0.f, pad)};
      }
      fit = FitCreatureWearToRegion(worn.itemLow, worn.itemHigh, region);
    }
    const glm::mat4 local = WearLocalMatrix(worn.wear) * fit;
    const glm::mat4 mvp = viewProj * bodyMat * boneMat * local;
    for (const GltfPrimitiveCpu &prim : asset->primitives)
    {
      if (prim.mesh.interleavedPosUv.empty() || prim.mesh.indices.empty())
      {
        continue;
      }
      GLuint tex = 0;
      if (!gltfAbs.empty())
      {
        tex = ItemGltfTextureCache::Instance().Get(gltfAbs, prim.textureStem);
      }
      if (tex == 0)
      {
        tex = fallbackTex;
      }
      if (immediate && shader)
      {
        static GLuint vao = 0, vbo = 0, ebo = 0;
        if (vao == 0)
        {
          glGenVertexArrays(1, &vao);
          glGenBuffers(1, &vbo);
          glGenBuffers(1, &ebo);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        shader->Use();
        shader->SetInt("texture0", 0);
        shader->SetMat4("mvp_matrix", mvp);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(prim.mesh.interleavedPosUv.size() *
                                             sizeof(float)),
                     prim.mesh.interleavedPosUv.data(), GL_STREAM_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(prim.mesh.indices.size() *
                                             sizeof(unsigned)),
                     prim.mesh.indices.data(), GL_STREAM_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                              nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                              reinterpret_cast<void *>(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glDrawElements(GL_TRIANGLES,
                       static_cast<GLsizei>(prim.mesh.indices.size()),
                       GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
      }
      else if (queue)
      {
        CreatureDrawRequest req;
        req.Kind = CreatureDrawKind::SkeletalMesh;
        req.Mvp = mvp;
        req.Texture = tex;
        req.SkeletalMesh = &prim.mesh;
        queue->Push(std::move(req));
      }
    }
  }
}

struct PartsMeshesFit
{
  std::vector<BoneSkeletonCubeMeshCpu> meshes;
  glm::mat4 fit{1.f};
};

const PartsMeshesFit &CachedPartsMeshes(const std::string &itemId)
{
  static std::mutex mu;
  static std::unordered_map<std::string, std::unique_ptr<PartsMeshesFit>> cache;
  static uint64_t cacheRevision = 0;
  std::lock_guard<std::mutex> lock(mu);
  // Parts follow the item's model_path: a resource-pack apply reloads the
  // definitions, so the cache must not keep the old pack's meshes.
  SyncItemModelCache(cache, cacheRevision);
  std::unique_ptr<PartsMeshesFit> &slot = cache[itemId];
  if (slot)
  {
    return *slot;
  }
  slot = std::make_unique<PartsMeshesFit>();
  slot->meshes = LoadPartsMeshes(itemId);
  if (slot->meshes.empty())
  {
    slot->meshes.push_back(BuildBoxMesh(glm::vec3(0.f, 0.18f, 0.f),
                                        glm::vec3(0.07f, 0.38f, 0.07f)));
  }
  glm::vec3 minV(1e9f), maxV(-1e9f);
  for (const auto &mesh : slot->meshes)
  {
    for (size_t i = 0; i + 4 < mesh.interleavedPosUv.size(); i += 5)
    {
      const glm::vec3 p(mesh.interleavedPosUv[i], mesh.interleavedPosUv[i + 1],
                        mesh.interleavedPosUv[i + 2]);
      minV = glm::min(minV, p);
      maxV = glm::max(maxV, p);
    }
  }
  if (minV.x <= maxV.x)
  {
    const glm::vec3 center = (minV + maxV) * 0.5f;
    const glm::vec3 ext = maxV - minV;
    const float maxExtent =
        std::max(1e-5f, std::max(ext.x, std::max(ext.y, ext.z)));
    const float scale = 0.7f / maxExtent;
    slot->fit = glm::scale(glm::mat4(1.f), glm::vec3(scale)) *
                glm::translate(glm::mat4(1.f), -center);
  }
  return *slot;
}

struct WieldGripLocal
{
  bool enabled{false};
  glm::mat4 rotation{1.f}, fit{1.f};
};

const WieldGripLocal &CachedWieldGrip(const std::string &itemId,
                                      const CreatureGltfMeshAsset &asset)
{
  static std::mutex mutex;
  static std::unordered_map<std::string, std::shared_ptr<WieldGripLocal>> cache;
  static uint64_t revision = 0;
  std::lock_guard<std::mutex> lock(mutex);
  SyncItemModelCache(cache, revision);
  if (const auto found = cache.find(itemId); found != cache.end())
    return *found->second;
  auto entry = std::make_shared<WieldGripLocal>();
  std::string text;
  auto *paths = IUPlatformPaths::TryGet();
  if (paths &&
      paths->ReadAssetText("models/items/" + itemId + "/wield_grip.json", text))
  {
    try
    {
      const auto json = nlohmann::json::parse(text);
      if (json.value("fit", std::string()) == "authored_origin")
      {
        const auto origin = json.at("origin");
        const auto euler = json.at("euler_deg");
        const glm::vec3 gripOrigin(origin.at(0).get<float>(),
                                   origin.at(1).get<float>(),
                                   origin.at(2).get<float>());
        WearLocal orientation;
        orientation.eulerDeg =
            glm::vec3(euler.at(0).get<float>(), euler.at(1).get<float>(),
                      euler.at(2).get<float>());
        entry->rotation = WearLocalMatrix(orientation);
        glm::vec3 low(1e9f), high(-1e9f);
        for (const auto &primitive : asset.primitives)
          for (size_t i = 0; i + 4 < primitive.mesh.interleavedPosUv.size();
               i += 5)
          {
            const auto &v = primitive.mesh.interleavedPosUv;
            const glm::vec3 p(v[i], v[i + 1], v[i + 2]);
            low = glm::min(low, p);
            high = glm::max(high, p);
          }
        const glm::vec3 size = high - low;
        const float span = std::max({size.x, size.y, size.z});
        if (span > 1e-5f)
        {
          entry->fit = glm::scale(glm::mat4(1.f), glm::vec3(.7f / span)) *
                       glm::translate(glm::mat4(1.f), -gripOrigin);
          entry->enabled = true;
        }
      }
    }
    catch (const std::exception &error)
    {
      std::cerr << "Invalid wield grip " << itemId << ": " << error.what()
                << '\n';
    }
  }
  cache[itemId] = entry;
  return *entry;
}

void SubmitItemOnSingleBone(CreatureDrawQueue *queue, UShaderProgram *shader,
                            const std::string &itemId,
                            const std::string &boneName, float extraScale,
                            const float offset[3], const float euler[3],
                            const glm::mat4 &viewProj, const glm::mat4 &bodyMat,
                            const BoneMatrixLookupFn &bones, bool immediate,
                            const BoneBoundsLookupFn &bounds)
{
  if (itemId.empty() || boneName.empty())
  {
    return;
  }
  WearLocal wear;
  wear.bones = {boneName};
  wear.offset = glm::vec3(offset[0], offset[1], offset[2]);
  wear.eulerDeg = glm::vec3(euler[0], euler[1], euler[2]);
  wear.scale = extraScale;
  glm::mat4 wearLocal = WearLocalMatrix(wear);
  const GLuint fallbackTex = SolidColorTex(itemId);

  glm::mat4 boneMat(1.f);
  if (!bones(boneName, boneMat))
  {
    return;
  }

  auto asset = LoadItemGltf(itemId);
  if (asset && !asset->primitives.empty())
  {
    glm::mat4 local;
    glm::vec3 low, high;
    const auto &grip = CachedWieldGrip(itemId, *asset);
    if (grip.enabled && bounds && bounds(boneName, low, high))
      local =
          glm::translate(glm::mat4(1.f), CreaturePalmGripPosition(low, high)) *
          wearLocal * grip.rotation * grip.fit;
    else
    {
      wear.offset.y += .05f;
      local = WearLocalMatrix(wear) * FitGltfToBone(*asset, .7f);
    }
    const std::filesystem::path gltfAbs = ResolveGltfAbs(itemId);
    const glm::mat4 mvp = viewProj * bodyMat * boneMat * local;
    for (const GltfPrimitiveCpu &prim : asset->primitives)
    {
      if (prim.mesh.interleavedPosUv.empty() || prim.mesh.indices.empty())
      {
        continue;
      }
      GLuint tex = 0;
      if (!gltfAbs.empty())
      {
        tex = ItemGltfTextureCache::Instance().Get(gltfAbs, prim.textureStem);
      }
      if (tex == 0)
      {
        tex = fallbackTex;
      }
      DrawMeshImmediateOrQueue(queue, shader, mvp, tex, prim.mesh, immediate);
    }
    return;
  }

  // parts_v1 / procedural rod when glTF missing. Built once per item and
  // never replaced: queued draws keep pointers into these meshes until the
  // frame's flush, and rebuilding per call (with a JSON read from disk every
  // frame) left the queue of an earlier wielder of the same item dangling.
  const PartsMeshesFit &partsFit = CachedPartsMeshes(itemId);
  wear.offset.y += .05f;
  wearLocal = WearLocalMatrix(wear);
  const glm::mat4 mvp = viewProj * bodyMat * boneMat * wearLocal * partsFit.fit;
  for (const auto &mesh : partsFit.meshes)
  {
    DrawMeshImmediateOrQueue(queue, shader, mvp, fallbackTex, mesh, immediate);
  }
}

void ResolveWieldParams(const UItemDefinitionStorage *items,
                        const std::string &itemId, float &scale,
                        float offset[3], float euler[3])
{
  scale = 1.25f;
  offset[0] = offset[1] = offset[2] = 0.f;
  euler[0] = euler[1] = euler[2] = 0.f;
  const UItemDefinitionStorage *src = items ? items : g_ItemDefinitions;
  if (!src || itemId.empty())
  {
    return;
  }
  if (const ItemDefinition *def = src->Get(itemId))
  {
    scale = DefaultWieldScale(*def) * def->Visual.TpScale;
    std::string fitAxis;
    ResolveWieldPose(*def, offset, euler, fitAxis);
    (void)fitAxis;
  }
}

} // namespace

void WornEquipmentDrawer::SubmitLoose(CreatureDrawQueue &queue,
                                      const std::string &id,
                                      const glm::mat4 &vp,
                                      const glm::mat4 &model, glm::vec3 size,
                                      unsigned int blockTexture)
{
  if (auto asset = LoadItemGltf(id))
  {
    const auto path = ResolveGltfAbs(id);
    for (const auto &prim : asset->primitives)
    {
      auto tex = ItemGltfTextureCache::Instance().Get(path, prim.textureStem);
      if (!tex)
        tex = SolidColorTex(id);
      DrawMeshImmediateOrQueue(
          &queue, nullptr,
          vp * model *
              FitGltfToBone(*asset, std::max({size.x, size.y, size.z})),
          tex, prim.mesh, false);
    }
  }
  else
  {
    static const auto cube = []
    {
      auto mesh = BuildBoxMesh(glm::vec3(0), glm::vec3(1));
      const int slots[] = {2, 0, 5, 4, 3, 1};
      for (size_t vertex = 0; vertex < 24; ++vertex)
      {
        auto &u = mesh.interleavedPosUv[vertex * 5 + 3];
        auto &v = mesh.interleavedPosUv[vertex * 5 + 4];
        u = (slots[vertex / 4] + u) / 6.f;
        v = 1 - v;
      }
      return mesh;
    }();
    DrawMeshImmediateOrQueue(
        &queue, nullptr, vp * model * glm::scale(glm::mat4(1), size),
        blockTexture ? blockTexture : SolidColorTex(id), cube, false);
  }
}
void WornEquipmentDrawer::SetHidePossessedWield(bool hide)
{
  g_HidePossessedWield = hide;
}

bool WornEquipmentDrawer::HidePossessedWield() { return g_HidePossessedWield; }

void WornEquipmentDrawer::SetItemDefinitions(
    const UItemDefinitionStorage *items)
{
  g_ItemDefinitions = items;
}

const UItemDefinitionStorage *WornEquipmentDrawer::ItemDefinitions()
{
  return g_ItemDefinitions;
}

void WornEquipmentDrawer::SubmitFromCreature(CreatureDrawQueue &queue,
                                             const UCreature &creature,
                                             const glm::mat4 &viewProj,
                                             const glm::mat4 &bodyMat,
                                             const BoneMatrixLookupFn &bones,
                                             const BoneBoundsLookupFn &bounds)
{
  std::array<WornArmorPreviewSlot, UCreatureInventory::kArmorSlotCount> slots{};
  for (size_t i = 0; i < UCreatureInventory::kArmorSlotCount; ++i)
  {
    const InventoryEntryRef &e = creature.GetInventory().GetEquippedArmor(i);
    if (!e.empty && !e.broken && e.kind == InventoryEntryKind::Item)
    {
      slots[i].ItemId = e.Id;
    }
  }
  for (size_t i = 0; i < slots.size(); ++i)
  {
    SubmitItemOnBones(&queue, nullptr, slots[i].ItemId, i, viewProj, bodyMat,
                      bones, false, bounds);
  }
}

void WornEquipmentDrawer::SubmitWieldedFromCreature(
    CreatureDrawQueue &queue, const UCreature &creature,
    const UItemDefinitionStorage *items, const glm::mat4 &viewProj,
    const glm::mat4 &bodyMat, const BoneMatrixLookupFn &bones,
    const BoneBoundsLookupFn &bounds)
{
  if (creature.CarryHandCount > 0 ||
      (g_HidePossessedWield && creature.IsPossessed()))
  {
    return;
  }
  const InventoryEntryRef *active = creature.GetInventory().GetActiveEntryRef();
  const InventoryEntryRef &off = creature.GetInventory().GetEquippedOffhand();
  std::string mainId;
  std::string offId;
  if (active && !active->empty && !active->broken &&
      active->kind == InventoryEntryKind::Item)
  {
    mainId = active->Id;
  }
  if (!off.empty && !off.broken && off.kind == InventoryEntryKind::Item)
  {
    offId = off.Id;
  }
  SubmitWieldedPreview(&queue, nullptr, mainId, offId, items, viewProj, bodyMat,
                       bones, false, bounds);
}

void WornEquipmentDrawer::SubmitWieldedPreview(
    CreatureDrawQueue *queue, UShaderProgram *shader,
    const std::string &mainItemId, const std::string &offhandItemId,
    const UItemDefinitionStorage *items, const glm::mat4 &viewProj,
    const glm::mat4 &bodyMat, const BoneMatrixLookupFn &bones, bool immediate,
    const BoneBoundsLookupFn &bounds)
{
  float scale = 1.25f;
  float offset[3] = {0, 0, 0};
  float euler[3] = {0, 0, 0};
  if (!mainItemId.empty())
  {
    ResolveWieldParams(items, mainItemId, scale, offset, euler);
    SubmitItemOnSingleBone(queue, shader, mainItemId, "rightItem", scale,
                           offset, euler, viewProj, bodyMat, bones, immediate,
                           bounds);
  }
  if (!offhandItemId.empty())
  {
    ResolveWieldParams(items, offhandItemId, scale, offset, euler);
    SubmitItemOnSingleBone(queue, shader, offhandItemId, "leftItem", scale,
                           offset, euler, viewProj, bodyMat, bones, immediate,
                           bounds);
  }
}

void WornEquipmentDrawer::DrawImmediate(
    const std::array<WornArmorPreviewSlot, kWornArmorSlotCount> &slots,
    const glm::mat4 &viewProj, const glm::mat4 &bodyMat,
    const BoneMatrixLookupFn &bones, UShaderProgram *shader,
    const BoneBoundsLookupFn &bounds)
{
  for (size_t i = 0; i < slots.size(); ++i)
  {
    SubmitItemOnBones(nullptr, shader, slots[i].ItemId, i, viewProj, bodyMat,
                      bones, true, bounds);
  }
}

} // namespace cutum

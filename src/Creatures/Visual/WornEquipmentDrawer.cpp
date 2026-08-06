#include "Creatures/Visual/WornEquipmentDrawer.h"

#include "App/Platform/IUPlatformPaths.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonTypes.h"
#include "Creatures/Visual/CreatureDrawRequest.h"
#include "Creatures/Visual/CreatureDrawQueue.h"
#include "Creatures/Visual/Gltf/CreatureGltfLoader.h"
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ItemGltfTextureCache.h"
#include "Items/ItemVisualDefaults.h"
#include "Render/Engine/GeometryEngine.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/GlIncludes.h"

#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
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

constexpr const char *kSlotBones[6][3] = {
    {"hat", "head", nullptr},          // head
    {"body", nullptr, nullptr},        // chest
    {"leftArm", "rightArm", nullptr},  // arms
    {"leftItem", "rightItem", nullptr},// hands
    {"leftLeg", "rightLeg", nullptr},  // legs
    {"leftLeg", "rightLeg", nullptr},  // feet
};

struct WearLocal
{
  std::vector<std::string> bones;
  glm::vec3 offset{0.f};
  glm::vec3 eulerDeg{0.f};
  float scale{1.f};
};

WearLocal DefaultWear(size_t slot)
{
  WearLocal w;
  for (int i = 0; i < 3 && kSlotBones[slot][i]; ++i)
  {
    w.bones.emplace_back(kSlotBones[slot][i]);
  }
  if (slot == 5)
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
    if (j.contains("bones") && j["bones"].is_array())
    {
      w.bones.clear();
      for (const auto &b : j["bones"])
      {
        w.bones.push_back(b.get<std::string>());
      }
    }
    if (j.contains("offset") && j["offset"].is_array() && j["offset"].size() >= 3)
    {
      w.offset = glm::vec3(j["offset"][0].get<float>(), j["offset"][1].get<float>(),
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

std::string ResolveGltfRel(const std::string &itemId)
{
  IUPlatformPaths *paths = IUPlatformPaths::TryGet();
  if (!paths || itemId.empty())
  {
    return {};
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

std::filesystem::path ResolveGltfAbs(const std::string &itemId)
{
  IUPlatformPaths *paths = IUPlatformPaths::TryGet();
  const std::string rel = ResolveGltfRel(itemId);
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
      if (!def->ModelPath.empty() &&
          def->ModelPath.size() > 5 &&
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

void DrawMeshImmediateOrQueue(UGeometryEngine *engine, UShaderProgram *shader,
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
    shader->SetInt("uAnimFrame", 0);
    shader->SetInt("uAnimFrameCount", 1);
    shader->SetMat4("mvp_matrix", mvp);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh.interleavedPosUv.size() *
                                         sizeof(float)),
                 mesh.interleavedPosUv.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
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
  if (engine)
  {
    CreatureDrawRequest req;
    req.Kind = CreatureDrawKind::SkeletalMesh;
    req.Mvp = mvp;
    req.Texture = tex;
    req.SkeletalMesh = &mesh;
    engine->GetCreatureDrawQueue().Push(std::move(req));
  }
}

std::shared_ptr<CreatureGltfMeshAsset> LoadItemGltf(const std::string &itemId)
{
  static std::mutex mu;
  static std::unordered_map<std::string, std::shared_ptr<CreatureGltfMeshAsset>>
      cache;
  std::lock_guard<std::mutex> lock(mu);
  const auto it = cache.find(itemId);
  if (it != cache.end())
  {
    return it->second;
  }
  const std::string rel = ResolveGltfRel(itemId);
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
    cache[itemId] = asset;
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

void PushOrDraw(UGeometryEngine *engine, UShaderProgram *shader,
                const glm::mat4 &mvp, GLuint tex,
                const BoneSkeletonCubeMeshCpu &mesh)
{
  if (engine)
  {
    CreatureDrawRequest req;
    req.Kind = CreatureDrawKind::SkeletalMesh;
    req.Mvp = mvp;
    req.Texture = tex;
    req.SkeletalMesh = &mesh;
    engine->GetCreatureDrawQueue().Push(std::move(req));
    return;
  }
  if (!shader)
  {
    return;
  }
  // Immediate path for preview — upload stream VAO once per call site via
  // GeometryEngine helpers aren't available; skip if no engine (preview uses
  // its own draw below).
  (void)mvp;
  (void)tex;
  (void)mesh;
}

void SubmitItemOnBones(UGeometryEngine *engine, UShaderProgram *shader,
                       const std::string &itemId, size_t slot,
                       const glm::mat4 &viewProj, const glm::mat4 &bodyMat,
                       const BoneMatrixLookupFn &bones, bool immediate)
{
  if (itemId.empty())
  {
    return;
  }
  auto asset = LoadItemGltf(itemId);
  if (!asset || asset->primitives.empty())
  {
    return;
  }
  const WearLocal wear = LoadWear(itemId, slot);
  const glm::mat4 fit = FitGltfToBone(*asset, slot == 0 ? 0.55f : 0.7f);
  const glm::mat4 local = WearLocalMatrix(wear) * fit;
  const std::filesystem::path gltfAbs = ResolveGltfAbs(itemId);
  const GLuint fallbackTex = SolidColorTex(itemId);

  for (const std::string &boneName : wear.bones)
  {
    glm::mat4 boneMat(1.f);
    if (!bones(boneName, boneMat))
    {
      continue;
    }
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
        shader->SetInt("uAnimFrame", 0);
        shader->SetInt("uAnimFrameCount", 1);
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
      else if (engine)
      {
        CreatureDrawRequest req;
        req.Kind = CreatureDrawKind::SkeletalMesh;
        req.Mvp = mvp;
        req.Texture = tex;
        req.SkeletalMesh = &prim.mesh;
        engine->GetCreatureDrawQueue().Push(std::move(req));
      }
    }
  }
}

void SubmitItemOnSingleBone(UGeometryEngine *engine, UShaderProgram *shader,
                            const std::string &itemId,
                            const std::string &boneName, float extraScale,
                            const float offset[3], const float euler[3],
                            const glm::mat4 &viewProj, const glm::mat4 &bodyMat,
                            const BoneMatrixLookupFn &bones, bool immediate)
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
  const glm::mat4 wearLocal = WearLocalMatrix(wear);
  const GLuint fallbackTex = SolidColorTex(itemId);

  glm::mat4 boneMat(1.f);
  if (!bones(boneName, boneMat))
  {
    return;
  }

  auto asset = LoadItemGltf(itemId);
  if (asset && !asset->primitives.empty())
  {
    const glm::mat4 fit = FitGltfToBone(*asset, 0.7f);
    const glm::mat4 local = wearLocal * fit;
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
      DrawMeshImmediateOrQueue(engine, shader, mvp, tex, prim.mesh, immediate);
    }
    return;
  }

  // parts_v1 / procedural rod when glTF missing.
  std::vector<BoneSkeletonCubeMeshCpu> parts = LoadPartsMeshes(itemId);
  if (parts.empty())
  {
    parts.push_back(BuildBoxMesh(glm::vec3(0.f, 0.18f, 0.f),
                                 glm::vec3(0.07f, 0.38f, 0.07f)));
  }
  glm::vec3 minV(1e9f), maxV(-1e9f);
  for (const auto &mesh : parts)
  {
    for (size_t i = 0; i + 4 < mesh.interleavedPosUv.size(); i += 5)
    {
      const glm::vec3 p(mesh.interleavedPosUv[i], mesh.interleavedPosUv[i + 1],
                        mesh.interleavedPosUv[i + 2]);
      minV = glm::min(minV, p);
      maxV = glm::max(maxV, p);
    }
  }
  glm::mat4 fit(1.f);
  if (minV.x <= maxV.x)
  {
    const glm::vec3 center = (minV + maxV) * 0.5f;
    const glm::vec3 ext = maxV - minV;
    const float maxExtent =
        std::max(1e-5f, std::max(ext.x, std::max(ext.y, ext.z)));
    const float scale = 0.7f / maxExtent;
    fit = glm::scale(glm::mat4(1.f), glm::vec3(scale)) *
          glm::translate(glm::mat4(1.f), -center);
  }
  const glm::mat4 mvp = viewProj * bodyMat * boneMat * wearLocal * fit;
  // Keep parts alive for queue path: static cache keyed by item.
  static std::mutex partsMu;
  static std::unordered_map<std::string, std::vector<BoneSkeletonCubeMeshCpu>>
      partsCache;
  std::vector<BoneSkeletonCubeMeshCpu> *owned = &parts;
  if (!immediate && engine)
  {
    std::lock_guard<std::mutex> lock(partsMu);
    partsCache[itemId] = std::move(parts);
    owned = &partsCache[itemId];
  }
  for (const auto &mesh : *owned)
  {
    DrawMeshImmediateOrQueue(engine, shader, mvp, fallbackTex, mesh, immediate);
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
    scale = DefaultWieldScale(*def);
    offset[0] = def->Visual.WieldOffset[0];
    offset[1] = def->Visual.WieldOffset[1];
    offset[2] = def->Visual.WieldOffset[2];
    euler[0] = def->Visual.WieldEulerDeg[0];
    euler[1] = def->Visual.WieldEulerDeg[1];
    euler[2] = def->Visual.WieldEulerDeg[2];
  }
}

} // namespace

void WornEquipmentDrawer::SetHidePossessedWield(bool hide)
{
  g_HidePossessedWield = hide;
}

bool WornEquipmentDrawer::HidePossessedWield()
{
  return g_HidePossessedWield;
}

void WornEquipmentDrawer::SetItemDefinitions(const UItemDefinitionStorage *items)
{
  g_ItemDefinitions = items;
}

const UItemDefinitionStorage *WornEquipmentDrawer::ItemDefinitions()
{
  return g_ItemDefinitions;
}

void WornEquipmentDrawer::SubmitFromCreature(UGeometryEngine &engine,
                                             const UCreature &creature,
                                             const glm::mat4 &viewProj,
                                             const glm::mat4 &bodyMat,
                                             const BoneMatrixLookupFn &bones)
{
  std::array<WornArmorPreviewSlot, 6> slots{};
  for (size_t i = 0; i < 6; ++i)
  {
    const InventoryEntryRef &e = creature.GetInventory().GetEquippedArmor(i);
    if (!e.empty && !e.broken && e.kind == InventoryEntryKind::Item)
    {
      slots[i].ItemId = e.Id;
    }
  }
  SubmitFromSlots(engine, slots, viewProj, bodyMat, bones);
}

void WornEquipmentDrawer::SubmitWieldedFromCreature(
    UGeometryEngine &engine, const UCreature &creature,
    const UItemDefinitionStorage *items, const glm::mat4 &viewProj,
    const glm::mat4 &bodyMat, const BoneMatrixLookupFn &bones)
{
  if (g_HidePossessedWield && creature.IsPossessed())
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
  SubmitWieldedPreview(&engine, nullptr, mainId, offId, items, viewProj, bodyMat,
                       bones, false);
}

void WornEquipmentDrawer::SubmitWieldedPreview(
    UGeometryEngine *engine, UShaderProgram *shader, const std::string &mainItemId,
    const std::string &offhandItemId, const UItemDefinitionStorage *items,
    const glm::mat4 &viewProj, const glm::mat4 &bodyMat,
    const BoneMatrixLookupFn &bones, bool immediate)
{
  float scale = 1.25f;
  float offset[3] = {0, 0, 0};
  float euler[3] = {0, 0, 0};
  if (!mainItemId.empty())
  {
    ResolveWieldParams(items, mainItemId, scale, offset, euler);
    // Hands armor may also occupy *Item; nudge tool slightly outward.
    offset[1] += 0.05f;
    SubmitItemOnSingleBone(engine, shader, mainItemId, "rightItem", scale,
                           offset, euler, viewProj, bodyMat, bones, immediate);
  }
  if (!offhandItemId.empty())
  {
    ResolveWieldParams(items, offhandItemId, scale, offset, euler);
    offset[1] += 0.05f;
    SubmitItemOnSingleBone(engine, shader, offhandItemId, "leftItem", scale,
                           offset, euler, viewProj, bodyMat, bones, immediate);
  }
}

void WornEquipmentDrawer::SubmitFromSlots(
    UGeometryEngine &engine, const std::array<WornArmorPreviewSlot, 6> &slots,
    const glm::mat4 &viewProj, const glm::mat4 &bodyMat,
    const BoneMatrixLookupFn &bones)
{
  for (size_t i = 0; i < slots.size(); ++i)
  {
    SubmitItemOnBones(&engine, nullptr, slots[i].ItemId, i, viewProj, bodyMat,
                      bones, false);
  }
}

void WornEquipmentDrawer::DrawImmediate(
    const std::array<WornArmorPreviewSlot, 6> &slots, const glm::mat4 &viewProj,
    const glm::mat4 &bodyMat, const BoneMatrixLookupFn &bones,
    UShaderProgram *shader)
{
  for (size_t i = 0; i < slots.size(); ++i)
  {
    SubmitItemOnBones(nullptr, shader, slots[i].ItemId, i, viewProj, bodyMat,
                      bones, true);
  }
}

} // namespace cutum

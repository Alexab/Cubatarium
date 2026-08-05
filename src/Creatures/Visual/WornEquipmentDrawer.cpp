#include "Creatures/Visual/WornEquipmentDrawer.h"

#include "App/Platform/IUPlatformPaths.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Visual/CreatureDrawRequest.h"
#include "Creatures/Visual/CreatureDrawQueue.h"
#include "Creatures/Visual/Gltf/CreatureGltfLoader.h"
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Items/ItemGltfTextureCache.h"
#include "Render/Engine/GeometryEngine.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/GlIncludes.h"

#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace cutum
{
namespace
{

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
  if (!paths)
  {
    return {};
  }
  const std::string sibling =
      (std::filesystem::path("models/items") / itemId / "model.gltf").generic_string();
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
        // Lifetime: asset is cached globally for process lifetime.
        req.SkeletalMesh = &prim.mesh;
        engine->GetCreatureDrawQueue().Push(std::move(req));
      }
    }
  }
}

} // namespace

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

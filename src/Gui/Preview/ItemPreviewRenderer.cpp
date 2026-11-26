#include "Gui/Preview/ItemPreviewRenderer.h"
#include "Core/Memory/VisualAssetCensus.h"
#include "Core/Json/JsonVec.h"
#include "Core/Memory/StlSourceStorage.h"
#include "Gui/Preview/PreviewRendererStorage.h"
#include "Items/ItemDrawRequest.h"

#include "Items/ItemGltfTextureCache.h"
#include "Items/ItemSurfaceRenderer.h"

#include "App/Platform/IUPlatformPaths.h"
#include "Creatures/Visual/Gltf/CreatureGltfCache.h"
#include "Creatures/Visual/Gltf/CreatureGltfLoader.h"
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include "Items/ItemDefinitionStorage.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/GlIncludes.h"
#include "Render/Pipeline/GlStateMask.h"
#include "Render/Pipeline/GlStateScope.h"
#include "Render/Textures/GlTextureOwner.h"

#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

namespace cutum
{

namespace
{
constexpr float kDefaultOrbitDistance = 3.0f;
constexpr float kFovDeg = 35.0f;

std::unordered_set<std::string> gItemModelMissingLogged;

enum class FallbackColor { Wood, Leather, Iron, Stone, Gold, Diamond, Count };
constexpr unsigned char kFallbackPixels[][4] = {
    {158, 107, 56, 255}, {120, 78, 48, 255}, {185, 190, 200, 255},
    {140, 140, 132, 255}, {225, 170, 60, 255}, {120, 210, 255, 255},
};
static_assert(std::size(kFallbackPixels) == static_cast<size_t>(FallbackColor::Count));

FallbackColor ColorForItem(const std::string &id)
{
  if (id.find("leather") != std::string::npos)
    return FallbackColor::Leather;
  if (id.find("iron") != std::string::npos)
    return FallbackColor::Iron;
  if (id.find("stone") != std::string::npos)
    return FallbackColor::Stone;
  if (id.find("gold") != std::string::npos)
    return FallbackColor::Gold;
  if (id.find("diamond") != std::string::npos)
    return FallbackColor::Diamond;
  return FallbackColor::Wood;
}

} // namespace

UItemPreviewRenderer::UItemPreviewRenderer(
    std::shared_ptr<UItemDefinitionStorage> items,
    std::shared_ptr<UShaderManager> shaderManager)
    : Items(std::move(items)), ShaderManager(std::move(shaderManager))
{
}

UItemPreviewRenderer::~UItemPreviewRenderer() { Shutdown(); }

std::shared_ptr<UItemPreviewRenderer> UItemPreviewRenderer::Create(
    std::shared_ptr<UItemDefinitionStorage> items, std::shared_ptr<UShaderManager> shaders)
{
  auto renderer = std::make_shared<UItemPreviewRenderer>(std::move(items), std::move(shaders));
  renderer->FactoryOwner = true;
  return renderer;
}

void UItemPreviewRenderer::InspectVisualAssets(VisualAssetCensus &census) const
{
  census.Add(InspectionAsset, VisualAssetOwner::Preview);
  for (const auto &entry : InspectionAssets)
    census.Add(entry.second, VisualAssetOwner::Preview);
  if (SurfaceRenderer)
    SurfaceRenderer->InspectVisualAssets(census, VisualAssetOwner::Preview);
}

PreviewRendererStorage UItemPreviewRenderer::InspectOwnedStorage(const UItemDefinitionStorage *items,
    const UShaderManager *shaders) const noexcept
{
  PreviewRendererStorage out;
  out.present = true;
  out.factoryOwner = FactoryOwner;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  if (FactoryOwner)
    out.requestBytes = sizeof(std::_Ref_count_obj2<UItemPreviewRenderer>);
#endif
  StlSourceStorage tables, total;
  tables.Hash(InspectionAssets);
  tables.Text(InspectionPath.native());
  out.assetReferences = bool(InspectionAsset);
  for (const auto &[key, asset] : InspectionAssets)
  {
    tables.Text(key);
    out.assetReferences += bool(asset);
  }
  if (SurfaceRenderer)
    out.surface = SurfaceRenderer->InspectOwnedStorage();
  for (const auto &texture : ColorTextures)
    out.fallbackColorTexels += texture.IsOwned();
  out.tableBytes = tables.bytes.requested;
  total.Add(out.requestBytes);
  total.Add(out.tableBytes);
  total.Add(out.surface.cpuBytes);
  out.cpuBytes = total.bytes.requested;
  const bool target = InspectPreviewTarget(out, Target, FboSize);
  const bool mesh = InspectPreviewCube(out, Cube);
  out.resolveFramebuffer = IconResolveFbo;
  out.scratchVertexBytes = ScratchVertexBytes;
  out.scratchIndexBytes = ScratchIndexBytes;
  const bool scratch = ScratchMeshVao ? ScratchMeshVbo && ScratchMeshEbo && ScratchMeshVbo != ScratchMeshEbo :
      !ScratchMeshVbo && !ScratchMeshEbo && !ScratchVertexBytes && !ScratchIndexBytes;
  out.supported = items == Items.get() && shaders == ShaderManager.get() && FactoryOwner && target && mesh &&
      scratch && (!SurfaceRenderer || out.surface.supported) && !InspectionDrawObserver &&
      tables.bytes.supported && total.bytes.supported;
  return out;
}

bool UItemPreviewRenderer::Initialize()
{
  if (!ShaderManager || !Items)
  {
    return false;
  }

  Shader = ShaderManager->CreateShader(
      "gui_prefab_icon", "shaders/vshader.glsl", "shaders/fshader.glsl");
  if (!Shader || !Shader->IsValid())
  {
    return false;
  }
  IconResolveShader = ShaderManager->CreateShader(
      "item_icon_resolve", "shaders/item_icon_resolve.vert",
      "shaders/item_icon_resolve.frag");
  if (!IconResolveShader || !IconResolveShader->IsValid())
    return false;

  if (!InitCubeMesh())
  {
    return false;
  }

  SurfaceRenderer = std::make_unique<ItemSurfaceRenderer>();
  if (!SurfaceRenderer->Initialize(*ShaderManager))
    return false;
  return EnsureFboSize(256);
}

void UItemPreviewRenderer::Shutdown()
{
  Invalidate();
  SurfaceRenderer.reset();
  Cube.Reset();
  Shader.reset();
  IconResolveShader.reset();
}

void UItemPreviewRenderer::Invalidate()
{
  InspectionAssets.clear();
  if (SurfaceRenderer)
    SurfaceRenderer->Clear();
  ReleaseFramebuffer();

  if (ScratchMeshEbo != 0)
  {
    glDeleteBuffers(1, &ScratchMeshEbo);
    ScratchMeshEbo = 0;
  }
  if (ScratchMeshVbo != 0)
  {
    glDeleteBuffers(1, &ScratchMeshVbo);
    ScratchMeshVbo = 0;
  }
  if (ScratchMeshVao != 0)
  {
    glDeleteVertexArrays(1, &ScratchMeshVao);
    ScratchMeshVao = 0;
  }
  ScratchVertexBytes = ScratchIndexBytes = 0;

  for (auto &texture : ColorTextures)
    texture.Reset();
}

bool UItemPreviewRenderer::InitCubeMesh()
{
  if (Cube.Vao != 0)
  {
    return true;
  }

  // Must match UContentPreviewRenderer cube mesh for shader compatibility.
  const float cubeShift = 1.0f / 6.0f;
  const float vertices[] = {
      -0.5f,
      -0.5f,
      0.5f,
      0.0f,
      1.0f,
      0.5f,
      -0.5f,
      0.5f,
      cubeShift,
      1.0f,
      -0.5f,
      0.5f,
      0.5f,
      0.0f,
      0.0f,
      0.5f,
      0.5f,
      0.5f,
      cubeShift,
      0.0f,
      0.5f,
      -0.5f,
      0.5f,
      cubeShift,
      1.0f,
      0.5f,
      -0.5f,
      -0.5f,
      cubeShift * 2.0f,
      1.0f,
      0.5f,
      0.5f,
      0.5f,
      cubeShift,
      0.0f,
      0.5f,
      0.5f,
      -0.5f,
      cubeShift * 2.0f,
      0.0f,
      0.5f,
      -0.5f,
      -0.5f,
      cubeShift * 2.0f,
      1.0f,
      -0.5f,
      -0.5f,
      -0.5f,
      cubeShift * 3.0f,
      1.0f,
      0.5f,
      0.5f,
      -0.5f,
      cubeShift * 2.0f,
      0.0f,
      -0.5f,
      0.5f,
      -0.5f,
      cubeShift * 3.0f,
      0.0f,
      -0.5f,
      -0.5f,
      -0.5f,
      cubeShift * 3.0f,
      1.0f,
      -0.5f,
      -0.5f,
      0.5f,
      cubeShift * 4.0f,
      1.0f,
      -0.5f,
      0.5f,
      -0.5f,
      cubeShift * 3.0f,
      0.0f,
      -0.5f,
      0.5f,
      0.5f,
      cubeShift * 4.0f,
      0.0f,
      -0.5f,
      0.5f,
      0.5f,
      cubeShift * 4.0f,
      0.0f,
      0.5f,
      0.5f,
      0.5f,
      cubeShift * 5.0f,
      0.0f,
      -0.5f,
      0.5f,
      -0.5f,
      cubeShift * 4.0f,
      1.0f,
      0.5f,
      0.5f,
      -0.5f,
      cubeShift * 5.0f,
      1.0f,
      -0.5f,
      -0.5f,
      -0.5f,
      cubeShift * 5.0f,
      0.0f,
      0.5f,
      -0.5f,
      -0.5f,
      1.0f,
      0.0f,
      -0.5f,
      -0.5f,
      0.5f,
      cubeShift * 5.0f,
      1.0f,
      0.5f,
      -0.5f,
      0.5f,
      1.0f,
      1.0f,
  };

  const unsigned int indices[] = {
      0,  1,  2,  2,  1,  3,  4,  5,  6,  6,  5,  7,  8,  9,  10, 10, 9,  11,
      12, 13, 14, 14, 13, 15, 16, 17, 18, 18, 17, 19, 20, 21, 22, 22, 21, 23,
  };

  return Cube.Initialize(vertices, indices);
}

void UItemPreviewRenderer::ReleaseFramebuffer()
{
  if (IconResolveFbo)
  {
    glDeleteFramebuffers(1, &IconResolveFbo);
    IconResolveFbo = 0;
  }
  Target.Reset();
  FboSize = 0;
}

bool UItemPreviewRenderer::EnsureFboSize(int size)
{
  const int clamped = std::clamp(size, 32, 512);
  if (Target.Framebuffer && FboSize == clamped)
    return true;
  if (!Target.Initialize(clamped))
    return false;
  FboSize = clamped;
  return true;
}

glm::mat4 UItemPreviewRenderer::OrbitView(float yawDeg, float pitchDeg,
                                          float distance) const
{
  const float yaw = glm::radians(yawDeg);
  const float pitch = glm::radians(pitchDeg);
  const float cosPitch = std::cos(pitch);
  const glm::vec3 eye(distance * cosPitch * std::sin(yaw),
                      distance * std::sin(pitch),
                      distance * cosPitch * std::cos(yaw));
  return glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

bool UItemPreviewRenderer::TryLoadPartsFromModelJson(
    const std::string &itemId, std::string &outModelRelPath,
    std::vector<Part> &outParts) const
{
  outParts.clear();
  if (!Items)
  {
    return false;
  }
  const auto *def = Items->Get(itemId);
  if (!def || def->ModelPath.empty())
  {
    return false;
  }

  outModelRelPath = def->ModelPath;
  IUPlatformPaths *paths = IUPlatformPaths::TryGet();
  if (!paths)
  {
    return false;
  }

  std::string jsonText;
  if (!paths->ReadAssetText(outModelRelPath, jsonText))
  {
    if (gItemModelMissingLogged.insert(itemId).second)
    {
      std::cerr << "ItemPreviewRenderer: missing model asset for item="
                << itemId << " path=" << outModelRelPath
                << " (using procedural fallback)\n";
    }
    return false;
  }

  nlohmann::json data;
  try
  {
    data = nlohmann::json::parse(jsonText);
  }
  catch (...)
  {
    if (gItemModelMissingLogged.insert(itemId + "|parse").second)
    {
      std::cerr << "ItemPreviewRenderer: failed to parse model for item="
                << itemId << " path=" << outModelRelPath << '\n';
    }
    return false;
  }

  // Prefer parts[] JSON authoring. Non-parts files (e.g. future wrappers)
  // fall through to glTF / procedural paths without spam.
  if (!data.contains("parts") || !data["parts"].is_array())
  {
    return false;
  }

  for (const auto &partJson : data["parts"])
  {
    Part p;
    p.textureStem = partJson.value("texture", "");
    const glm::vec3 off =
        ReadVec3(partJson.value("offset", nlohmann::json::array()),
                 glm::vec3{0.f, 0.f, 0.f});
    const glm::vec3 sz =
        ReadVec3(partJson.value("size", nlohmann::json::array()),
                 glm::vec3{1.f, 1.f, 1.f});
    p.ox = off.x;
    p.oy = off.y;
    p.oz = off.z;
    p.sx = sz.x;
    p.sy = sz.y;
    p.sz = sz.z;
    outParts.push_back(p);
  }

  return !outParts.empty();
}

bool UItemPreviewRenderer::TryDrawGltfModel(const std::string &itemId,
                                            const std::string &modelRel,
                                            const glm::mat4 &projection,
                                            const glm::mat4 &view,
                                            const ItemVisualInputs &inputs)
{
  IUPlatformPaths *paths = IUPlatformPaths::TryGet();
  if (!paths || modelRel.empty())
  {
    return false;
  }

  std::string gltfRel = modelRel;
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
  const bool isGltf = endsWithIgnoreCase(modelRel, ".gltf") ||
                      endsWithIgnoreCase(modelRel, ".glb");
  if (!isGltf)
  {
    // Convention: models/items/<id>/model.gltf next to or instead of .json
    const std::filesystem::path asDir =
        std::filesystem::path(modelRel).parent_path() /
        std::filesystem::path(modelRel).stem() / "model.gltf";
    const std::filesystem::path sibling =
        std::filesystem::path("models/items") / itemId / "model.gltf";
    if (paths->AssetExists(asDir.generic_string()))
    {
      gltfRel = asDir.generic_string();
    }
    else if (paths->AssetExists(sibling.generic_string()))
    {
      gltfRel = sibling.generic_string();
    }
    else
    {
      return false;
    }
  }
  else if (!paths->AssetExists(gltfRel))
  {
    return false;
  }

  const std::filesystem::path absPath = paths->AssetRoot() / gltfRel;
  return DrawGltfPath(itemId, absPath, projection, view, inputs);
}

bool UItemPreviewRenderer::DrawGltfPath(const std::string &itemId,
                                        const std::filesystem::path &absPath,
                                        const glm::mat4 &projection,
                                        const glm::mat4 &view,
                                        const ItemVisualInputs &inputs)
{
  if (const auto surface =
          InspectionAsset
              ? InspectionAsset
              : FindItemSurfaceAsset(absPath,
                                     UItemDefinitionStorage::ContentRevision()))
  {
    const auto state = EvaluateItemVisualState(*surface, inputs);
    const auto bounds = InspectionFramingBounds.value_or(
        std::make_pair(surface->Low, surface->High));
    const auto center = (bounds.first + bounds.second) * .5f;
    const auto extent = bounds.second - bounds.first;
    const auto *definition = Items->Get(itemId);
    const float scale =
        1.2f / std::max(1e-5f, std::max({extent.x, extent.y, extent.z})) *
        (definition ? definition->Visual.IconScale : 1.f);
    const auto angles = glm::radians(surface->Grip.IconEulerDeg);
    const auto rotation =
        glm::rotate(glm::mat4(1), angles.x, glm::vec3(1, 0, 0)) *
        glm::rotate(glm::mat4(1), angles.y, glm::vec3(0, 1, 0)) *
        glm::rotate(glm::mat4(1), angles.z, glm::vec3(0, 0, 1));
    const auto model = glm::scale(glm::mat4(1), glm::vec3(scale)) * rotation *
                       glm::translate(glm::mat4(1), -center);
    ItemPresentationProfile profile;
    profile.InspectionMode = InspectionMode;
    profile.Ambient = InspectionAmbient;
    profile.CameraPosition = glm::vec3(glm::inverse(view)[3]);
    bool geometryReady = false;
    SurfaceRenderer->Draw(surface, absPath, projection * view, model, state,
                          profile, &geometryReady);
    if (!geometryReady)
      return false;
    if (InspectionDrawObserver)
      InspectionDrawObserver({surface,
                              absPath,
                              projection * view,
                              model,
                              state,
                              surface->Low,
                              surface->High,
                              {}});
    return true;
  }
  // UX R2: route through the shared glTF cache - re-parsing the model JSON
  // + .bin from disk per icon render was the first-open stall driver.
  auto asset = CreatureGltfCache::Instance().LoadFromPath(absPath);
  if (!asset || asset->primitives.empty())
  {
    if (gItemModelMissingLogged.insert(itemId + "|gltf").second)
    {
      std::cerr << "ItemPreviewRenderer: failed to load glTF for item="
                << itemId << " path=" << absPath << '\n';
    }
    return false;
  }

  glm::vec3 minV(1e9f);
  glm::vec3 maxV(-1e9f);
  for (const GltfPrimitiveCpu &prim : asset->primitives)
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
    return false;
  }

  const glm::vec3 center = (minV + maxV) * 0.5f;
  const glm::vec3 ext = maxV - minV;
  const float maxExtent =
      std::max(1e-5f, std::max(ext.x, std::max(ext.y, ext.z)));
  const auto *definition = Items ? Items->Get(itemId) : nullptr;
  const float scale =
      1.2f / maxExtent * (definition ? definition->Visual.IconScale : 1.f);
  const glm::mat4 modelMat = glm::scale(glm::mat4(1.f), glm::vec3(scale)) *
                             glm::translate(glm::mat4(1.f), -center);
  const glm::mat4 mvp = projection * view * modelMat;

  if (ScratchMeshVao == 0)
  {
    glGenVertexArrays(1, &ScratchMeshVao);
    glGenBuffers(1, &ScratchMeshVbo);
    glGenBuffers(1, &ScratchMeshEbo);
    if (!ScratchMeshVao || !ScratchMeshVbo || !ScratchMeshEbo)
    {
      glDeleteVertexArrays(1, &ScratchMeshVao);
      glDeleteBuffers(1, &ScratchMeshVbo);
      glDeleteBuffers(1, &ScratchMeshEbo);
      ScratchMeshVao = ScratchMeshVbo = ScratchMeshEbo = 0;
      return false;
    }
  }

  const GLuint fallbackTex = GetOrCreateColorTexture(itemId);

  Shader->Use();
  Shader->SetInt("texture0", 0);
  Shader->SetInt("uAnimFrame", 0);
  Shader->SetInt("uAnimFrameCount", 1);

  bool drew = false;
  for (const GltfPrimitiveCpu &prim : asset->primitives)
  {
    const auto &mesh = prim.mesh;
    constexpr auto maxBytes = static_cast<size_t>(std::numeric_limits<GLsizeiptr>::max());
    if (mesh.interleavedPosUv.empty() || mesh.indices.empty() ||
        mesh.interleavedPosUv.size() > maxBytes / sizeof(float) ||
        mesh.indices.size() > maxBytes / sizeof(unsigned) ||
        mesh.indices.size() > static_cast<size_t>(std::numeric_limits<GLsizei>::max()))
    {
      continue;
    }
    GLuint tex =
        ItemGltfTextureCache::Instance().Get(absPath, prim.textureStem);
    if (tex == 0)
      tex = fallbackTex;
    if (tex == 0)
      continue;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    Shader->SetMat4("mvp_matrix", mvp);
    glBindVertexArray(ScratchMeshVao);
    glBindBuffer(GL_ARRAY_BUFFER, ScratchMeshVbo);
    const auto vertexRequest = GLsizeiptr(mesh.interleavedPosUv.size() * sizeof(float));
    const auto indexRequest = GLsizeiptr(mesh.indices.size() * sizeof(unsigned));
    glBufferData(GL_ARRAY_BUFFER, vertexRequest, mesh.interleavedPosUv.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ScratchMeshEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexRequest, mesh.indices.data(), GL_STREAM_DRAW);
    GLint64 vertexBytes = 0, indexBytes = 0;
    glGetBufferParameteri64v(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &vertexBytes);
    glGetBufferParameteri64v(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &indexBytes);
    ScratchVertexBytes = size_t(vertexBytes);
    ScratchIndexBytes = size_t(indexBytes);
    if (vertexBytes != vertexRequest || indexBytes != indexRequest)
      continue;
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          reinterpret_cast<void *>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()),
                   GL_UNSIGNED_INT, nullptr);
    drew = true;
  }

  glBindVertexArray(0);
  Shader->Unuse();
  return drew;
}

std::vector<UItemPreviewRenderer::Part>
UItemPreviewRenderer::FallbackParts(const std::string &itemId) const
{
  if (itemId.find("sword") != std::string::npos)
  {
    return {
        Part{"metal", 0.f, -0.1f, 0.f, 0.04f, 0.6f, 0.04f},
        Part{"metal", 0.f, 0.25f, 0.f, 0.18f, 0.06f, 0.04f},
    };
  }
  if (itemId.find("shovel") != std::string::npos)
  {
    return {
        Part{"metal", 0.f, -0.1f, 0.f, 0.05f, 0.65f, 0.05f},
        Part{"metal", 0.f, 0.25f, 0.f, 0.18f, 0.05f, 0.08f},
    };
  }
  if (itemId.find("axe") != std::string::npos)
  {
    return {
        Part{"metal", 0.f, -0.1f, 0.f, 0.05f, 0.6f, 0.05f},
        Part{"metal", 0.f, 0.25f, 0.f, 0.16f, 0.08f, 0.22f},
    };
  }
  if (itemId.find("pickaxe") != std::string::npos)
  {
    return {
        Part{"metal", 0.f, -0.1f, 0.f, 0.05f, 0.65f, 0.05f},
        Part{"metal", 0.f, 0.25f, 0.f, 0.2f, 0.1f, 0.06f},
        Part{"metal", 0.02f, 0.3f, 0.04f, 0.06f, 0.05f, 0.06f},
        Part{"metal", -0.02f, 0.3f, 0.04f, 0.06f, 0.05f, 0.06f},
    };
  }
  if (itemId.find("hammer") != std::string::npos ||
      itemId.find("mallet") != std::string::npos)
  {
    return {
        Part{"metal", 0.f, -0.1f, 0.f, 0.05f, 0.55f, 0.05f},
        Part{"metal", 0.f, 0.28f, 0.f, 0.22f, 0.14f, 0.12f},
    };
  }
  return {Part{"metal", 0.f, 0.f, 0.f, 0.2f, 0.2f, 0.2f}};
}

GLuint UItemPreviewRenderer::GetOrCreateColorTexture(const std::string &itemId)
{
  static_assert(std::tuple_size_v<decltype(ColorTextures)> == std::size(kFallbackPixels));
  const auto color = static_cast<size_t>(ColorForItem(itemId));
  auto &cached = ColorTextures[color];
  if (cached.Get())
    return cached.Get();

  GLuint tex = 0;
  glGenTextures(1, &tex);
  UGlTextureOwner candidate(tex);
  if (!tex)
    return 0;
  glBindTexture(GL_TEXTURE_2D, tex);
  {
    UGlPixelUnpackScope unpack;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 kFallbackPixels[color]);
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  GLint width = 0;
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
  glBindTexture(GL_TEXTURE_2D, 0);
  if (width != 1)
    return 0;
  cached = std::move(candidate);
  return cached.Get();
}

GLuint
UItemPreviewRenderer::RenderToUniqueTexture(const std::string &itemId, int size,
                                            float yawDeg, float pitchDeg,
                                            const ItemVisualInputs &inputs)
{
  if (!Shader || !Items || itemId.empty())
  {
    return 0;
  }
  if (!RenderingIconSamples && size >= 32 && size <= 64)
  {
    struct Sampling
    {
      bool &Active;
      explicit Sampling(bool &active) : Active(active) { Active = true; }
      ~Sampling() { Active = false; }
    } sampling(RenderingIconSamples);
    UGlTextureOwner source(RenderToUniqueTexture(itemId, size * 4, yawDeg, pitchDeg, inputs));
    return source.Get() ? ResolveIconTexture(source.Get(), size) : 0;
  }
  UGlStateScope glState(kGlMaskIconFbo | GlStateBit::DepthMask);
  if (!EnsureFboSize(size) || FboSize <= 0)
  {
    return 0;
  }

  const int clamped = std::max(32, std::min(size, 512));

  GLuint tex{0};
  glGenTextures(1, &tex);
  UGlTextureOwner texture(tex);
  if (!tex)
    return 0;
  glBindTexture(GL_TEXTURE_2D, tex);
  AllocatePreviewColor(clamped);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  UGlIconColorAttachment attachment(Target.Framebuffer, tex, Target.Color);
  if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    return 0;

  glViewport(0, 0, clamped, clamped);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDisable(GL_CULL_FACE);
  if (InspectionMode == 1)
    glClearColor(.95f, .95f, .95f, 1.f);
  else
    glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  const glm::mat4 projection =
      glm::perspective(glm::radians(kFovDeg), 1.0f, 0.1f, 50.0f);
  const glm::mat4 view = OrbitView(yawDeg, pitchDeg, kDefaultOrbitDistance);

  std::string modelRel;
  const auto *def = Items->Get(itemId);
  if (def)
  {
    modelRel = def->ModelPath;
  }

  // Prefer static glTF when present, then parts[] cubes, then procedural.
  if (InspectionAsset
          ? DrawGltfPath(itemId, InspectionPath, projection, view, inputs)
          : TryDrawGltfModel(itemId, modelRel, projection, view, inputs))
  {
    return texture.Release();
  }

  std::vector<Part> parts;
  std::string partsRel;
  if (!TryLoadPartsFromModelJson(itemId, partsRel, parts))
  {
    parts = FallbackParts(itemId);
  }
  if (parts.empty())
  {
    return 0;
  }

  glm::vec3 minV(1e9f);
  glm::vec3 maxV(-1e9f);
  for (const Part &p : parts)
  {
    const glm::vec3 half{p.sx * 0.5f, p.sy * 0.5f, p.sz * 0.5f};
    const glm::vec3 c{p.ox, p.oy, p.oz};
    minV = glm::min(minV, c - half);
    maxV = glm::max(maxV, c + half);
  }
  const glm::vec3 center = (minV + maxV) * 0.5f;
  const glm::vec3 ext = maxV - minV;
  const float maxExtent =
      std::max(1e-5f, std::max(ext.x, std::max(ext.y, ext.z)));
  const auto *definition = Items ? Items->Get(itemId) : nullptr;
  const float scale =
      1.2f / maxExtent * (definition ? definition->Visual.IconScale : 1.f);

  const GLuint colorTex = GetOrCreateColorTexture(itemId);
  if (!colorTex)
    return 0;
  Shader->Use();
  Shader->SetInt("texture0", 0);
  Shader->SetInt("uAnimFrame", 0);
  Shader->SetInt("uAnimFrameCount", 1);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, colorTex);

  glBindVertexArray(Cube.Vao);
  for (const Part &p : parts)
  {
    const glm::vec3 c = (glm::vec3{p.ox, p.oy, p.oz} - center) * scale;
    const glm::vec3 s = glm::vec3{p.sx, p.sy, p.sz} * scale;
    const glm::mat4 model =
        glm::translate(glm::mat4(1.0f), c) * glm::scale(glm::mat4(1.0f), s);
    const glm::mat4 mvp = projection * view * model;
    Shader->SetMat4("mvp_matrix", mvp);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
  }

  glBindVertexArray(0);
  Shader->Unuse();

  return texture.Release();
}

GLuint UItemPreviewRenderer::ResolveIconTexture(GLuint source, int size)
{
  if (!source || size < 1 || size > 64 || !IconResolveShader)
    return 0;
  UGlStateScope scope(kGlMaskIconFbo | GlStateBit::DepthMask |
                      GlStateBit::ColorMask);
  GLint program = 0, vao = 0, active = 0, texture = 0;
  glGetIntegerv(GL_CURRENT_PROGRAM, &program);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
  struct Restore
  {
    GLint Program, Vao, Active, Texture;
    bool Scissor;
    ~Restore()
    {
      glUseProgram(GLuint(Program));
      glBindVertexArray(GLuint(Vao));
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, GLuint(Texture));
      glActiveTexture(GLenum(Active));
      if (Scissor)
        glEnable(GL_SCISSOR_TEST);
      else
        glDisable(GL_SCISSOR_TEST);
    }
  } restore{program, vao, active, texture, glIsEnabled(GL_SCISSOR_TEST) != 0};
  glBindTexture(GL_TEXTURE_2D, source);
  GLint width = 0, height = 0;
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
  if (height != size * 4 || width != size * 4)
    return 0;
  GLuint result = 0;
  glGenTextures(1, &result);
  UGlTextureOwner textureResult(result);
  if (!result)
    return 0;
  glBindTexture(GL_TEXTURE_2D, result);
  AllocatePreviewColor(size, GL_RGBA8);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  if (!IconResolveFbo)
    glGenFramebuffers(1, &IconResolveFbo);
  if (!IconResolveFbo)
    return 0;
  UGlIconColorAttachment attachment(IconResolveFbo, result, 0);
  if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    return 0;
  glViewport(0, 0, size, size);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  IconResolveShader->Use();
  IconResolveShader->SetInt("uSamples", 0);
  glBindTexture(GL_TEXTURE_2D, source);
  glBindVertexArray(Cube.Vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  return textureResult.Release();
}

std::shared_ptr<const ItemRenderAsset>
UItemPreviewRenderer::GetInspectionAsset(const std::filesystem::path &path)
{
  const auto key = std::filesystem::weakly_canonical(path).generic_string();
  auto &asset = InspectionAssets[key];
  if (!asset)
    asset = LoadItemRenderAsset(path, true);
  return asset;
}

GLuint UItemPreviewRenderer::RenderInspectionTexture(
    const std::string &itemId, const std::filesystem::path &path, int size,
    float yaw, float pitch, int mode, float ambient,
    const ItemVisualInputs &inputs)
{
  InspectionAsset = GetInspectionAsset(path);
  InspectionPath = path;
  InspectionMode = std::clamp(mode, 0, 6);
  InspectionAmbient = std::clamp(ambient, 0.f, 1.f);
  try
  {
    const auto texture =
        RenderToUniqueTexture(itemId, size, yaw, pitch, inputs);
    InspectionAsset.reset();
    InspectionMode = 0;
    InspectionAmbient = .65f;
    return texture;
  }
  catch (...)
  {
    InspectionAsset.reset();
    InspectionMode = 0;
    InspectionAmbient = .65f;
    throw;
  }
}
} // namespace cutum

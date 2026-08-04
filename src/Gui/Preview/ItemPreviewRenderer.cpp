#include "Gui/Preview/ItemPreviewRenderer.h"

#include "App/Platform/IUPlatformPaths.h"
#include "Creatures/Visual/Gltf/CreatureGltfLoader.h"
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include "Items/ItemDefinitionStorage.h"
#include "Render/GlIncludes.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/Pipeline/GlStateMask.h"
#include "Render/Pipeline/GlStateScope.h"

#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
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

struct Rgb
{
  unsigned char r{158};
  unsigned char g{107};
  unsigned char b{56};
};

  Rgb ToolRgb(const std::string &id)
  {
    // Rough palette to make rotation silhouettes readable even without textures.
    if (id.find("leather") != std::string::npos)
    {
      return Rgb{120, 78, 48};
    }
    if (id.find("iron") != std::string::npos)
    {
      return Rgb{185, 190, 200};
    }
    if (id.find("stone") != std::string::npos)
    {
      return Rgb{140, 140, 132};
    }
    if (id.find("gold") != std::string::npos)
    {
      return Rgb{225, 170, 60};
    }
    if (id.find("diamond") != std::string::npos)
    {
      return Rgb{120, 210, 255};
    }
    if (id.find("wood") != std::string::npos)
    {
      return Rgb{158, 107, 56};
    }
    return Rgb{158, 107, 56};
  }

glm::vec3 ReadVec3(const nlohmann::json &arr, const glm::vec3 &fallback)
{
  if (!arr.is_array() || arr.size() < 3)
  {
    return fallback;
  }
  return glm::vec3(arr[0].get<float>(), arr[1].get<float>(),
                   arr[2].get<float>());
}

} // namespace

UItemPreviewRenderer::UItemPreviewRenderer(
    std::shared_ptr<UItemDefinitionStorage> items,
    std::shared_ptr<UShaderManager> shaderManager)
    : Items(std::move(items)), ShaderManager(std::move(shaderManager))
{
}

UItemPreviewRenderer::~UItemPreviewRenderer() { Shutdown(); }

bool UItemPreviewRenderer::Initialize()
{
  if (!ShaderManager || !Items)
  {
    return false;
  }

  Shader = ShaderManager->CreateShader("gui_prefab_icon", "shaders/vshader.glsl",
                                       "shaders/fshader.glsl");
  if (!Shader || !Shader->IsValid())
  {
    return false;
  }

  if (!InitCubeMesh())
  {
    return false;
  }

  return EnsureFboSize(256);
}

void UItemPreviewRenderer::Shutdown()
{
  Invalidate();

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
  if (CubeEbo != 0)
  {
    glDeleteBuffers(1, &CubeEbo);
    CubeEbo = 0;
  }
  if (CubeVbo != 0)
  {
    glDeleteBuffers(1, &CubeVbo);
    CubeVbo = 0;
  }
  if (CubeVao != 0)
  {
    glDeleteVertexArrays(1, &CubeVao);
    CubeVao = 0;
  }

  Shader.reset();
}

void UItemPreviewRenderer::Invalidate()
{
  if (DepthRbo != 0)
  {
    glDeleteRenderbuffers(1, &DepthRbo);
    DepthRbo = 0;
  }
  if (ColorTex != 0)
  {
    glDeleteTextures(1, &ColorTex);
    ColorTex = 0;
  }
  if (Fbo != 0)
  {
    glDeleteFramebuffers(1, &Fbo);
    Fbo = 0;
  }
  FboSize = 0;

  for (auto &kv : ColorTexCache)
  {
    if (kv.second != 0)
    {
      glDeleteTextures(1, &kv.second);
    }
  }
  ColorTexCache.clear();
}

bool UItemPreviewRenderer::InitCubeMesh()
{
  if (CubeVao != 0)
  {
    return true;
  }

  // Must match UContentPreviewRenderer cube mesh for shader compatibility.
  const float cubeShift = 1.0f / 6.0f;
  const float vertices[] = {
      -0.5f, -0.5f, 0.5f,  0.0f, 1.0f,  0.5f,  -0.5f, 0.5f,  cubeShift, 1.0f,
      -0.5f, 0.5f,  0.5f,  0.0f, 0.0f,  0.5f,  0.5f,  0.5f,  cubeShift, 0.0f,
      0.5f,  -0.5f, 0.5f,  cubeShift, 1.0f,  0.5f,  -0.5f, -0.5f, cubeShift * 2.0f,
      1.0f,  0.5f,  0.5f,  0.5f,  cubeShift, 0.0f,  0.5f,  0.5f,  -0.5f, cubeShift * 2.0f,
      0.0f,  0.5f,  -0.5f, -0.5f, cubeShift * 2.0f, 1.0f, -0.5f, -0.5f, -0.5f,
      cubeShift * 3.0f, 1.0f, 0.5f, 0.5f, -0.5f, cubeShift * 2.0f, 0.0f, 0.5f, 0.5f,
      -0.5f, cubeShift * 3.0f, 0.0f, -0.5f, -0.5f, -0.5f, cubeShift * 3.0f, 1.0f,
      -0.5f, -0.5f, 0.5f,  cubeShift * 4.0f, 1.0f, -0.5f, 0.5f, -0.5f, cubeShift * 3.0f,
      0.0f,  -0.5f, 0.5f,  0.5f, cubeShift * 4.0f, 0.0f, -0.5f, 0.5f, 0.5f,
      cubeShift * 4.0f, 0.0f, 0.5f,  0.5f, 0.5f, cubeShift * 5.0f, 0.0f, -0.5f, 0.5f,
      -0.5f, cubeShift * 4.0f, 1.0f, 0.5f, 0.5f, -0.5f, cubeShift * 5.0f, 1.0f,
      -0.5f, -0.5f, -0.5f, cubeShift * 5.0f, 0.0f, 0.5f, -0.5f, -0.5f, 1.0f, 0.0f,
      -0.5f, -0.5f, 0.5f,  cubeShift * 5.0f, 1.0f, 0.5f, -0.5f, 0.5f, 1.0f, 1.0f,
  };

  const unsigned int indices[] = {
      0,  1,  2,  2,  1,  3,  4,  5,  6,  6,  5,  7,  8,  9,  10, 10, 9,  11,
      12, 13, 14, 14, 13, 15, 16, 17, 18, 18, 17, 19, 20, 21, 22, 22, 21, 23,
  };

  glGenVertexArrays(1, &CubeVao);
  glGenBuffers(1, &CubeVbo);
  glGenBuffers(1, &CubeEbo);

  glBindVertexArray(CubeVao);
  glBindBuffer(GL_ARRAY_BUFFER, CubeVbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, CubeEbo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
               GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                         reinterpret_cast<void *>(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  glBindVertexArray(0);
  return true;
}

bool UItemPreviewRenderer::EnsureFboSize(int size)
{
  const int clamped = std::max(64, std::min(size, 512));
  if (Fbo != 0 && FboSize == clamped)
  {
    return true;
  }

  Invalidate();

  glGenFramebuffers(1, &Fbo);
  glGenTextures(1, &ColorTex);
  glBindTexture(GL_TEXTURE_2D, ColorTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, clamped, clamped, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glBindFramebuffer(GL_FRAMEBUFFER, Fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         ColorTex, 0);

  glGenRenderbuffers(1, &DepthRbo);
  glBindRenderbuffer(GL_RENDERBUFFER, DepthRbo);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, clamped,
                         clamped);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, DepthRbo);

  const bool complete =
      glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  FboSize = complete ? clamped : 0;
  return complete;
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
    const glm::vec3 off = ReadVec3(partJson.value("offset", nlohmann::json::array()),
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
                                            const glm::mat4 &view)
{
  IUPlatformPaths *paths = IUPlatformPaths::TryGet();
  if (!paths || modelRel.empty())
  {
    return false;
  }

  std::string gltfRel = modelRel;
  auto endsWithIgnoreCase = [](const std::string &s, const char *suf) {
    const size_t n = std::char_traits<char>::length(suf);
    if (s.size() < n)
    {
      return false;
    }
    for (size_t i = 0; i < n; ++i)
    {
      const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(
          s[s.size() - n + i])));
      const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(
          suf[i])));
      if (a != b)
      {
        return false;
      }
    }
    return true;
  };
  const bool isGltf =
      endsWithIgnoreCase(modelRel, ".gltf") || endsWithIgnoreCase(modelRel, ".glb");
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
  auto asset = CreatureGltfLoader::LoadFromFile(absPath.string());
  if (!asset || asset->primitives.empty())
  {
    if (gItemModelMissingLogged.insert(itemId + "|gltf").second)
    {
      std::cerr << "ItemPreviewRenderer: failed to load glTF for item="
                << itemId << " path=" << gltfRel << '\n';
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
  const float scale = 1.2f / maxExtent;
  const glm::mat4 modelMat =
      glm::scale(glm::mat4(1.f), glm::vec3(scale)) *
      glm::translate(glm::mat4(1.f), -center);
  const glm::mat4 mvp = projection * view * modelMat;

  if (ScratchMeshVao == 0)
  {
    glGenVertexArrays(1, &ScratchMeshVao);
    glGenBuffers(1, &ScratchMeshVbo);
    glGenBuffers(1, &ScratchMeshEbo);
  }

  const GLuint colorTex = GetOrCreateColorTexture(itemId);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, colorTex);

  Shader->Use();
  Shader->SetInt("texture0", 0);
  Shader->SetInt("uAnimFrame", 0);
  Shader->SetInt("uAnimFrameCount", 1);
  Shader->SetMat4("mvp_matrix", mvp);

  bool drew = false;
  for (const GltfPrimitiveCpu &prim : asset->primitives)
  {
    const auto &mesh = prim.mesh;
    if (mesh.interleavedPosUv.empty() || mesh.indices.empty())
    {
      continue;
    }
    glBindVertexArray(ScratchMeshVao);
    glBindBuffer(GL_ARRAY_BUFFER, ScratchMeshVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh.interleavedPosUv.size() *
                                         sizeof(float)),
                 mesh.interleavedPosUv.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ScratchMeshEbo);
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
  return {Part{"metal", 0.f, 0.f, 0.f, 0.2f, 0.2f, 0.2f}};
}

GLuint UItemPreviewRenderer::GetOrCreateColorTexture(const std::string &itemId)
{
  const auto it = ColorTexCache.find(itemId);
  if (it != ColorTexCache.end())
  {
    return it->second;
  }

  const Rgb rgb = ToolRgb(itemId);
  const unsigned char rgba[4] = {rgb.r, rgb.g, rgb.b, 255};

  GLuint tex{0};
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               rgba);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);

  ColorTexCache[itemId] = tex;
  return tex;
}

GLuint UItemPreviewRenderer::RenderToUniqueTexture(const std::string &itemId,
                                                     int size, float yawDeg,
                                                     float pitchDeg)
{
  if (!Shader || !Items || itemId.empty())
  {
    return 0;
  }
  if (!EnsureFboSize(size) || FboSize <= 0)
  {
    return 0;
  }

  const int clamped = std::max(64, std::min(size, 512));

  GLuint tex{0};
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, clamped, clamped, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  UGlStateScope glState(kGlMaskIconFbo);
  glBindFramebuffer(GL_FRAMEBUFFER, Fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         tex, 0);

  glViewport(0, 0, clamped, clamped);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDisable(GL_CULL_FACE);
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
  if (TryDrawGltfModel(itemId, modelRel, projection, view))
  {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           ColorTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
  }

  std::vector<Part> parts;
  std::string partsRel;
  if (!TryLoadPartsFromModelJson(itemId, partsRel, parts))
  {
    parts = FallbackParts(itemId);
  }
  if (parts.empty())
  {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, ColorTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &tex);
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
  const float maxExtent = std::max(1e-5f, std::max(ext.x, std::max(ext.y, ext.z)));
  const float scale = 1.2f / maxExtent;

  Shader->Use();
  Shader->SetInt("texture0", 0);
  Shader->SetInt("uAnimFrame", 0);
  Shader->SetInt("uAnimFrameCount", 1);

  const GLuint colorTex = GetOrCreateColorTexture(itemId);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, colorTex);

  glBindVertexArray(CubeVao);
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

  // Restore framebuffer state.
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         ColorTex, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);

  return tex;
}

} // namespace cutum


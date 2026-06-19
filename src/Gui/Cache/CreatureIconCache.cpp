#include "Gui/Cache/CreatureIconCache.h"
#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Visual/CreatureAppearance.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Render/Engine/ShaderManager.h"

#include "Render/Pipeline/GlStateMask.h"
#include "Render/Pipeline/GlStateScope.h"

#include "Render/GlIncludes.h"
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace cutum
{

UCreatureIconCache::UCreatureIconCache(
    std::shared_ptr<UCreatureDefinitionStorage> species,
    std::shared_ptr<USkinDefinitionStorage> skins,
    std::shared_ptr<UCreatureTextureStorage> textures,
    std::shared_ptr<UShaderManager> shader_manager)
    : Species(std::move(species)), Skins(std::move(skins)),
      Textures(std::move(textures)), ShaderManager(std::move(shader_manager))
{
}

UCreatureIconCache::~UCreatureIconCache() { Shutdown(); }

namespace
{

void UploadIconCubeMesh(GLuint &vao, GLuint &vbo, GLuint &ebo,
                        const float *texCoords)
{
  float vertices[24 * 5];
  for (int v = 0; v < 24; ++v)
  {
    vertices[v * 5 + 0] = kCreaturePartPositions[v * 3 + 0];
    vertices[v * 5 + 1] = kCreaturePartPositions[v * 3 + 1];
    vertices[v * 5 + 2] = kCreaturePartPositions[v * 3 + 2];
    vertices[v * 5 + 3] = texCoords[v * 2 + 0];
    vertices[v * 5 + 4] = texCoords[v * 2 + 1];
  }
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glGenBuffers(1, &ebo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kCreaturePartIndices),
               kCreaturePartIndices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        reinterpret_cast<void *>(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glBindVertexArray(0);
}

} // namespace

bool UCreatureIconCache::InitCubeMesh()
{
  if (CubeVao != 0)
  {
    return true;
  }
  float boxUv[48];
  float headUv[48];
  float bodyUv[48];
  float rigidHeadUv[48];
  BuildCreatureBoxTexCoords(boxUv);
  BuildCreatureHeadTexCoords(headUv);
  BuildCreatureBodyTexCoords(bodyUv);
  BuildCreatureRigidHeadTexCoords(rigidHeadUv);
  UploadIconCubeMesh(CubeVao, CubeVbo, CubeEbo, boxUv);
  UploadIconCubeMesh(HeadCubeVao, HeadCubeVbo, HeadCubeEbo, headUv);
  UploadIconCubeMesh(BodyCubeVao, BodyCubeVbo, BodyCubeEbo, bodyUv);
  UploadIconCubeMesh(RigidHeadCubeVao, RigidHeadCubeVbo, RigidHeadCubeEbo,
                     rigidHeadUv);
  return CubeVao != 0 && HeadCubeVao != 0 && BodyCubeVao != 0 &&
         RigidHeadCubeVao != 0;
}

bool UCreatureIconCache::Initialize()
{
  if (!ShaderManager)
  {
    return false;
  }
  Shader = ShaderManager->CreateShader("creature_icon", "shaders/vshader.glsl",
                                        "shaders/fshader.glsl");
  if (!Shader || !Shader->IsValid() || !InitCubeMesh())
  {
    return false;
  }

  glGenFramebuffers(1, &Fbo);
  glGenTextures(1, &ColorTex);
  glBindTexture(GL_TEXTURE_2D, ColorTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kIconSize, kIconSize, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, Fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         ColorTex, 0);

  glGenRenderbuffers(1, &DepthRbo);
  glBindRenderbuffer(GL_RENDERBUFFER, DepthRbo);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kIconSize,
                        kIconSize);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, DepthRbo);

  const bool complete =
      glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  if (Species)
  {
    WarmupQueue = Species->ListSpawnable();
    for (const std::string &Id : Species->ListAllIds())
    {
      if (const CreatureDefinition *def = Species->Get(Id))
      {
        if (def->role == CreatureRole::ControlledDefault)
        {
          WarmupQueue.push_back(Id);
        }
      }
    }
  }
  if (Skins)
  {
    for (const std::string &Id : Skins->ListEquippable())
    {
      WarmupQueue.push_back("skin:" + Id);
    }
  }
  WarmupIndex = 0;
  return complete;
}

void UCreatureIconCache::ClearRenderedIcons()
{
  for (const auto &entry : SpeciesCache)
  {
    GLuint tex = entry.second;
    if (tex == 0)
    {
      continue;
    }
    if (Textures)
    {
      const GLuint iconTex = Textures->GetTexture(entry.first + "/icon");
      if (tex == iconTex)
      {
        continue;
      }
    }
    glDeleteTextures(1, &tex);
  }
  for (const auto &entry : SkinCache)
  {
    GLuint tex = entry.second;
    if (tex == 0)
    {
      continue;
    }
    if (Textures)
    {
      const GLuint diffuse =
          Textures->GetTexture("skin/" + entry.first + "/diffuse");
      if (tex == diffuse)
      {
        continue;
      }
    }
    glDeleteTextures(1, &tex);
  }
  SpeciesCache.clear();
  SkinCache.clear();
  WarmupIndex = 0;
  if (Species)
  {
    WarmupQueue = Species->ListSpawnable();
    for (const std::string &Id : Species->ListAllIds())
    {
      if (const CreatureDefinition *def = Species->Get(Id))
      {
        if (def->role == CreatureRole::ControlledDefault)
        {
          WarmupQueue.push_back(Id);
        }
      }
    }
  }
  if (Skins)
  {
    for (const std::string &Id : Skins->ListEquippable())
    {
      WarmupQueue.push_back("skin:" + Id);
    }
  }
}

void UCreatureIconCache::Shutdown()
{
  for (const auto &entry : SpeciesCache)
  {
    if (entry.second != 0)
    {
      glDeleteTextures(1, &entry.second);
    }
  }
  for (const auto &entry : SkinCache)
  {
    if (entry.second == 0)
    {
      continue;
    }
    if (Textures)
    {
      const GLuint diffuse =
          Textures->GetTexture("skin/" + entry.first + "/diffuse");
      if (entry.second == diffuse)
      {
        continue;
      }
    }
    glDeleteTextures(1, &entry.second);
  }
  SpeciesCache.clear();
  SkinCache.clear();
  Shader.reset();
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
  if (BodyCubeEbo != 0)
  {
    glDeleteBuffers(1, &BodyCubeEbo);
    BodyCubeEbo = 0;
  }
  if (BodyCubeVbo != 0)
  {
    glDeleteBuffers(1, &BodyCubeVbo);
    BodyCubeVbo = 0;
  }
  if (BodyCubeVao != 0)
  {
    glDeleteVertexArrays(1, &BodyCubeVao);
    BodyCubeVao = 0;
  }
  if (RigidHeadCubeEbo != 0)
  {
    glDeleteBuffers(1, &RigidHeadCubeEbo);
    RigidHeadCubeEbo = 0;
  }
  if (RigidHeadCubeVbo != 0)
  {
    glDeleteBuffers(1, &RigidHeadCubeVbo);
    RigidHeadCubeVbo = 0;
  }
  if (RigidHeadCubeVao != 0)
  {
    glDeleteVertexArrays(1, &RigidHeadCubeVao);
    RigidHeadCubeVao = 0;
  }
  if (HeadCubeEbo != 0)
  {
    glDeleteBuffers(1, &HeadCubeEbo);
    HeadCubeEbo = 0;
  }
  if (HeadCubeVbo != 0)
  {
    glDeleteBuffers(1, &HeadCubeVbo);
    HeadCubeVbo = 0;
  }
  if (HeadCubeVao != 0)
  {
    glDeleteVertexArrays(1, &HeadCubeVao);
    HeadCubeVao = 0;
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
}

GLuint UCreatureIconCache::RenderSolidColorIcon(float r, float g, float b,
                                                float a)
{
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindFramebuffer(GL_FRAMEBUFFER, Fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         tex, 0);
  glViewport(0, 0, kIconSize, kIconSize);
  glClearColor(r, g, b, a);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}

GLuint UCreatureIconCache::RenderSpeciesPartsIcon(const std::string &speciesId)
{
  if (!Species || !Skins || !Textures || !Shader || Fbo == 0)
  {
    return 0;
  }
  const CreatureDefinition *def = Species->Get(speciesId);
  if (!def)
  {
    return 0;
  }

  if (Textures)
  {
    const GLuint iconTex = Textures->GetTexture(speciesId + "/icon");
    const bool useDirectIcon =
        iconTex != 0 &&
        (def->visual.iconMode == "species_texture" ||
         def->visual.iconMode == "skin_texture");
    if (useDirectIcon)
    {
      return iconTex;
    }
  }

  if (ParseCreatureVisualBackend(def->visual.backend) ==
      CreatureVisualBackend::GltfSkeleton)
  {
    return RenderSolidColorIcon(def->visual.wireframeColor.r,
                              def->visual.wireframeColor.g,
                              def->visual.wireframeColor.b, 1.0f);
  }

  const ResolvedCreatureAppearance appearance =
      ResolveCreatureAppearance(*Species, *Skins, speciesId, "");

  GLuint iconTex = 0;
  glGenTextures(1, &iconTex);
  glBindTexture(GL_TEXTURE_2D, iconTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kIconSize, kIconSize, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  UGlStateScope glState(kGlMaskIconFbo);
  glBindFramebuffer(GL_FRAMEBUFFER, Fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         iconTex, 0);
  glViewport(0, 0, kIconSize, kIconSize);
  glEnable(GL_DEPTH_TEST);
  const glm::vec4 bg = def->visual.wireframeColor * 0.25f;
  glClearColor(bg.r, bg.g, bg.b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  const float fitHeight = std::max(def->bounds.restSizeBlocks.y, 0.5f);
  const float fitScale = 1.6f / fitHeight;
  const glm::mat4 projection =
      glm::perspective(glm::radians(28.0f), 1.0f, 0.1f, 30.0f);
  const glm::mat4 view =
      glm::lookAt(glm::vec3(1.8f, 1.4f, 1.8f), glm::vec3(0.0f),
                  glm::vec3(0.0f, 1.0f, 0.0f));

  Shader->Use();
  Shader->SetInt("texture0", 0);
  Shader->SetInt("uAnimFrame", 0);
  Shader->SetInt("uAnimFrameCount", 1);
  for (const ResolvedCreaturePart &part : appearance.Parts)
  {
    const GLuint tex = Textures->GetTexture(part.textureAssetKey);
    if (tex == 0)
    {
      continue;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    GLuint partVao = CubeVao;
    if (ParseCreatureTextureLayout(appearance.textureLayout) ==
        CreatureTextureLayout::PlayerSkinAtlas)
    {
      if (part.partId == "head")
      {
        partVao = HeadCubeVao;
      }
      else if (part.partId == "torso")
      {
        partVao = BodyCubeVao;
      }
    }
    else if (UsesRigidFaceTexture(part.textureAssetKey))
    {
      partVao = RigidHeadCubeVao;
    }
    glBindVertexArray(partVao);
    const glm::vec3 local = part.offsetBlocks * fitScale;
    const glm::mat4 model =
        glm::translate(glm::mat4(1.0f), local) *
        glm::scale(glm::mat4(1.0f), part.sizeBlocks * fitScale);
    Shader->SetMat4("mvp_matrix", projection * view * model);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
  }

  glBindVertexArray(0);
  Shader->Unuse();
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return iconTex;
}

GLuint UCreatureIconCache::GetOrCreateSpeciesIcon(const std::string &speciesId)
{
  const auto it = SpeciesCache.find(speciesId);
  if (it != SpeciesCache.end())
  {
    return it->second;
  }
  GLuint tex = RenderSpeciesPartsIcon(speciesId);
  if (tex == 0)
  {
    glm::vec4 color{0.5f, 0.5f, 0.5f, 1.0f};
    if (Species)
    {
      if (const CreatureDefinition *def = Species->Get(speciesId))
      {
        color = def->visual.wireframeColor;
      }
    }
    tex = RenderSolidColorIcon(color.r, color.g, color.b, color.a);
  }
  SpeciesCache[speciesId] = tex;
  return tex;
}

GLuint UCreatureIconCache::GetOrCreateSkinIcon(const std::string &skinId)
{
  const auto it = SkinCache.find(skinId);
  if (it != SkinCache.end())
  {
    return it->second;
  }
  if (Textures)
  {
    const GLuint existing = Textures->GetTexture("skin/" + skinId + "/diffuse");
    if (existing != 0)
    {
      SkinCache[skinId] = existing;
      return existing;
    }
  }
  glm::vec4 color{0.7f, 0.7f, 0.7f, 1.0f};
  if (Skins)
  {
    if (const SkinDefinition *def = Skins->Get(skinId))
    {
      color = def->iconFallbackColor;
    }
  }
  const GLuint tex = RenderSolidColorIcon(color.r, color.g, color.b, color.a);
  SkinCache[skinId] = tex;
  return tex;
}

GLuint UCreatureIconCache::GetSpeciesIcon(const std::string &speciesId)
{
  if (speciesId.empty())
  {
    return 0;
  }
  return GetOrCreateSpeciesIcon(speciesId);
}

GLuint UCreatureIconCache::GetSkinIcon(const std::string &skinId)
{
  if (skinId.empty())
  {
    return 0;
  }
  return GetOrCreateSkinIcon(skinId);
}

void UCreatureIconCache::WarmupNext(size_t count)
{
  for (size_t i = 0; i < count && WarmupIndex < WarmupQueue.size();
       ++i, ++WarmupIndex)
  {
    const std::string &key = WarmupQueue[WarmupIndex];
    if (key.rfind("skin:", 0) == 0)
    {
      GetOrCreateSkinIcon(key.substr(5));
    }
    else
    {
      GetOrCreateSpeciesIcon(key);
    }
  }
}

} // namespace cutum

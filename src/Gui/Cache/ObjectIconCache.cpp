#include "Gui/Cache/ObjectIconCache.h"
#include "Gui/Cache/GuiOffscreenIconCacheBase.h"
#include "Gui/Preview/ObjectPreviewLayout.h"

#include "Blocks/BlockDefinitionStorage.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/Textures/TextureCube.h"
#include "World/Math/BlockTypes.h"
#include "World/Objects/ObjectLibrary.h"

#include "Render/Pipeline/GlStateMask.h"
#include "Render/Pipeline/GlStateScope.h"

#include "Render/GlIncludes.h"
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

namespace cutum
{

UObjectIconCache::UObjectIconCache(
    std::shared_ptr<UObjectLibrary> objects,
    std::shared_ptr<UTextureCubeStorage> textures,
    std::shared_ptr<UBlockDefinitionStorage> blockDefs,
    std::shared_ptr<UShaderManager> shader_manager)
    : Objects(std::move(objects)), Textures(std::move(textures)),
      BlockDefs(std::move(blockDefs)), ShaderManager(std::move(shader_manager))
{
}

UObjectIconCache::~UObjectIconCache() { Shutdown(); }

bool UObjectIconCache::Initialize()
{
  if (!ShaderManager || !Objects)
  {
    return false;
  }
  Shader = ShaderManager->CreateShader(
      "gui_prefab_icon", "shaders/vshader.glsl", "shaders/fshader.glsl");
  if (!Shader || !Shader->IsValid())
  {
    return false;
  }
  if (!InitCubeMesh())
  {
    return false;
  }

  glGenFramebuffers(1, &Fbo);
  glGenTextures(1, &ColorTex);
  glBindTexture(GL_TEXTURE_2D, ColorTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kIconSize, kIconSize, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
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

  WarmupQueue = Objects->ListNames();
  WarmupIndex = 0;
  return complete;
}

void UObjectIconCache::Shutdown()
{
  UGuiOffscreenIconCacheBase::DeleteGlTextures(Cache, ColorTex);
  UGuiOffscreenIconCacheBase::DeleteGlTextures(BlockCache, ColorTex);

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

bool UObjectIconCache::InitCubeMesh()
{
  if (CubeVao != 0)
  {
    return true;
  }
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
      0.5f,
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

GLuint UObjectIconCache::GetBlockTexture(BlockId blockId) const
{
  if (!BlockDefs || !Textures || blockId == BLOCK_AIR)
  {
    return 0;
  }
  const BlockDefinition *def = BlockDefs->GetById(blockId);
  if (!def)
  {
    return 0;
  }
  const auto &texMap = Textures->GetTextures();
  for (const auto &kv : texMap)
  {
    if (kv.second.GetName() == def->Name)
    {
      return kv.second.GetTexture();
    }
  }
  return 0;
}

void UObjectIconCache::ClearBlockIconCache()
{
  UGuiOffscreenIconCacheBase::DeleteGlTextures(BlockCache, ColorTex);
}

GLuint UObjectIconCache::GetBlockIconTexture(const std::string &blockName)
{
  if (!BlockDefs || !Textures || !Shader || blockName.empty())
  {
    return 0;
  }
  const BlockDefinition *def = BlockDefs->GetByName(blockName);
  if (!def || def->Id == BLOCK_AIR)
  {
    return 0;
  }
  const BlockId Id = def->Id;
  if (auto it = BlockCache.find(Id); it != BlockCache.end())
  {
    return it->second;
  }

  const GLuint tex = RenderBlockIcon(Id);
  if (tex != 0)
  {
    BlockCache[Id] = tex;
  }
  return tex;
}

GLuint UObjectIconCache::RenderObjectIcon(const std::string &objectName)
{
  const WorldObjectDefinition *object =
      Objects ? Objects->Get(objectName) : nullptr;
  if (!object || object->voxels.empty() || !Shader || Fbo == 0)
  {
    return 0;
  }

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
  glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  const ObjectPreviewFit fit = FitWorldObjectPreview(*object);

  const glm::mat4 projection =
      glm::perspective(glm::radians(35.0f), 1.0f, 0.1f, 50.0f);
  const glm::mat4 view =
      glm::lookAt(glm::vec3(2.2f, 2.0f, 2.2f), glm::vec3(0.0f),
                  glm::vec3(0.0f, 1.0f, 0.0f));

  Shader->Use();
  Shader->SetInt("texture0", 0);
  Shader->SetInt("uAnimFrame", 0);
  Shader->SetInt("uAnimFrameCount", 1);
  glBindVertexArray(CubeVao);

  for (const ObjectVoxel &voxel : object->voxels)
  {
    if (voxel.Id == BLOCK_AIR)
    {
      continue;
    }
    const GLuint tex = GetBlockTexture(voxel.Id);
    if (tex == 0)
    {
      continue;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);

    const glm::mat4 model = ObjectPreviewVoxelModel(fit, voxel.offset);
    const glm::mat4 mvp = projection * view * model;
    Shader->SetMat4("mvp_matrix", mvp);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
  }

  glBindVertexArray(0);
  Shader->Unuse();

  return iconTex;
}

GLuint UObjectIconCache::RenderBlockIcon(BlockId blockId)
{
  const GLuint tex = GetBlockTexture(blockId);
  if (tex == 0 || !Shader || Fbo == 0)
  {
    return 0;
  }

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
  glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glm::mat4 projection =
      glm::perspective(glm::radians(35.0f), 1.0f, 0.1f, 50.0f);
  glm::mat4 view = glm::lookAt(glm::vec3(2.2f, 2.0f, 2.2f), glm::vec3(0.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f));

  Shader->Use();
  Shader->SetInt("texture0", 0);
  Shader->SetInt("uAnimFrame", 0);
  Shader->SetInt("uAnimFrameCount", 1);
  glBindVertexArray(CubeVao);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);

  const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(0.88f));
  const glm::mat4 mvp = projection * view * model;
  Shader->SetMat4("mvp_matrix", mvp);
  glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);

  glBindVertexArray(0);
  Shader->Unuse();
  return iconTex;
}

GLuint UObjectIconCache::GetIconIfCached(const std::string &objectName) const
{
  const auto it = Cache.find(objectName);
  return it != Cache.end() ? it->second : 0;
}

GLuint UObjectIconCache::GetIcon(const std::string &objectName)
{
  if (objectName.empty())
  {
    return 0;
  }
  if (const GLuint cached = GetIconIfCached(objectName))
  {
    return cached;
  }
  const GLuint tex = RenderObjectIcon(objectName);
  Cache[objectName] = tex;
  return tex;
}

void UObjectIconCache::WarmupNext(size_t count)
{
  for (size_t n = 0; n < count && WarmupIndex < WarmupQueue.size();
       ++n, ++WarmupIndex)
  {
    GetIcon(WarmupQueue[WarmupIndex]);
  }
}

} // namespace cutum

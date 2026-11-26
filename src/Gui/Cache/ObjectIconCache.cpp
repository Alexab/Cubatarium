#include "Gui/Cache/ObjectIconCache.h"
#include "Core/Memory/StlSourceStorage.h"
#include "Gui/Cache/InventoryIconService.h"
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
#include <sstream>

namespace cutum
{

namespace
{
void BeginIconDraw(int size)
{
  glViewport(0, 0, size, size);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  const GLfloat clear[]{0.12f, 0.12f, 0.14f, 1.0f};
  glClearBufferfv(GL_COLOR, 0, clear);
  glClear(GL_DEPTH_BUFFER_BIT);
}
} // namespace

UObjectIconCache::UObjectIconCache(
    std::shared_ptr<UObjectLibrary> objects,
    std::shared_ptr<UTextureCubeStorage> textures,
    std::shared_ptr<UBlockDefinitionStorage> blockDefs,
    std::shared_ptr<UShaderManager> shader_manager,
    std::shared_ptr<UInventoryIconService> iconService)
    : Objects(std::move(objects)), Textures(std::move(textures)),
      BlockDefs(std::move(blockDefs)), ShaderManager(std::move(shader_manager)),
      IconService(std::move(iconService))
{
}

UObjectIconCache::~UObjectIconCache() { Shutdown(); }

bool UObjectIconCache::Initialize()
{
  if (!ShaderManager || !Objects)
    return false;

  UGuiIconWarmupQueue warmup;
  for (const auto &name : Objects->ListNames())
    warmup.Push(name);
  PreviewRenderTarget target;
  PreviewCubeMesh cube;
  if (!target.Initialize(kIconSize) || !cube.Initialize())
    return false;
  auto shader = ShaderManager->CreateShader(
      "gui_prefab_icon", "shaders/vshader.glsl", "shaders/fshader.glsl");
  if (!shader || !shader->IsValid())
    return false;

  Warmup.Swap(warmup);
  Target.Swap(target);
  Cube.Swap(cube);
  Shader = std::move(shader);
  return true;
}

void UObjectIconCache::Shutdown()
{
  Cache.clear();
  BlockCache.clear();
  Warmup.Clear();
  BlockWarmup.Clear();
  Target.Reset();
  Cube.Reset();
  Shader.reset();
}

ObjectIconStorage UObjectIconCache::InspectOwnedStorage(const UObjectLibrary *objects,
    const UTextureCubeStorage *textures, const UBlockDefinitionStorage *blocks,
    const UShaderManager *shaders) const noexcept
{
  ObjectIconStorage out;
  out.present = true;
  if (!objects || !shaders || Objects.get() != objects || Textures.get() != textures ||
      BlockDefs.get() != blocks || ShaderManager.get() != shaders)
    return out;
  const bool mesh = Cube.Vao && Cube.Vbo && Cube.Ebo && Cube.Vbo != Cube.Ebo;
  const bool target = Target.Framebuffer && Target.Color && Target.Depth;
  if ((!mesh && (Cube.Vao || Cube.Vbo || Cube.Ebo)) ||
      (!target && (Target.Framebuffer || Target.Color || Target.Depth)) ||
      mesh != target || (target && (!Shader || !Shader->IsValid())) || (!target && Shader))
    return out;

  StlSourceStorage objectTable, blockTable, total;
  objectTable.Hash(Cache);
  bool iconsKnown = true;
  for (const auto &entry : Cache)
  {
    objectTable.Text(entry.first);
    iconsKnown &= entry.second.Get() && entry.second.Get() != Target.Color;
  }
  blockTable.Hash(BlockCache);
  for (const auto &entry : BlockCache)
    iconsKnown &= entry.second.Get() && entry.second.Get() != Target.Color;
  const auto objectsPending = Warmup.InspectOwnedStorage();
  const auto blocksPending = BlockWarmup.InspectOwnedStorage();
  out.cacheBytes = sizeof(*this);
  out.objectTableBytes = objectTable.bytes.requested;
  out.blockTableBytes = blockTable.bytes.requested;
  out.objectQueueBytes = objectsPending.requested;
  out.blockQueueBytes = blocksPending.requested;
  for (const auto bytes : {out.cacheBytes, out.objectTableBytes, out.blockTableBytes,
                          out.objectQueueBytes, out.blockQueueBytes})
    total.Add(bytes);
  out.cpuBytes = total.bytes.requested;
  out.objectIcons = Cache.size();
  out.blockIcons = BlockCache.size();
  out.pendingObjects = Warmup.Size();
  out.pendingBlocks = BlockWarmup.Size();
  out.vertexBytes = mesh ? PreviewCubeMesh::kCubeVertexBytes : 0;
  out.indexBytes = mesh ? PreviewCubeMesh::kCubeIndexBytes : 0;
  constexpr size_t iconTexels = kIconSize * kIconSize;
  out.targetTexels = target ? iconTexels : 0;
  out.depthSamples = out.targetTexels;
  if (out.objectIcons <= SIZE_MAX / iconTexels && out.blockIcons <= SIZE_MAX / iconTexels - out.objectIcons)
    out.cachedTexels = (out.objectIcons + out.blockIcons) * iconTexels;
  else
    iconsKnown = false;
  out.vertexArray = Cube.Vao;
  out.vertexBuffer = Cube.Vbo;
  out.indexBuffer = Cube.Ebo;
  out.framebuffer = Target.Framebuffer;
  out.colorTexture = Target.Color;
  out.depthBuffer = Target.Depth;
  out.shaderPresent = bool(Shader);
  out.servicePresent = bool(IconService);
  out.bindingsMatch = true;
  out.supported = total.bytes.supported && objectTable.bytes.supported && blockTable.bytes.supported &&
                  objectsPending.supported && blocksPending.supported && iconsKnown;
  return out;
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
  BlockCache.clear();
  if (IconService)
  {
    IconService->InvalidateKind("block");
  }
}

GLuint UObjectIconCache::GetBlockIconTextureIfCached(
    const std::string &blockName) const
{
  if (!BlockDefs || blockName.empty())
  {
    return 0;
  }
  const BlockDefinition *def = BlockDefs->GetByName(blockName);
  if (!def)
  {
    return 0;
  }
  const auto it = BlockCache.find(def->Id);
  return it != BlockCache.end() ? it->second.Get() : 0;
}

void UObjectIconCache::QueueBlockIcon(const std::string &blockName)
{
  BlockWarmup.Push(blockName);
}

void UObjectIconCache::WarmupBlockIcons(size_t count)
{
  size_t done = 0;
  while (done < count)
  {
    if (!BlockWarmup.RunFront([this](const auto &name) { GetBlockIconTexture(name); }))
      break;
    ++done;
  }
  if (done > 0 && IconService)
  {
    IconService->FlushManifest();
  }
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
    return it->second.Get();
  }

  const std::string fingerprint = IconService ? BuildBlockFingerprint(blockName) : std::string{};
  if (IconService && !fingerprint.empty())
  {
    GLuint cachedTex = 0;
    if (IconService->TryLoadIconTexture("block", blockName, "", fingerprint,
                                        kIconSize, cachedTex))
    {
      return BlockCache.try_emplace(Id, UGlTextureOwner(cachedTex)).first->second.Get();
    }
  }

  UGlTextureOwner rendered(RenderBlockIcon(Id));
  if (!rendered.Get())
    return 0;
  const auto tex = BlockCache.try_emplace(Id, std::move(rendered)).first->second.Get();
  if (IconService && !fingerprint.empty())
    IconService->StoreIconTexture("block", blockName, "", fingerprint, kIconSize, tex);
  return tex;
}

GLuint UObjectIconCache::RenderObjectIcon(const std::string &objectName)
{
  const WorldObjectDefinition *object =
      Objects ? Objects->Get(objectName) : nullptr;
  if (!object || object->voxels.empty() || !Shader || Target.Framebuffer == 0)
  {
    return 0;
  }

  UGlStateScope glState(kGlMaskIconFbo | GlStateBit::DepthMask);
  GLuint iconTex = 0;
  glGenTextures(1, &iconTex);
  UGlTextureOwner texture(iconTex);
  if (!iconTex)
    return 0;
  glBindTexture(GL_TEXTURE_2D, iconTex);
  if (!AllocatePreviewColor(kIconSize))
    return 0;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  UGlIconColorAttachment attachment(Target.Framebuffer, iconTex, Target.Color);
  if (!attachment.Ready() || glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    return 0;
  BeginIconDraw(kIconSize);

  const ObjectPreviewFit fit = FitWorldObjectPreview(*object);

  const glm::mat4 projection =
      glm::perspective(glm::radians(35.0f), 1.0f, 0.1f, 50.0f);
  const glm::mat4 view =
      glm::lookAt(glm::vec3(2.2f, 2.0f, 2.2f), glm::vec3(0.0f),
                  glm::vec3(0.0f, 1.0f, 0.0f));

  Shader->Use();
  Shader->SetInt("texture0", 0);
  glBindVertexArray(Cube.Vao);

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

  return texture.Release();
}

GLuint UObjectIconCache::RenderBlockIcon(BlockId blockId)
{
  const GLuint tex = GetBlockTexture(blockId);
  if (tex == 0 || !Shader || Target.Framebuffer == 0)
  {
    return 0;
  }

  UGlStateScope glState(kGlMaskIconFbo | GlStateBit::DepthMask);
  GLuint iconTex = 0;
  glGenTextures(1, &iconTex);
  UGlTextureOwner texture(iconTex);
  if (!iconTex)
    return 0;
  glBindTexture(GL_TEXTURE_2D, iconTex);
  if (!AllocatePreviewColor(kIconSize))
    return 0;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  UGlIconColorAttachment attachment(Target.Framebuffer, iconTex, Target.Color);
  if (!attachment.Ready() || glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    return 0;
  BeginIconDraw(kIconSize);

  glm::mat4 projection =
      glm::perspective(glm::radians(35.0f), 1.0f, 0.1f, 50.0f);
  glm::mat4 view = glm::lookAt(glm::vec3(2.2f, 2.0f, 2.2f), glm::vec3(0.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f));

  Shader->Use();
  Shader->SetInt("texture0", 0);
  glBindVertexArray(Cube.Vao);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);

  const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(0.88f));
  const glm::mat4 mvp = projection * view * model;
  Shader->SetMat4("mvp_matrix", mvp);
  glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);

  glBindVertexArray(0);
  Shader->Unuse();
  return texture.Release();
}

GLuint UObjectIconCache::GetIconIfCached(const std::string &objectName) const
{
  const auto it = Cache.find(objectName);
  return it != Cache.end() ? it->second.Get() : 0;
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
  const std::string fingerprint = IconService ? BuildObjectFingerprint(objectName) : std::string{};
  if (IconService && !fingerprint.empty())
  {
    GLuint cachedTex = 0;
    if (IconService->TryLoadIconTexture("object", objectName, "", fingerprint,
                                        kIconSize, cachedTex))
    {
      return Cache.try_emplace(objectName, UGlTextureOwner(cachedTex)).first->second.Get();
    }
  }
  UGlTextureOwner rendered(RenderObjectIcon(objectName));
  if (!rendered.Get())
    return 0;
  const auto tex = Cache.try_emplace(objectName, std::move(rendered)).first->second.Get();
  if (IconService && !fingerprint.empty())
    IconService->StoreIconTexture("object", objectName, "", fingerprint, kIconSize, tex);
  return tex;
}

void UObjectIconCache::WarmupNext(size_t count)
{
  for (size_t n = 0; n < count; ++n)
  {
    if (!Warmup.RunFront([this](const auto &name) { GetIcon(name); }))
      break;
  }
  if (IconService)
  {
    IconService->FlushManifest();
  }
}

std::string UObjectIconCache::BuildObjectFingerprint(
    const std::string &objectName) const
{
  if (!Objects || objectName.empty())
  {
    return {};
  }
  const WorldObjectDefinition *def = Objects->Get(objectName);
  if (!def)
  {
    return {};
  }
  std::ostringstream out;
  out << "v2|object|" << objectName << '|' << def->voxels.size();
  for (const ObjectVoxel &voxel : def->voxels)
  {
    out << '|' << voxel.Id << ':' << voxel.offset.x << ',' << voxel.offset.y
        << ',' << voxel.offset.z;
  }
  return out.str();
}

std::string UObjectIconCache::BuildBlockFingerprint(
    const std::string &blockName) const
{
  if (!BlockDefs || blockName.empty())
  {
    return {};
  }
  const BlockDefinition *def = BlockDefs->GetByName(blockName);
  if (!def)
  {
    return {};
  }
  std::ostringstream out;
  out << "v2|block|" << blockName << '|' << def->Id << '|'
      << static_cast<int>(def->Render.Transparent) << '|'
      << static_cast<int>(def->Physics.IsLiquid);
  return out.str();
}

} // namespace cutum

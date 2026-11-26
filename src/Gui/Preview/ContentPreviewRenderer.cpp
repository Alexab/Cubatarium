#include "Gui/Preview/ContentPreviewRenderer.h"
#include "Render/Engine/RenderImageStorage.h"

#include "Blocks/BlockDefinitionStorage.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Gui/Preview/CreaturePreviewRenderer.h"
#include "Gui/Preview/ObjectPreviewLayout.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/Pipeline/GlStateMask.h"
#include "Render/Pipeline/GlStateScope.h"
#include "Render/Textures/TextureCube.h"
#include "World/Math/BlockTypes.h"
#include "World/Objects/ObjectLibrary.h"
#include "Gui/Preview/ItemPreviewRenderer.h"

#include "Render/Textures/GlTextureOwner.h"
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace cutum
{

namespace
{

constexpr float kDefaultOrbitDistance = 3.5f;
constexpr float kFovDeg = 35.0f;
constexpr int kMinTargetSize = 64;
constexpr int kMaxTargetSize = 512;

void BeginPreviewDraw(int size)
{
  glViewport(0, 0, size, size);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDisable(GL_CULL_FACE);
  const GLfloat background[]{0.12f, 0.12f, 0.14f, 1.0f};
  glClearBufferfv(GL_COLOR, 0, background);
  glClear(GL_DEPTH_BUFFER_BIT);
}

} // namespace

UContentPreviewRenderer::UContentPreviewRenderer(
    std::shared_ptr<UObjectLibrary> objects,
    std::shared_ptr<UTextureCubeStorage> textures,
    std::shared_ptr<UBlockDefinitionStorage> blockDefs,
    std::shared_ptr<UShaderManager> shader_manager,
    std::shared_ptr<UCreaturePreviewRenderer> creatures,
    std::shared_ptr<UItemPreviewRenderer> itemPreview)
    : Objects(std::move(objects)), Textures(std::move(textures)),
      BlockDefs(std::move(blockDefs)), ShaderManager(std::move(shader_manager)),
      Creatures(std::move(creatures)), ItemPreview(std::move(itemPreview))
{
}

UContentPreviewRenderer::~UContentPreviewRenderer() { Shutdown(); }

bool UContentPreviewRenderer::Initialize()
{
  if (!ShaderManager || !Objects)
  {
    return false;
  }
  auto shader = ShaderManager->CreateShader(
      "gui_prefab_icon", "shaders/vshader.glsl", "shaders/fshader.glsl");
  if (!shader || !shader->IsValid() || !Cube.Initialize())
  {
    return false;
  }
  Shader = std::move(shader);
  return true;
}

void UContentPreviewRenderer::Shutdown()
{
  Target.Reset();
  FboSize = 0;
  Cube.Reset();
  Shader.reset();
}

void UContentPreviewRenderer::InspectVisualAssets(VisualAssetCensus &census,
    const UCreaturePreviewRenderer *alreadyInspected) const
{
  if (Creatures && Creatures.get() != alreadyInspected)
    Creatures->InspectVisualAssets(census);
  if (ItemPreview)
    ItemPreview->InspectVisualAssets(census);
}

void UContentPreviewRenderer::InspectRenderImages(RenderImageCensus &out) const
{
  out.Add(RenderImageRole::ContentColor, GL_TEXTURE_2D, Target.Color);
  out.Add(RenderImageRole::ContentDepth, GL_RENDERBUFFER, Target.Depth);
  if (ItemPreview)
    ItemPreview->InspectRenderImages(out);
}

ContentPreviewRendererStorage UContentPreviewRenderer::InspectOwnedStorage(
    const UObjectLibrary *objects, const UTextureCubeStorage *textures,
    const UBlockDefinitionStorage *blocks, const UShaderManager *shaders,
    const UCreaturePreviewRenderer *creatures) const noexcept
{
  ContentPreviewRendererStorage out;
  out.present = true;
  if (!objects || !shaders || Objects.get() != objects || Textures.get() != textures ||
      BlockDefs.get() != blocks || ShaderManager.get() != shaders || Creatures.get() != creatures)
    return out;

  const bool mesh = Cube.Vao && Cube.Vbo && Cube.Ebo && Cube.Vbo != Cube.Ebo;
  const bool target = Target.Framebuffer && Target.Color && Target.Depth &&
                      FboSize >= kMinTargetSize && FboSize <= kMaxTargetSize;
  if ((!mesh && (Cube.Vao || Cube.Vbo || Cube.Ebo || Shader)) ||
      (mesh && (!Shader || !Shader->IsValid())) ||
      (!target && (Target.Framebuffer || Target.Color || Target.Depth || FboSize)))
    return out;

  out.rendererBytes = sizeof(*this);
  out.vertexBytes = mesh ? PreviewCubeMesh::kCubeVertexBytes : 0;
  out.indexBytes = mesh ? PreviewCubeMesh::kCubeIndexBytes : 0;
  out.targetSize = FboSize;
  out.colorTexels = static_cast<std::size_t>(FboSize) * FboSize;
  out.depthSamples = out.colorTexels;
  out.vertexArray = Cube.Vao;
  out.vertexBuffer = Cube.Vbo;
  out.indexBuffer = Cube.Ebo;
  out.framebuffer = Target.Framebuffer;
  out.colorTexture = Target.Color;
  out.depthBuffer = Target.Depth;
  out.shaderPresent = bool(Shader);
  out.creatureSourcePresent = bool(Creatures);
  out.itemSourcePresent = bool(ItemPreview);
  out.bindingsMatch = true;
  out.supported = true;
  return out;
}

bool UContentPreviewRenderer::SupportsKind(ContentKind kind) const
{
  return kind == ContentKind::Block || kind == ContentKind::Object ||
         kind == ContentKind::UCreature || kind == ContentKind::Skin ||
         kind == ContentKind::Item;
}

bool UContentPreviewRenderer::EnsureFboSize(int size)
{
  const int clamped = std::clamp(size, kMinTargetSize, kMaxTargetSize);
  if (Target.Framebuffer && FboSize == clamped)
    return true;
  if (!Target.Initialize(clamped))
    return false;
  FboSize = clamped;
  return true;
}

GLuint UContentPreviewRenderer::GetBlockTexture(BlockId blockId) const
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

glm::mat4 UContentPreviewRenderer::OrbitView(float yawDeg, float pitchDeg,
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

GLuint UContentPreviewRenderer::Render(ContentKind kind, const std::string &id,
                                     int size, float yawDeg, float pitchDeg)
{
  if (id.empty() || !SupportsKind(kind))
  {
    return 0;
  }
  if (kind == ContentKind::Block)
  {
    return RenderBlock(id, size, yawDeg, pitchDeg);
  }
  if (kind == ContentKind::Object)
  {
    return RenderObject(id, size, yawDeg, pitchDeg);
  }
  if (!Creatures)
  {
    return 0;
  }
  if (kind == ContentKind::UCreature)
  {
    return Creatures->Render(id, "", size, yawDeg, pitchDeg);
  }
  if (kind == ContentKind::Skin)
  {
    UCreatureDefinitionStorage *species = Creatures->GetSpecies();
    if (!species)
    {
      return 0;
    }
    return Creatures->Render(species->GetControlledDefaultSpeciesId(), id, size,
                             yawDeg, pitchDeg);
  }
  return 0;
}

GLuint UContentPreviewRenderer::RenderUnique(ContentKind kind,
                                             const std::string &id, int size,
                                             float yawDeg, float pitchDeg,
                                             float animTimeSec, bool animateWalk)
{
  if (id.empty() || !SupportsKind(kind))
  {
    return 0;
  }
  if (kind == ContentKind::UCreature)
  {
    return Creatures
               ? Creatures->RenderToUniqueTexture(id, "", size, yawDeg, pitchDeg,
                                                  animTimeSec, animateWalk)
               : 0;
  }
  if (kind == ContentKind::Skin)
  {
    if (!Creatures)
    {
      return 0;
    }
    UCreatureDefinitionStorage *species = Creatures->GetSpecies();
    if (!species)
    {
      return 0;
    }
    return Creatures->RenderToUniqueTexture(
        species->GetControlledDefaultSpeciesId(), id, size, yawDeg, pitchDeg,
        animTimeSec, animateWalk);
  }

  if (kind == ContentKind::Item)
  {
    return ItemPreview ? ItemPreview->RenderToUniqueTexture(id, size, yawDeg, pitchDeg)
                        : 0;
  }

  if (kind != ContentKind::Block && kind != ContentKind::Object)
  {
    return 0;
  }

  UGlStateScope glState(kGlMaskIconFbo | GlStateBit::DepthMask);
  if (!EnsureFboSize(size))
    return 0;

  const int clamped = FboSize;
  GLuint name = 0;
  glGenTextures(1, &name);
  UGlTextureOwner texture(name);
  if (!texture.Get())
    return 0;
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture.Get());
  AllocatePreviewColor(clamped);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  UGlIconColorAttachment attachment(Target.Framebuffer, texture.Get(), Target.Color);
  if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    return 0;

  BeginPreviewDraw(clamped);

  if (kind == ContentKind::Object)
  {
    const WorldObjectDefinition *object =
        Objects ? Objects->Get(id) : nullptr;
    if (!object || object->voxels.empty() || !Shader)
    {
      return 0;
    }
    const ObjectPreviewFit fit = FitWorldObjectPreview(*object);

    const glm::mat4 projection =
        glm::perspective(glm::radians(kFovDeg), 1.0f, 0.1f, 50.0f);
    const glm::mat4 view = OrbitView(yawDeg, pitchDeg, kDefaultOrbitDistance);

    Shader->Use();
    Shader->SetInt("texture0", 0);
    Shader->SetInt("uAnimFrame", 0);
    Shader->SetInt("uAnimFrameCount", 1);
    glBindVertexArray(Cube.Vao);

    for (const ObjectVoxel &voxel : object->voxels)
    {
      if (voxel.Id == BLOCK_AIR)
      {
        continue;
      }
      const GLuint blockTex = GetBlockTexture(voxel.Id);
      if (blockTex == 0)
      {
        continue;
      }
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, blockTex);

      const glm::mat4 model = ObjectPreviewVoxelModel(fit, voxel.offset);
      const glm::mat4 mvp = projection * view * model;
      Shader->SetMat4("mvp_matrix", mvp);
      glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
    }

    glBindVertexArray(0);
    Shader->Unuse();
  }
  else
  {
    if (!BlockDefs || id.empty() || !Shader)
    {
      return 0;
    }
    const BlockDefinition *def = BlockDefs->GetByName(id);
    if (!def || def->Id == BLOCK_AIR)
    {
      return 0;
    }
    const GLuint blockTex = GetBlockTexture(def->Id);
    if (blockTex == 0)
    {
      return 0;
    }

    const glm::mat4 projection =
        glm::perspective(glm::radians(kFovDeg), 1.0f, 0.1f, 50.0f);
    const glm::mat4 view = OrbitView(yawDeg, pitchDeg, kDefaultOrbitDistance);

    Shader->Use();
    Shader->SetInt("texture0", 0);
    Shader->SetInt("uAnimFrame", 0);
    Shader->SetInt("uAnimFrameCount", 1);
    glBindVertexArray(Cube.Vao);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, blockTex);

    const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f)) *
                            glm::scale(glm::mat4(1.0f), glm::vec3(0.88f));
    const glm::mat4 mvp = projection * view * model;
    Shader->SetMat4("mvp_matrix", mvp);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);

    glBindVertexArray(0);
    Shader->Unuse();
  }

  return texture.Release();
}

GLuint UContentPreviewRenderer::RenderObject(const std::string &objectName,
                                             int size, float yawDeg,
                                             float pitchDeg)
{
  const WorldObjectDefinition *object =
      Objects ? Objects->Get(objectName) : nullptr;
  if (!object || object->voxels.empty() || !Shader)
  {
    return 0;
  }

  UGlStateScope glState(kGlMaskIconFbo | GlStateBit::DepthMask);
  if (!EnsureFboSize(size))
    return 0;
  glBindFramebuffer(GL_FRAMEBUFFER, Target.Framebuffer);
  BeginPreviewDraw(FboSize);

  const ObjectPreviewFit fit = FitWorldObjectPreview(*object);

  const glm::mat4 projection =
      glm::perspective(glm::radians(kFovDeg), 1.0f, 0.1f, 50.0f);
  const glm::mat4 view =
      OrbitView(yawDeg, pitchDeg, kDefaultOrbitDistance);

  Shader->Use();
  Shader->SetInt("texture0", 0);
  Shader->SetInt("uAnimFrame", 0);
  Shader->SetInt("uAnimFrameCount", 1);
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
  return Target.Color;
}

GLuint UContentPreviewRenderer::RenderBlock(const std::string &blockName,
                                            int size, float yawDeg,
                                            float pitchDeg)
{
  if (!BlockDefs || blockName.empty() || !Shader)
  {
    return 0;
  }
  const BlockDefinition *def = BlockDefs->GetByName(blockName);
  if (!def || def->Id == BLOCK_AIR)
  {
    return 0;
  }
  const GLuint tex = GetBlockTexture(def->Id);
  if (tex == 0)
  {
    return 0;
  }

  UGlStateScope glState(kGlMaskIconFbo | GlStateBit::DepthMask);
  if (!EnsureFboSize(size))
    return 0;
  glBindFramebuffer(GL_FRAMEBUFFER, Target.Framebuffer);
  BeginPreviewDraw(FboSize);

  const glm::mat4 projection =
      glm::perspective(glm::radians(kFovDeg), 1.0f, 0.1f, 50.0f);
  const glm::mat4 view =
      OrbitView(yawDeg, pitchDeg, kDefaultOrbitDistance);

  Shader->Use();
  Shader->SetInt("texture0", 0);
  Shader->SetInt("uAnimFrame", 0);
  Shader->SetInt("uAnimFrameCount", 1);
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
  return Target.Color;
}

} // namespace cutum

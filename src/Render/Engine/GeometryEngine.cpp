
#include "Render/Engine/GeometryEngine.h"
#include "App/Core.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include "Creatures/Player/User.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Creatures/Visual/CreatureVisual.h"
#include "Pose/CreaturePoseParams.h"
#include "Pose/ICreaturePosePresenter.h"
#include "Render/Camera/Camera.h"
#include "Render/Camera/CameraPerspective.h"
#include "Render/Engine/DistanceFog.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/GlIncludes.h"
#include "Render/Pipeline/GlStateMask.h"
#include "Render/Pipeline/GlStateScope.h"
#include "Render/Pipeline/GreedyTransparentPipeline.h"
#include "Render/Pipeline/GreedyTransparentSort.h"
#include "Storage/ObjectImplementation.h"
#include "Storage/ObjectStorage.h"
#include "World/Math/GridMath.h"
#include "WorldGen/Features/PrefabFeatureConfig.h"
#include "WorldGen/Sampling/BiomeSampler.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>

namespace cutum
{

UGeometryEngine::UGeometryEngine(
    std::shared_ptr<UObjectStorage> object_storage,
    std::shared_ptr<UWorld> world,
    std::shared_ptr<UTextureBaseStorage> texture_base_storage,
    std::shared_ptr<UTextureCubeStorage> texture_cube_storage,
    std::shared_ptr<UTextRenderer> text_renderer)
    : ObjectStorageInstance(object_storage), WorldInstance(world),
      TextureBaseStorageInstance(texture_base_storage),
      TextureCubeStorageInstance(texture_cube_storage),
      textRenderer(text_renderer), skyColor(0.5f, 0.7f, 1.0f, 1.0f),
      BaseSkyColor(0.5f, 0.7f, 1.0f), SmoothedSkyTint(0.5f, 0.7f, 1.0f),
      useGradientSky(true)
{
}

UGeometryEngine::~UGeometryEngine()
{
  DestroyCubeBuffers();
  DestroyFaceQuadBuffers();
  DestroyGreedyMeshBuffers();
  DestroyPreviewBuffers();
  DestroyOutlineBuffers();
  DestroyCreaturePartBuffers();
  DestroyOverlayBuffers();
}

void UGeometryEngine::SetCreatureTextureStorage(
    std::shared_ptr<UCreatureTextureStorage> storage)
{
  CreatureTextureStorage = std::move(storage);
}

bool UGeometryEngine::InitEngine()
{
  // Initialize UShaderManager
  shaderManager = std::make_shared<UShaderManager>();
  if (!shaderManager->Initialize())
  {
    std::cerr << "Failed to initialize UShaderManager" << std::endl;
    return false;
  }

  if (!InitShaders())
    return false;

  // Initialize static cube buffers
  if (!InitCubeBuffers())
  {
    std::cerr << "Failed to initialize cube buffers" << std::endl;
    return false;
  }

  BlockBatchesValid = false;

  // Initialize preview buffers
  InitPreviewBuffers();

  DestroyCreaturePartBuffers();
  if (!InitCreaturePartBuffers() || !InitCreatureHeadPartBuffers() ||
      !InitCreatureBodyPartBuffers())
  {
    std::cerr << "Failed to initialize creature part buffers" << std::endl;
    return false;
  }

  if (!InitOutlineBuffers())
  {
    std::cerr << "Failed to initialize outline buffers" << std::endl;
    return false;
  }

  return true;
}

bool UGeometryEngine::InitShaders()
{
  // Create shaders through UShaderManager
  defaultShader = shaderManager->CreateShader("default", "shaders/vshader.glsl",
                                              "shaders/fshader.glsl");
  if (!defaultShader || !defaultShader->IsValid())
  {
    std::cerr << "Failed to create default shader" << std::endl;
    return false;
  }

  skyShader = shaderManager->CreateShader("sky", "shaders/vshader.glsl",
                                          "shaders/fshader_sky.glsl");
  if (!skyShader || !skyShader->IsValid())
  {
    std::cerr << "Failed to create sky shader" << std::endl;
    return false;
  }

  uiShader = shaderManager->CreateShader("ui", "shaders/vshader_2d.glsl",
                                         "shaders/fshader_2d.glsl");
  if (!uiShader || !uiShader->IsValid())
  {
    std::cerr << "Failed to create UI shader" << std::endl;
    return false;
  }

  TextShader = shaderManager->CreateShader("text", "shaders/vshader_text.glsl",
                                           "shaders/fshader_text.glsl");
  if (!TextShader || !TextShader->IsValid())
  {
    std::cerr << "Failed to create text shader" << std::endl;
    return false;
  }

  // Instanced cube shader (legacy block path — passthrough UV from cube VAO)
  instancedShader = shaderManager->CreateShader(
      "instanced", "shaders/vshader_instanced.glsl", "shaders/fshader.glsl");
  if (!instancedShader || !instancedShader->IsValid())
  {
    std::cerr
        << "Failed to create instanced shader from files, trying inline sources"
        << std::endl;
    const char *instancedVS = R"(
 #version 330 core
 layout (location = 0) in vec3 aPos;
 layout (location = 1) in vec2 aTexCoord;
 layout (location = 2) in mat4 instanceMVP;
 out vec2 TexCoord;
 void main()
 {
     gl_Position = instanceMVP * vec4(aPos, 1.0);
     TexCoord = aTexCoord;
 }
 )";
    const char *commonFS = R"(
 #version 330 core
 in vec2 TexCoord;
 out vec4 FragColor;
 uniform sampler2D texture0;
 void main()
 {
     FragColor = texture(texture0, TexCoord);
 }
 )";
    instancedShader = shaderManager->CreateShaderFromStrings(
        "instanced", instancedVS, commonFS);
    if (!instancedShader || !instancedShader->IsValid())
    {
      std::cerr << "Failed to create instanced shader" << std::endl;
      return false;
    }
  }

  instancedFaceShader = shaderManager->CreateShader(
      "instanced_face", "shaders/vshader_instanced_face.glsl",
      "shaders/fshader.glsl");
  if (!instancedFaceShader || !instancedFaceShader->IsValid())
  {
    std::cerr << "Failed to create instanced face shader from files, trying "
                 "inline sources"
              << std::endl;
    const char *instancedFaceVS = R"(
 #version 330 core
 layout (location = 0) in vec3 aPos;
 layout (location = 1) in vec2 aTexCoord;
 layout (location = 2) in mat4 instanceModel;
 layout (location = 6) in float instanceFaceIndex;
 uniform mat4 uVP;
uniform sampler2D texture0;
 out vec2 TexCoord;
 float voxelTile(float w) { return w - floor(w - 0.5) - 0.5; }
vec2 atlasHalfTexelInset() {
    ivec2 atlasSize = textureSize(texture0, 0);
    vec2 safeSize = vec2(max(atlasSize.x, 1), max(atlasSize.y, 1));
    return 0.5 / safeSize;
}
float insetMix(float a, float b, float t, float inset) {
    float span = b - a;
    float dir = sign(span);
    float safeInset = min(inset, abs(span) * 0.25);
    return mix(a + dir * safeInset, b - dir * safeInset, t);
}
 vec2 atlasUVFromWorldPos(int face, vec3 wp) {
     float cubeShift = 1.0 / 6.0;
     float u0 = float(face) * cubeShift;
     float u1 = float(face + 1) * cubeShift;
    vec2 inset = atlasHalfTexelInset();
     float tx = voxelTile(wp.x);
     float ty = voxelTile(wp.y);
     float tz = voxelTile(wp.z);
    if (face == 0) return vec2(insetMix(u0, u1, tx, inset.X), insetMix(1.0, 0.0, ty, inset.Y));
    if (face == 1) return vec2(insetMix(u0, u1, 1.0 - tz, inset.X), insetMix(1.0, 0.0, ty, inset.Y));
    if (face == 2) return vec2(insetMix(u0, u1, 1.0 - tx, inset.X), insetMix(1.0, 0.0, ty, inset.Y));
    if (face == 3) return vec2(insetMix(u0, u1, tz, inset.X), insetMix(1.0, 0.0, ty, inset.Y));
    if (face == 4) return vec2(insetMix(u0, u1, tx, inset.X), insetMix(0.0, 1.0, 1.0 - tz, inset.Y));
    return vec2(insetMix(u0, u1, tx, inset.X), insetMix(0.0, 1.0, tz, inset.Y));
 }
 void main() {
     vec4 worldPos = instanceModel * vec4(aPos, 1.0);
     gl_Position = uVP * worldPos;
     int face = int(instanceFaceIndex + 0.5);
     TexCoord = atlasUVFromWorldPos(face, worldPos.xyz);
 }
 )";
    const char *commonFS = R"(
 #version 330 core
 in vec2 TexCoord;
 out vec4 FragColor;
 uniform sampler2D texture0;
 void main()
 {
     FragColor = texture(texture0, TexCoord);
 }
 )";
    instancedFaceShader = shaderManager->CreateShaderFromStrings(
        "instanced_face", instancedFaceVS, commonFS);
    if (!instancedFaceShader || !instancedFaceShader->IsValid())
    {
      std::cerr << "Failed to create instanced face shader" << std::endl;
      return false;
    }
  }

  greedyShader = shaderManager->CreateShader(
      "greedy", "shaders/vshader_greedy.glsl", "shaders/fshader_greedy.glsl");
  if (!greedyShader || !greedyShader->IsValid())
  {
    std::cerr << "Failed to create greedy mesh shader" << std::endl;
    return false;
  }

  outlineShader = shaderManager->CreateShader("outline", "shaders/vshader.glsl",
                                              "shaders/fshader_2d.glsl");
  if (!outlineShader || !outlineShader->IsValid())
  {
    std::cerr << "Failed to create outline shader" << std::endl;
    return false;
  }

  overlayShader =
      shaderManager->CreateShader("overlay", "shaders/vshader_overlay.glsl",
                                  "shaders/fshader_overlay.glsl");
  if (!overlayShader || !overlayShader->IsValid())
  {
    std::cerr << "Failed to create overlay shader" << std::endl;
    return false;
  }

  return true;
}

void UGeometryEngine::Paint(int width_size, int height_size,
                            double view_duration)
{
  if (auto camera = WorldInstance->GetCurrentUserCamera())
  {
    AnimationClock.Tick(static_cast<float>(camera->GetDeltaTime()));
  }
  (void)view_duration;
  DrawCubeGeometry();
  if (WorldInstance)
  {
    WorldInstance->UpdateFrameHitchDiagnostics(DurationDrawSceneMks,
                                               view_duration);
  }
  if (OverlayTintAlpha > 0.01f)
  {
    RenderFluidOverlay(width_size, height_size);
  }

  // Render crosshair
  if (ShowCrosshair)
  {
    RenderCrosshair(width_size, height_size);
  }

  // Render simple text
  if (ShowHud)
  {
    RenderSimpleText(width_size, height_size);
    RenderActiveObjectPreview(width_size, height_size);
    RenderHotbarLabels(width_size, height_size);
  }

  // Disable performance UI text rendering
  if (ShowPerformance)
  {
    RenderPerformanceText(width_size, height_size, view_duration);
  }
}

void UGeometryEngine::DrawCubeGeometry()
{
  auto t_begin = std::chrono::high_resolution_clock::now();

  auto camera = WorldInstance->GetCurrentUserCamera();
  if (!camera)
  {
    return;
  }

  UGlStateScope glGuard(kGlMaskDrawCubeRestore);

  // Ensure instanced resources are ready
  if (cubeVAO == 0)
  {
    if (!InitCubeBuffers())
    {
      std::cerr << "DrawCubeGeometry: cube buffers not initialized"
                << std::endl;
      return;
    }
  }

  auto textures = TextureCubeStorageInstance->GetTextures();
  const uint64_t meshRevision = WorldInstance->GetMeshRevision();
  const bool useGreedyMesh = Render.UseFaceQuadDraw();
  const size_t renderCount =
      useGreedyMesh ? WorldInstance->GetGreedyVertexCount()
                    : WorldInstance->GetBlockRenderInstances().size();
  const bool useBatchCache = Render.BatchCache && !useGreedyMesh;

  if (useGreedyMesh)
  {
    const auto &greedyBatches = WorldInstance->GetGreedyRenderBatches();
    if (!useBatchCache || !BlockBatchesValid ||
        greedyBatches.size() != CachedInstanceCount ||
        meshRevision != CachedMeshRevision)
    {
      CachedInstanceCount = greedyBatches.size();
      CachedMeshRevision = meshRevision;
      BlockBatchesValid = true;
    }
    const glm::mat4 vp = camera->GetProjection() * camera->GetViewMatrix();
    const uint64_t cullRev = WorldInstance->GetCullRevision();
    DrawGreedyOpaqueBatches(greedyBatches, vp, textures, meshRevision, cullRev);
    GLboolean blendWasEnabled;
    glGetBooleanv(GL_BLEND, &blendWasEnabled);
    GLboolean cullWasEnabled;
    glGetBooleanv(GL_CULL_FACE, &cullWasEnabled);
    GreedyTransparentDrawContext tctx{greedyBatches,
                                      vp,
                                      meshRevision,
                                      cullRev,
                                      camera->GetPosition(),
                                      WorldInstance->GetBlockRegistry(),
                                      textures};
    UGreedyTransparentPipeline::Draw(*this, tctx);
    if (cullWasEnabled)
    {
      glEnable(GL_CULL_FACE);
    }
    else
    {
      glDisable(GL_CULL_FACE);
    }
    if (!blendWasEnabled)
    {
      glDisable(GL_BLEND);
    }
  }
  else
  {
    const auto &blockInstances = WorldInstance->GetBlockRenderInstances();
    if (!useBatchCache || !BlockBatchesValid ||
        renderCount != CachedInstanceCount ||
        meshRevision != CachedMeshRevision)
    {
      PrepareRenderBatchesFromBlocks(blockInstances, textures);
      CachedInstanceCount = renderCount;
      CachedMeshRevision = meshRevision;
      BlockBatchesValid = true;
    }
    glm::mat4 dummy_mvp = camera->GetMvpMatrix();
    RenderBatches(dummy_mvp);
  }

  RenderSelectionOutline();
  RenderBlockCrackOverlay();
  RenderCreatures();

  // Active object preview disabled to avoid per-frame resource churn

  auto t_end = std::chrono::high_resolution_clock::now();
  DurationDrawSceneMks =
      std::chrono::duration<double, std::micro>(t_end - t_begin).count();
}

void UGeometryEngine::ShowTransientMessage(const std::string &msg,
                                           double seconds)
{
  TransientMessage = msg;
  TransientMessageUntil =
      std::chrono::duration<double>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count() +
      seconds;
}

void UGeometryEngine::SetRenderSettings(const RenderSettings &settings)
{
  Render = settings;
  SetGradientSky(Render.GradientSky);
  BlockBatchesValid = false;
  DestroyGreedyGpuBatches();
  DestroyFaceQuadBuffers();
}

void UGeometryEngine::PrepareRenderBatchesFromBlocks(
    const std::vector<BlockInstance> &instances,
    const std::map<size_t, UTextureCube> &textures)
{
  renderBatches.clear();
  std::unordered_map<size_t, RenderBatch> batchMap;

  for (const auto &instance : instances)
  {
    const size_t textureId = static_cast<size_t>(instance.Id);
    auto &batch = batchMap[textureId];
    batch.blockTypeId = textureId;
    if (batch.textureID == 0)
    {
      const auto texIt = textures.find(textureId);
      if (texIt == textures.end())
      {
        continue;
      }
      batch.textureID = texIt->second.GetTextureId();
    }
    batch.ModelMatrices.push_back(instance.model);
    batch.faceIndices.push_back(static_cast<float>(instance.faceIndex));
    batch.quadSizes.push_back(instance.quadSize);
  }

  for (auto &pair : batchMap)
  {
    renderBatches.push_back(std::move(pair.second));
  }
}

void UGeometryEngine::RenderBatches(const glm::mat4 &mvp_matrix)
{
  for (const auto &batch : renderBatches)
  {
    DrawBatch(batch, mvp_matrix);
  }
}

void UGeometryEngine::DrawBatch(const RenderBatch &batch,
                                const glm::mat4 &mvp_matrix)
{
  if (batch.ModelMatrices.empty() && batch.objects.empty())
  {
    if (VerboseLogging)
      std::cout << "DrawBatch: Empty batch, skipping" << std::endl;
    return;
  }

  if (VerboseLogging)
    std::cout << "DrawBatch: Drawing " << batch.ModelMatrices.size()
              << " objects" << std::endl;

  glBindTexture(GL_TEXTURE_2D, batch.textureID);

  std::vector<glm::mat4> instanceMVPs;
  auto camera = WorldInstance->GetCurrentUserCamera();
  if (!camera)
    return;

  if (!batch.objects.empty())
  {
    instanceMVPs.reserve(batch.objects.size());
    for (size_t i = 0; i < batch.cubeIndices.size(); ++i)
    {
      auto &object = batch.objects[i];
      if (!object)
        continue;
      size_t cubeIdx = batch.cubeIndices[i];
      if (cubeIdx >= object->GetCubes().size())
        continue;
      auto &cube = object->GetCubes()[cubeIdx];
      glm::mat4 model = object->GetPose() * cube->GetInitialPose();
      glm::mat4 mvp = camera->GetProjection() * camera->GetViewMatrix() * model;
      instanceMVPs.push_back(mvp);
    }
  }
  else
  {
    instanceMVPs.reserve(batch.ModelMatrices.size());
    for (const auto &model : batch.ModelMatrices)
    {
      instanceMVPs.push_back(camera->GetProjection() * camera->GetViewMatrix() *
                             model);
    }
  }

  const bool isBlockBatch =
      batch.objects.empty() && !batch.ModelMatrices.empty();
  const bool drawFaceQuads =
      isBlockBatch && Render.UseFaceQuadDraw() &&
      batch.faceIndices.size() == batch.ModelMatrices.size();
  const GLsizei indexCount = drawFaceQuads ? 6 : 36;
  GLuint vao = drawFaceQuads ? faceVAO : cubeVAO;

  std::shared_ptr<UShaderProgram> activeShader = instancedShader;
  if (drawFaceQuads)
  {
    if (faceVAO == 0 && !InitFaceQuadBuffers())
    {
      return;
    }
    vao = faceVAO;
    activeShader = instancedFaceShader;
  }
  else if (!instancedShader)
  {
    return;
  }

  if (!activeShader || !activeShader->IsValid())
  {
    return;
  }

  activeShader->Use();
  activeShader->SetInt("texture0", 0);
  if (drawFaceQuads && batch.blockTypeId != 0 && TextureCubeStorageInstance)
  {
    SetBlockAnimUniforms(activeShader, static_cast<BlockId>(batch.blockTypeId),
                         TextureCubeStorageInstance->GetTextures());
  }

  if (drawFaceQuads)
  {
    struct BlockDrawInstance
    {
      glm::mat4 model;
      float faceIndex;
      float pad[3];
    };
    std::vector<BlockDrawInstance> blockInstances;
    blockInstances.reserve(batch.ModelMatrices.size());
    const glm::mat4 vp = camera->GetProjection() * camera->GetViewMatrix();
    activeShader->SetMat4("uVP", vp);
    for (size_t i = 0; i < batch.ModelMatrices.size(); ++i)
    {
      BlockDrawInstance inst;
      inst.model = batch.ModelMatrices[i];
      inst.faceIndex = batch.faceIndices[i];
      inst.pad[0] = inst.pad[1] = inst.pad[2] = 0.0f;
      blockInstances.push_back(inst);
    }
    glBindBuffer(GL_ARRAY_BUFFER, instanceBlockVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 blockInstances.size() * sizeof(BlockDrawInstance),
                 blockInstances.data(), GL_DYNAMIC_DRAW);
  }
  else
  {
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
    glBufferData(GL_ARRAY_BUFFER, instanceMVPs.size() * sizeof(glm::mat4),
                 instanceMVPs.data(), GL_DYNAMIC_DRAW);
  }

  glBindVertexArray(vao);
  const GLsizei instanceCount =
      drawFaceQuads ? static_cast<GLsizei>(batch.ModelMatrices.size())
                    : static_cast<GLsizei>(instanceMVPs.size());
  if (instanceCount > 0)
  {
    glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0,
                            instanceCount);
  }
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  activeShader->Unuse();
}

void UGeometryEngine::DestroyGreedyGpuPassCache(GreedyGpuPassCache &cache)
{
  for (GreedyGpuBatch &batch : cache.batches)
  {
    if (batch.ebo)
    {
      glDeleteBuffers(1, &batch.ebo);
      batch.ebo = 0;
    }
    if (batch.vbo)
    {
      glDeleteBuffers(1, &batch.vbo);
      batch.vbo = 0;
    }
  }
  cache.batches.clear();
  cache.meshRevision = 0;
  cache.cullRevision = 0;
  cache.sortRevision = 0;
}

void UGeometryEngine::DestroyGreedyGpuBatches()
{
  DestroyGreedyGpuPassCache(GreedyGpuOpaque);
  DestroyGreedyGpuPassCache(GreedyGpuCutout);
  DestroyGreedyGpuPassCache(GreedyGpuTransparent);
}

void UGeometryEngine::RefreshGreedyGpuBatches(
    const std::vector<GreedyMeshBatch> &batches, uint64_t meshRevision,
    uint64_t cullRevision, GreedyGpuPassCache &cache, uint64_t sortRevision)
{
  if (meshRevision == cache.meshRevision &&
      cullRevision == cache.cullRevision && sortRevision == cache.sortRevision)
  {
    return;
  }

  DestroyGreedyGpuPassCache(cache);
  cache.batches.reserve(batches.size());

  for (const GreedyMeshBatch &batch : batches)
  {
    if (batch.vertices.empty() || batch.indices.empty())
    {
      continue;
    }
    GreedyGpuBatch gpu;
    gpu.blockId = batch.blockId;
    gpu.vertexCount = batch.vertices.size();
    gpu.indexCount = batch.indices.size();
    gpu.indexCountGl = static_cast<GLsizei>(batch.indices.size());
    glGenBuffers(1, &gpu.vbo);
    glGenBuffers(1, &gpu.ebo);
    glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 batch.vertices.size() * sizeof(GreedyMeshVertex),
                 batch.vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 batch.indices.size() * sizeof(uint32_t), batch.indices.data(),
                 GL_STATIC_DRAW);
    cache.batches.push_back(gpu);
  }

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  cache.meshRevision = meshRevision;
  cache.cullRevision = cullRevision;
  cache.sortRevision = sortRevision;
}

void UGeometryEngine::SetBlockAnimUniforms(
    const std::shared_ptr<UShaderProgram> &shader, BlockId blockId,
    const std::map<size_t, UTextureCube> &textures)
{
  int FrameCount = 1;
  const auto texIt = textures.find(static_cast<size_t>(blockId));
  if (texIt != textures.end())
  {
    FrameCount = static_cast<int>(texIt->second.GetNumTextureFrames());
  }
  if (FrameCount < 1)
  {
    FrameCount = 1;
  }

  int frame = 0;
  if (FrameCount > 1 && WorldInstance)
  {
    const BlockAnimationSpec &anim =
        WorldInstance->GetBlockRegistry().Animation(blockId);
    const int frametimeTicks = std::max(1, anim.FrametimeTicks);
    const float frameDuration = static_cast<float>(frametimeTicks) / 20.0f;
    if (frameDuration > 0.0f)
    {
      const float elapsed = AnimationClock.ElapsedSeconds();
      frame = static_cast<int>(elapsed / frameDuration) % FrameCount;
      if (frame < 0)
      {
        frame = 0;
      }
    }
  }

  shader->SetInt("uAnimFrame", frame);
  shader->SetInt("uAnimFrameCount", FrameCount);
}

void UGeometryEngine::PrepareFrameRendering()
{
  auto camera = WorldInstance->GetCurrentUserCamera();
  if (!camera)
  {
    return;
  }
  const UWorld::SampledFluidState fluid = WorldInstance->SampleFluidPhysics(
      camera->GetPosition(), camera->GetPlayerCapsule());
  BlockId eyeFluid = BLOCK_AIR;
  const bool cameraInFluid =
      WorldInstance->IsCameraInsideFluid(camera->GetPosition(), &eyeFluid);

  glm::vec3 targetSky = BaseSkyColor;
  FogEnabled = 0.0f;
  FogHorizontal = 0.0f;
  FogHorizonBlend = 0.0f;
  OverlayTintAlpha = 0.0f;
  OverlayBlockId = BLOCK_AIR;

  const UBlockRegistry &registry = WorldInstance->GetBlockRegistry();
  if (cameraInFluid)
  {
    if (const FluidViewProfile *fv = registry.GetFluidView(eyeFluid))
    {
      if (registry.GetRenderStyle(eyeFluid) == BlockRenderStyle::Fluid)
      {
        FogEnabled = 1.0f;
        FogStart = fv->FogStart;
        FogEnd = fv->FogEnd;
        FogMinBlend = fv->FogMinBlend;
        SmoothedFogColor = glm::mix(SmoothedFogColor, fv->FogColor, 0.15f);
        targetSky = fv->FogColor;
      }
    }
  }
  else if (Render.DistanceFog)
  {
    const DistanceFogParams distance_fog = ComputeDistanceFog(
        WorldInstance->GetEffectiveRenderDistance(), SmoothedSkyTint,
        Render.DistanceFogStartRatio,
        WorldInstance->GetEffectiveFogStartRatio(), Render.DistanceFogDensity);
    FogEnabled = 1.0f;
    FogStart = distance_fog.Start;
    FogEnd = distance_fog.End;
    FogDensity = distance_fog.Density;
    FogMinBlend = 0.0f;
    FogHorizontal = Render.DistanceFogHorizontal ? 1.0f : 0.0f;
    FogHorizonBlend = 1.0f;
    SmoothedFogColor = glm::mix(SmoothedFogColor, distance_fog.Color, 0.15f);
  }
  if (fluid.inFluid)
  {
    if (const FluidViewProfile *fv = registry.GetFluidView(fluid.dominantFluid))
    {
      if (fv->OverlayAlpha > 0.01f &&
          registry.GetRenderStyle(fluid.dominantFluid) ==
              BlockRenderStyle::Cross)
      {
        OverlayTintAlpha = fv->OverlayAlpha;
        OverlayTintColor = fv->OverlayColor;
        OverlayBlockId = fluid.dominantFluid;
      }
    }
  }

  SmoothedSkyTint = glm::mix(SmoothedSkyTint, targetSky, 0.15f);
  skyColor = glm::vec4(SmoothedSkyTint, 1.0f);
}

void UGeometryEngine::ApplyFogUniforms(
    const std::shared_ptr<UShaderProgram> &shader, const glm::vec3 &cameraPos)
{
  shader->SetVec3("uCameraPos", cameraPos);
  shader->SetVec3("uFogColor", SmoothedFogColor);
  shader->SetFloat("uFogStart", FogStart);
  shader->SetFloat("uFogEnd", FogEnd);
  shader->SetFloat("uFogMinBlend", FogMinBlend);
  shader->SetFloat("uFogEnabled", FogEnabled);
  shader->SetFloat("uFogHorizontal", FogHorizontal);
  shader->SetFloat("uFogDensity", FogDensity);
}

void UGeometryEngine::SetGreedyShaderMode(
    const std::shared_ptr<UShaderProgram> &shader, bool alphaCutout,
    bool transparentPass, GreedyShaderMode mode, float shellAlphaThreshold)
{
  shader->SetInt("uAlphaCutout", alphaCutout ? 1 : 0);
  if (transparentPass)
  {
    shader->SetInt("uGreedyShaderMode", GreedyShaderModeToUniform(mode));
    shader->SetFloat("uShellAlphaThreshold", shellAlphaThreshold);
  }
  else
  {
    shader->SetInt("uGreedyShaderMode", 0);
    shader->SetFloat("uShellAlphaThreshold", 0.0f);
  }
}

void UGeometryEngine::DrawGreedyGpuBatches(
    const GreedyGpuPassCache &cache, const glm::mat4 &vp,
    const std::map<size_t, UTextureCube> &textures, bool alphaCutout,
    bool transparentPass, GreedyShaderMode mode, float shellAlphaThreshold)
{
  if (cache.batches.empty())
  {
    return;
  }
  if (greedyMeshVAO == 0 && !InitGreedyMeshBuffers())
  {
    return;
  }
  if (!greedyShader || !greedyShader->IsValid())
  {
    return;
  }

  greedyShader->Use();
  greedyShader->SetMat4("mvp_matrix", vp);
  greedyShader->SetInt("texture0", 0);
  SetGreedyShaderMode(greedyShader, alphaCutout, transparentPass, mode,
                      shellAlphaThreshold);
  if (auto camera = WorldInstance->GetCurrentUserCamera())
  {
    ApplyFogUniforms(greedyShader, camera->GetPosition());
  }
  glActiveTexture(GL_TEXTURE0);

  glBindVertexArray(greedyMeshVAO);
  const GLsizei kStride = static_cast<GLsizei>(sizeof(GreedyMeshVertex));
  for (const GreedyGpuBatch &gpu : cache.batches)
  {
    SetBlockAnimUniforms(greedyShader, gpu.blockId, textures);
    if (gpu.indexCountGl <= 0 || gpu.vbo == 0 || gpu.ebo == 0)
    {
      continue;
    }
    const auto texIt = textures.find(static_cast<size_t>(gpu.blockId));
    if (texIt == textures.end())
    {
      continue;
    }
    const GLuint textureId = texIt->second.GetTextureId();
    if (textureId == 0)
    {
      continue;
    }
    glBindTexture(GL_TEXTURE_2D, textureId);
    glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ebo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride, (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, kStride,
                          (void *)(offsetof(GreedyMeshVertex, faceIndex)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, kStride,
                          (void *)(offsetof(GreedyMeshVertex, u)));
    glEnableVertexAttribArray(2);
    glDrawElements(GL_TRIANGLES, gpu.indexCountGl, GL_UNSIGNED_INT, nullptr);
  }

  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  greedyShader->Unuse();
}

void UGeometryEngine::WarmupGreedyGpuFromWorld()
{
  if (!WorldInstance || !TextureCubeStorageInstance ||
      !Render.UseFaceQuadDraw())
  {
    return;
  }
  auto camera = WorldInstance->GetCurrentUserCamera();
  if (!camera)
  {
    return;
  }

  const auto &greedyBatches = WorldInstance->GetGreedyRenderBatches();
  const uint64_t meshRevision = WorldInstance->GetMeshRevision();
  const uint64_t cullRev = WorldInstance->GetCullRevision();
  const glm::mat4 vp = camera->GetProjection() * camera->GetViewMatrix();
  const auto textures = TextureCubeStorageInstance->GetTextures();

  DrawGreedyOpaqueBatches(greedyBatches, vp, textures, meshRevision, cullRev);

  GreedyTransparentDrawContext tctx{greedyBatches,
                                    vp,
                                    meshRevision,
                                    cullRev,
                                    camera->GetPosition(),
                                    WorldInstance->GetBlockRegistry(),
                                    textures};
  PrepareTransparent(tctx);

  CachedInstanceCount = greedyBatches.size();
  CachedMeshRevision = meshRevision;
  BlockBatchesValid = true;
}

void UGeometryEngine::DrawGreedyOpaqueBatches(
    const std::vector<GreedyMeshBatch> &batches, const glm::mat4 &vp,
    const std::map<size_t, UTextureCube> &textures, uint64_t meshRevision,
    uint64_t cullRevision)
{
  std::vector<GreedyMeshBatch> solid;
  std::vector<GreedyMeshBatch> cutout;
  solid.reserve(batches.size());
  cutout.reserve(batches.size());
  for (const GreedyMeshBatch &batch : batches)
  {
    if (batch.Transparent)
    {
      continue;
    }
    if (batch.AlphaCutout)
    {
      cutout.push_back(batch);
    }
    else
    {
      solid.push_back(batch);
    }
  }
  if (!solid.empty())
  {
    RefreshGreedyGpuBatches(solid, meshRevision, cullRevision, GreedyGpuOpaque,
                            0);
    DrawGreedyGpuBatches(GreedyGpuOpaque, vp, textures, false, false,
                         GreedyShaderMode::TransparentColor, 0.0f);
  }
  if (!cutout.empty())
  {
    GLboolean cullWasEnabled;
    glGetBooleanv(GL_CULL_FACE, &cullWasEnabled);
    glDisable(GL_CULL_FACE);
    RefreshGreedyGpuBatches(cutout, meshRevision, cullRevision, GreedyGpuCutout,
                            0);
    DrawGreedyGpuBatches(GreedyGpuCutout, vp, textures, true, false,
                         GreedyShaderMode::TransparentColor, 0.0f);
    if (cullWasEnabled)
    {
      glEnable(GL_CULL_FACE);
    }
  }
}

void UGeometryEngine::PrepareTransparent(
    const GreedyTransparentDrawContext &ctx)
{
  std::vector<GreedyMeshBatch> filtered;
  filtered.reserve(ctx.allBatches.size());
  for (const GreedyMeshBatch &batch : ctx.allBatches)
  {
    if (!batch.Transparent)
    {
      continue;
    }
    if (ctx.blockRegistry.GetRenderStyle(batch.blockId) ==
        BlockRenderStyle::Cross)
    {
      continue;
    }
    filtered.push_back(batch);
  }
  if (filtered.empty())
  {
    GreedyGpuTransparent.batches.clear();
    PreparedTransparentTextures = nullptr;
    return;
  }
  SortTransparentGreedyBatches(filtered, ctx.cameraPos, ctx.blockRegistry);
  const uint64_t sortRevision = GreedyTransparentSortRevision(ctx.cameraPos);
  RefreshGreedyGpuBatches(filtered, ctx.meshRevision, ctx.cullRevision,
                          GreedyGpuTransparent, sortRevision);
  PreparedTransparentVp = ctx.viewProjection;
  PreparedTransparentTextures = &ctx.textures;
}

void UGeometryEngine::DrawPreparedTransparent(GreedyShaderMode mode,
                                              float shellAlpha)
{
  if (!PreparedTransparentTextures || GreedyGpuTransparent.batches.empty())
  {
    return;
  }
  DrawGreedyGpuBatches(GreedyGpuTransparent, PreparedTransparentVp,
                       *PreparedTransparentTextures, true, true, mode,
                       shellAlpha);
}

bool UGeometryEngine::InitGreedyMeshBuffers()
{
  if (greedyMeshVAO != 0)
  {
    return true;
  }

  glGenVertexArrays(1, &greedyMeshVAO);
  glGenBuffers(1, &greedyMeshVBO);
  glGenBuffers(1, &greedyMeshEBO);

  glBindVertexArray(greedyMeshVAO);
  glBindBuffer(GL_ARRAY_BUFFER, greedyMeshVBO);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, greedyMeshEBO);

  constexpr GLsizei kStride = static_cast<GLsizei>(sizeof(GreedyMeshVertex));
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride, (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, kStride,
                        (void *)(offsetof(GreedyMeshVertex, faceIndex)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, kStride,
                        (void *)(offsetof(GreedyMeshVertex, u)));
  glEnableVertexAttribArray(2);

  glBindVertexArray(0);
  return greedyMeshVAO != 0;
}

void UGeometryEngine::DestroyGreedyMeshBuffers()
{
  DestroyGreedyGpuBatches();
  if (greedyMeshEBO)
  {
    glDeleteBuffers(1, &greedyMeshEBO);
    greedyMeshEBO = 0;
  }
  if (greedyMeshVBO)
  {
    glDeleteBuffers(1, &greedyMeshVBO);
    greedyMeshVBO = 0;
  }
  if (greedyMeshVAO)
  {
    glDeleteVertexArrays(1, &greedyMeshVAO);
    greedyMeshVAO = 0;
  }
}

void UGeometryEngine::DrawCube(std::shared_ptr<UCube> icube, GLuint texture)
{
  auto cube = std::dynamic_pointer_cast<UCubeGL>(icube);
  if (!cube)
  {
    std::cout << "DrawCube: Failed to cast to UCubeGL" << std::endl;
    return;
  }

  // debug removed

  glBindTexture(GL_TEXTURE_2D, texture);
  defaultShader->Use();
  defaultShader->SetInt("texture0", 0);

  // Tell OpenGL which VBOs to use
  if (cubeDrawVAO == 0)
  {
    glGenVertexArrays(1, &cubeDrawVAO);
  }
  glBindVertexArray(cubeDrawVAO);
  glBindBuffer(GL_ARRAY_BUFFER, cube->GetArrayBuf());
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cube->GetIndexBuf());

  // Offset for position
  size_t offset = 0;

  // Tell OpenGL programmable pipeline how to locate vertex position data
  int vertexLocation =
      glGetAttribLocation(defaultShader->GetProgramID(), "aPos");
  if (vertexLocation >= 0)
  {
    glEnableVertexAttribArray(vertexLocation);
    glVertexAttribPointer(vertexLocation, 3, GL_FLOAT, GL_FALSE,
                          sizeof(VertexData), (void *)offset);
  }
  else
  {
    defaultShader->Unuse();
    glBindVertexArray(0);
    return;
  }

  // Offset for texture coordinate
  offset += sizeof(glm::vec3);

  // Tell OpenGL programmable pipeline how to locate vertex texture coordinate
  // data
  int texcoordLocation =
      glGetAttribLocation(defaultShader->GetProgramID(), "aTexCoord");
  if (texcoordLocation >= 0)
  {
    glEnableVertexAttribArray(texcoordLocation);
    glVertexAttribPointer(texcoordLocation, 2, GL_FLOAT, GL_FALSE,
                          sizeof(VertexData), (void *)offset);
  }
  else
  {
    // still draw without UVs would be pointless; abort safe
    defaultShader->Unuse();
    glBindVertexArray(0);
    return;
  }

  // Draw cube geometry using indices from VBO 1
  glDrawElements(
      GL_TRIANGLE_STRIP,
      int(std::dynamic_pointer_cast<UCubeGL>(cube)->GetIndices().size()),
      GL_UNSIGNED_SHORT, nullptr);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glBindVertexArray(0);

  defaultShader->Unuse();
}

void UGeometryEngine::DrawObject(std::shared_ptr<UObject> object,
                                 const std::map<size_t, UTextureCube> &textures)
{
  for (size_t i = 0; i < object->GetCubes().size(); i++)
  {
    auto &cube = object->GetCubes()[i];
    GLuint texture = textures.at(cube->GetTypeId()).GetTextureId();
    DrawCube(cube, texture);
  }
}

void UGeometryEngine::DrawSkyGradient()
{
  // Use simple version which is more reliable
  DrawSkyGradientSimple();
}

void UGeometryEngine::DrawSkyGradientSimple()
{
  // Check that sky shader is ready
  if (!skyShader->IsValid())
  {
    std::cerr << "Sky shader is not linked!" << std::endl;
    return;
  }

  // Temporarily disable depth test for sky
  glDisable(GL_DEPTH_TEST);

  // Use sky shader
  skyShader->Use();

  // Set identity matrix for sky
  glm::mat4 skyMatrix = glm::mat4(1.0f);
  skyShader->SetMat4("mvp_matrix", skyMatrix);

  // Pass sky color to shader
  skyShader->SetVec4("skyColor", skyColor);
  skyShader->SetVec3("uFogColor", SmoothedFogColor);
  skyShader->SetFloat("uFogHorizonBlend", FogHorizonBlend);

  // Create simple rectangle for sky (full screen)
  static const GLfloat skyVertices[] = {
      // Positions      // Texture coordinates
      -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,  -1.0f, 0.0f, 1.0f, 0.0f,
      1.0f,  1.0f,  0.0f, 1.0f, 1.0f, -1.0f, 1.0f,  0.0f, 0.0f, 1.0f};

  // Create temporary VBO for rendering
  GLuint tempVBO;
  glGenBuffers(1, &tempVBO);
  glBindBuffer(GL_ARRAY_BUFFER, tempVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(skyVertices), skyVertices,
               GL_STATIC_DRAW);

  // Set attributes
  int vertexLocation =
      glGetAttribLocation(skyShader->GetProgramID(), "a_position");
  if (vertexLocation != -1)
  {
    glEnableVertexAttribArray(vertexLocation);
    glVertexAttribPointer(vertexLocation, 3, GL_FLOAT, GL_FALSE,
                          5 * sizeof(GLfloat), (void *)0);
  }

  int texcoordLocation =
      glGetAttribLocation(skyShader->GetProgramID(), "a_texcoord");
  if (texcoordLocation != -1)
  {
    glEnableVertexAttribArray(texcoordLocation);
    glVertexAttribPointer(texcoordLocation, 2, GL_FLOAT, GL_FALSE,
                          5 * sizeof(GLfloat), (void *)(3 * sizeof(GLfloat)));
  }

  // Render sky as triangles
  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

  // Check OpenGL errors
  GLenum error = glGetError();
  if (error != GL_NO_ERROR)
  {
    std::cerr << "OpenGL error after drawing sky: " << error << std::endl;
  }

  // Free resources
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glDeleteBuffers(1, &tempVBO);
  skyShader->Unuse();

  // Enable depth test back
  glEnable(GL_DEPTH_TEST);
}

bool UGeometryEngine::InitOverlayBuffers()
{
  if (overlayVAO != 0)
  {
    return true;
  }
  const float quad[] = {
      -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,  -1.0f, 1.0f, 0.0f,
      1.0f,  1.0f,  1.0f, 1.0f, -1.0f, 1.0f,  0.0f, 1.0f,
  };
  const unsigned int indices[] = {0, 1, 2, 0, 2, 3};
  GLuint ebo = 0;
  glGenVertexArrays(1, &overlayVAO);
  glGenBuffers(1, &overlayVBO);
  glGenBuffers(1, &ebo);
  glBindVertexArray(overlayVAO);
  glBindBuffer(GL_ARRAY_BUFFER, overlayVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
               GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        (void *)(2 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glBindVertexArray(0);
  return true;
}

void UGeometryEngine::DestroyOverlayBuffers()
{
  if (overlayVBO)
  {
    glDeleteBuffers(1, &overlayVBO);
    overlayVBO = 0;
  }
  if (overlayVAO)
  {
    glDeleteVertexArrays(1, &overlayVAO);
    overlayVAO = 0;
  }
}

void UGeometryEngine::RenderFluidOverlay(int width, int height)
{
  (void)width;
  (void)height;
  if (!overlayShader || OverlayBlockId == BLOCK_AIR ||
      !TextureCubeStorageInstance)
  {
    return;
  }
  if (!InitOverlayBuffers())
  {
    return;
  }
  const auto textures = TextureCubeStorageInstance->GetTextures();
  const auto texIt = textures.find(static_cast<size_t>(OverlayBlockId));
  if (texIt == textures.end() || texIt->second.GetTextureId() == 0)
  {
    return;
  }

  UGlStateScope glGuard(kGlMaskOverlay2D);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  overlayShader->Use();
  overlayShader->SetInt("texture0", 0);
  SetBlockAnimUniforms(overlayShader, OverlayBlockId, textures);
  overlayShader->SetVec3("uTintColor", OverlayTintColor);
  overlayShader->SetFloat("uTintAlpha", OverlayTintAlpha);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texIt->second.GetTextureId());
  glBindVertexArray(overlayVAO);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
  overlayShader->Unuse();
}

// Methods for sky color management
void UGeometryEngine::SetSkyColor(float r, float g, float b, float a)
{
  BaseSkyColor = glm::vec3(r, g, b);
  SmoothedSkyTint = BaseSkyColor;
  skyColor = glm::vec4(r, g, b, a);
}

void UGeometryEngine::SetSkyColor(const glm::vec4 &color)
{
  BaseSkyColor = glm::vec3(color);
  SmoothedSkyTint = BaseSkyColor;
  skyColor = color;
}

glm::vec4 UGeometryEngine::GetSkyColor() const { return skyColor; }

void UGeometryEngine::SetGradientSky(bool useGradient)
{
  useGradientSky = useGradient;
}

bool UGeometryEngine::IsGradientSky() const { return useGradientSky; }

void UGeometryEngine::SetOverlayMargins(int right, int top)
{
  OverlayMarginRight = std::max(10, right);
  OverlayMarginTop = std::max(30, top);
}

void UGeometryEngine::RenderPerformanceText(int width_size, int height_size,
                                            double view_duration)
{
  if (!textRenderer)
  {
    return;
  }

  // Обновляем размеры окна в TextRenderer
  textRenderer->SetWindowSize(width_size, height_size);

  float scale = 0.7f;
  glm::vec3 textColor(1.0f, 1.0f, 0.0f); // Yellow color for performance

  // Calculate FPS
  double totalTime = DurationDrawSceneMks +
                     WorldInstance->GetDurationDoMovementMks() + view_duration;
  double fps = totalTime > 0 ? 1000000.0 / totalTime : 0.0;

  size_t blockCount = WorldInstance->GetCachedBlockCount();
  size_t drawCount = WorldInstance->GetRenderInstanceCount();

  // Form performance information strings
  std::vector<std::string> performanceLines = {
      "Performance:",
      "FPS: " + std::to_string(fps).substr(0, 6),
      "Blocks: " + std::to_string(blockCount) +
          " draw: " + std::to_string(drawCount),
      "Scene: " + std::to_string(DurationDrawSceneMks / 1000.0).substr(0, 6) +
          " ms",
      "Movement: " +
          std::to_string(WorldInstance->GetDurationDoMovementMks() / 1000.0)
              .substr(0, 6) +
          " ms",
      "View: " + std::to_string(view_duration / 1000.0).substr(0, 6) + " ms"};

  const auto &md = WorldInstance->GetMovementDiagnostics();
  performanceLines.push_back(
      "Flat: " + std::to_string(md.flatRebuildMs).substr(0, 5) + "ms" +
      " rebuilt: " + std::to_string(md.meshRebuildsThisFrame) +
      " hitch: " + (md.hitchDetected ? "yes" : "no"));
  if (md.streamingGenMs > 0.01 || md.meshRebuildMs > 0.01 ||
      md.streamingIoMs > 0.01)
  {
    performanceLines.push_back(
        "Gen: " + std::to_string(md.streamingGenMs).substr(0, 5) + " ms" +
        " Mesh: " + std::to_string(md.meshRebuildMs).substr(0, 5) + " ms" +
        " IO: " + std::to_string(md.streamingIoMs).substr(0, 5) + " ms");
    performanceLines.push_back(
        "Dirty: " + std::to_string(md.dirtyChunksPending) +
        " rebuilt: " + std::to_string(md.meshRebuildsThisFrame));
    performanceLines.push_back(
        "Flat: " + std::to_string(md.flatRebuildMs).substr(0, 5) + "ms" +
        " Cache: " + std::to_string(md.greedyCacheEntries) +
        " Dirty: " + std::to_string(md.dirtyChunksPending));
  }
  {
    float temperature = 0.0f;
    float moisture = 0.0f;
    const ProceduralSettings settings = WorldInstance->GetProceduralSettings();
    ComputeBiomeClimate(md.feetBlock.x, md.feetBlock.z, settings.Seed,
                        temperature, moisture);
    const float localHeightNorm =
        std::clamp(static_cast<float>(md.feetBlock.y - settings.SeaLevel) /
                       static_cast<float>(
                           std::max(1, settings.MaxHeight - settings.SeaLevel)),
                   0.0f, 1.0f);
    const BiomeId biome = ClassifyBiome(temperature, moisture, localHeightNorm);
    performanceLines.push_back(
        std::string("Biome: ") + BiomeIdToString(biome) +
        " T:" + std::to_string(temperature).substr(0, 4) +
        " M:" + std::to_string(moisture).substr(0, 4));
  }
  if (md.fallThroughSuspected || md.feetInUnloadList)
  {
    performanceLines.push_back(
        "Dt: " + std::to_string(md.deltaTime).substr(0, 5) +
        " yDrop: " + std::to_string(md.playerYDrop).substr(0, 5));
    performanceLines.push_back(
        std::string("Feet chunk: ") + (md.feetChunkLoaded ? "OK" : "MISSING") +
        (md.feetIsAir ? " AIR" : " SOLID") +
        " unloads: " + std::to_string(md.streamingUnloads));
    if (md.fallThroughSuspected)
    {
      performanceLines.emplace_back("!! fall-through suspected !!");
    }
  }

  // Display text in top right corner
  float y =
      static_cast<float>(height_size) - static_cast<float>(OverlayMarginTop);
  for (const auto &line : performanceLines)
  {
    // Calculate position for right alignment
    glm::vec2 textSize = textRenderer->GetTextSize(line, scale);
    float x = static_cast<float>(width_size) - textSize.x -
              static_cast<float>(OverlayMarginRight);

    textRenderer->RenderText(line, x, y, scale, textColor);
    y -= 18.0f; // Margin between lines
  }
}

void UGeometryEngine::RenderCrosshair(int width_size, int height_size)
{
  if (!uiShader || !uiShader->IsValid())
  {
    std::cerr << "UI shader is not valid" << std::endl;
    return;
  }

  UGlStateScope glGuard(kGlMaskOverlay2D);
  glDisable(GL_DEPTH_TEST);

  // Используем UI шейдер
  uiShader->Use();

  // Set screen size
  uiShader->SetVec2("screenSize", glm::vec2(width_size, height_size));

  // Set yellow color for crosshair
  uiShader->SetVec4("color", glm::vec4(1.0f, 1.0f, 0.0f, 1.0f)); // Yellow color

  const float centerX = static_cast<float>(width_size) * 0.5f;
  const float centerY = static_cast<float>(height_size) * 0.5f;
  const float crosshairSize = 20.0f;
  const float lineThickness = 2.0f;
  const float halfThick = lineThickness * 0.5f;

  const float horizontalLine[] = {
      centerX - crosshairSize, centerY - halfThick,     centerX + crosshairSize,
      centerY - halfThick,     centerX - crosshairSize, centerY + halfThick,
      centerX + crosshairSize, centerY + halfThick,
  };

  const float verticalLine[] = {
      centerX - halfThick,     centerY - crosshairSize, centerX + halfThick,
      centerY - crosshairSize, centerX - halfThick,     centerY + crosshairSize,
      centerX + halfThick,     centerY + crosshairSize,
  };

  // Create VAO and VBO for horizontal line
  GLuint horizontalVAO, horizontalVBO;
  glGenVertexArrays(1, &horizontalVAO);
  glGenBuffers(1, &horizontalVBO);

  glBindVertexArray(horizontalVAO);
  glBindBuffer(GL_ARRAY_BUFFER, horizontalVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(horizontalLine), horizontalLine,
               GL_STATIC_DRAW);

  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  // Draw horizontal line
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  // Create VAO and VBO for Vertical line
  GLuint verticalVAO, verticalVBO;
  glGenVertexArrays(1, &verticalVAO);
  glGenBuffers(1, &verticalVBO);

  glBindVertexArray(verticalVAO);
  glBindBuffer(GL_ARRAY_BUFFER, verticalVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(verticalLine), verticalLine,
               GL_STATIC_DRAW);

  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  // Draw Vertical line
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  // Очищаем ресурсы
  glDeleteVertexArrays(1, &horizontalVAO);
  glDeleteBuffers(1, &horizontalVBO);
  glDeleteVertexArrays(1, &verticalVAO);
  glDeleteBuffers(1, &verticalVBO);

  // Отключаем шейдер
  uiShader->Unuse();
}

void UGeometryEngine::RenderSimpleText(int width_size, int height_size)
{
  if (!textRenderer || !WorldInstance)
  {
    if (!textRenderer)
    {
      std::cerr << "TextRenderer is not available" << std::endl;
    }
    return;
  }

  textRenderer->SetWindowSize(width_size, height_size);

  const float scale = 0.8f;
  const glm::vec3 helpColor(1.0f, 1.0f, 1.0f);
  const glm::vec3 statusColor(1.0f, 0.85f, 0.2f);
  const glm::vec3 headerColor(0.0f, 1.0f, 1.0f);

  const std::string header = WorldInstance->GetCurrentUserName() + " [" +
                             WorldInstance->GetWorldName() + "]";
  textRenderer->RenderTextCentered(
      header, static_cast<float>(height_size) - 28.0f, scale, headerColor);

  const std::vector<std::string> helpLines = {
      "WASD - Movement",
      "Ctrl - Sprint (hold)",
      "Space - Jump / Fly up",
      "Shift - Crouch / Fly down",
      "2xSpace - Toggle flight",
      "0-9 - Primary hotbar; objects via HUD / palette",
      "Classic: mouse look, hold LMB break, RMB place/use slot",
      "Cubatarium: RMB drag look, LMB tap place/use slot / hold break",
      "Delete - Instant break, F1-F8 - Sky",
  };

  constexpr float helpX = 20.0f;
  float y = 20.0f;
  for (const std::string &line : helpLines)
  {
    textRenderer->RenderText(line, helpX, y, scale, helpColor);
    y += 20.0f;
  }

  const double now = std::chrono::duration<double>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
  if (!TransientMessage.empty() && now < TransientMessageUntil)
  {
    textRenderer->RenderTextCentered(TransientMessage, 8.0f, 1.0f, statusColor);
  }
}

void UGeometryEngine::RenderHotbarLabels(int width_size, int height_size)
{
  if (!textRenderer || !WorldInstance)
  {
    return;
  }

  textRenderer->SetWindowSize(width_size, height_size);
  auto user = WorldInstance->GetCurrentUser();
  if (!user)
  {
    return;
  }

  constexpr int kPreviewSize = 80;
  constexpr int kPreviewMargin = 10;
  const float labelX = static_cast<float>(kPreviewMargin + kPreviewSize + 8);
  const float previewBottom =
      static_cast<float>(height_size - kPreviewSize - kPreviewMargin);
  const float labelScale = 0.75f;

  const float blockY =
      previewBottom + static_cast<float>(kPreviewSize) * 0.5f - 6.0f;
  std::string activeBlock;
  if (UCreature *controlled = WorldInstance->GetControlledCreature())
  {
    activeBlock = controlled->GetInventory().GetActiveBlockTypeName();
  }
  textRenderer->RenderText(activeBlock, labelX, blockY, labelScale,
                           glm::vec3(1.0f, 1.0f, 1.0f));

  std::string prefabName;
  if (UCreature *controlled = WorldInstance->GetControlledCreature())
  {
    prefabName = controlled->GetInventory().GetActivePrefabName();
  }
  if (!prefabName.empty())
  {
    textRenderer->RenderText(
        "UObject: " + prefabName, static_cast<float>(kPreviewMargin),
        previewBottom - 18.0f, labelScale, glm::vec3(0.85f, 1.0f, 0.85f));
  }
}

void UGeometryEngine::InitPreviewBuffers()
{
  // Create a simple cube for preview (smaller scale)
  float scale = 0.3f; // Smaller cube for preview
  float cube_shift = 1.0f / 6.0f;
  float vertices[] = {
      // positions                   // texture coordinates
      // Face 0 (NEAR) - coordinates 0.0 - 1/6, V flipped for sides
      -scale, -scale, scale, 0.0f, 1.0f, scale, -scale, scale,
      cube_shift * 1.0f, 1.0f, -scale, scale, scale, 0.0f, 0.0f, scale, scale,
      scale, cube_shift * 1.0f, 0.0f,

      // Face 1 (RIGHT) - coordinates 1/6 - 2/6
      scale, -scale, scale, cube_shift * 1.0f, 1.0f, scale, -scale, -scale,
      cube_shift * 2.0f, 1.0f, scale, scale, scale, cube_shift * 1.0f, 0.0f,
      scale, scale, -scale, cube_shift * 2.0f, 0.0f,

      // Face 2 (FAR) - coordinates 2/6 - 3/6
      scale, -scale, -scale, cube_shift * 2.0f, 1.0f, -scale, -scale, -scale,
      cube_shift * 3.0f, 1.0f, scale, scale, -scale, cube_shift * 2.0f, 0.0f,
      -scale, scale, -scale, cube_shift * 3.0f, 0.0f,

      // Face 3 (LEFT) - coordinates 3/6 - 4/6
      -scale, -scale, -scale, cube_shift * 3.0f, 1.0f, -scale, -scale, scale,
      cube_shift * 4.0f, 1.0f, -scale, scale, -scale, cube_shift * 3.0f, 0.0f,
      -scale, scale, scale, cube_shift * 4.0f, 0.0f,

      // Face 4 (TOP) - coordinates 4/6 - 5/6
      -scale, scale, scale, cube_shift * 4.0f, 0.0f, scale, scale, scale,
      cube_shift * 5.0f, 0.0f, -scale, scale, -scale, cube_shift * 4.0f, 1.0f,
      scale, scale, -scale, cube_shift * 5.0f, 1.0f,

      // Face 5 (BOTTOM) - coordinates 5/6 - 1.0
      -scale, -scale, -scale, cube_shift * 5.0f, 0.0f, scale, -scale, -scale,
      1.0f, 0.0f, -scale, -scale, scale, cube_shift * 5.0f, 1.0f, scale, -scale,
      scale, 1.0f, 1.0f};

  unsigned int indices[] = {0,  1,  2,  2,  1,  3,  4,  5,  6,  6,  5,  7,
                            8,  9,  10, 10, 9,  11, 12, 13, 14, 14, 13, 15,
                            16, 17, 18, 18, 17, 19, 20, 21, 22, 22, 21, 23};

  glGenVertexArrays(1, &previewVAO);
  glGenBuffers(1, &previewVBO);
  glGenBuffers(1, &previewEBO);

  glBindVertexArray(previewVAO);
  glBindBuffer(GL_ARRAY_BUFFER, previewVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, previewEBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
               GL_STATIC_DRAW);

  // Vertex attributes
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);

  // Load a default texture for preview (prefer "stone", else first available)
  if (TextureCubeStorageInstance)
  {
    const auto &texMap = TextureCubeStorageInstance->GetTextures();
    GLuint texId = 0;
    for (const auto &kv : texMap)
    {
      const UTextureCube &tc = kv.second;
      if (tc.GetName() == std::string("stone"))
      {
        texId = tc.GetTexture();
        break;
      }
    }
    if (texId == 0 && !texMap.empty())
    {
      texId = texMap.begin()->second.GetTexture();
    }
    previewTexture = texId;
  }
}

void UGeometryEngine::DestroyPreviewBuffers()
{
  if (previewVAO)
  {
    glDeleteVertexArrays(1, &previewVAO);
    previewVAO = 0;
  }
  if (previewVBO)
  {
    glDeleteBuffers(1, &previewVBO);
    previewVBO = 0;
  }
  if (previewEBO)
  {
    glDeleteBuffers(1, &previewEBO);
    previewEBO = 0;
  }
  // Note: previewTexture is managed by UTextureCubeStorage, don't delete it
}

void UGeometryEngine::RenderActiveObjectPreview(int width_size, int height_size)
{
  if (!defaultShader || !defaultShader->IsValid() || !previewVAO)
    return;

  // Save current state
  GLint prevViewport[4];
  glGetIntegerv(GL_VIEWPORT, prevViewport);

  // Set viewport for preview (top-right corner)
  int previewSize = 80; // 80x80 pixels
  int x = 10;
  int y = height_size - previewSize - 10;
  glViewport(x, y, previewSize, previewSize);

  // Enable depth testing for 3D effect
  glEnable(GL_DEPTH_TEST);

  // Create orthographic projection for preview
  glm::mat4 projection = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f);

  // Create view matrix (look at cube from slightly above and to the side)
  glm::mat4 view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), // eye position
                               glm::vec3(0.0f, 0.0f, 0.0f), // center
                               glm::vec3(0.0f, 1.0f, 0.0f)  // up
  );

  // Model matrix (identity for centered cube)
  glm::mat4 model = glm::mat4(1.0f);

  // Combine matrices
  glm::mat4 mvp = projection * view * model;

  // Use shader
  defaultShader->Use();
  defaultShader->SetMat4("mvp_matrix", mvp);
  defaultShader->SetInt("texture0", 0);

  // Pick texture by Active block Name if available
  GLuint texId = previewTexture;
  if (WorldInstance && TextureCubeStorageInstance)
  {
    std::string activeName;
    if (UCreature *controlled = WorldInstance->GetControlledCreature())
    {
      activeName = controlled->GetInventory().GetActiveBlockTypeName();
    }
    if (!activeName.empty())
    {
      const auto &texMap = TextureCubeStorageInstance->GetTextures();
      for (const auto &kv : texMap)
      {
        const UTextureCube &tc = kv.second;
        if (tc.GetName() == activeName)
        {
          texId = tc.GetTexture();
          break;
        }
      }
    }
  }

  // Bind chosen texture (fallback to previewTexture if not found)
  if (texId)
  {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texId);
  }

  // Draw preview cube
  glBindVertexArray(previewVAO);
  glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);

  // Restore viewport
  glViewport(prevViewport[0], prevViewport[1], prevViewport[2],
             prevViewport[3]);

  defaultShader->Unuse();
}

// Initialize static cube geometry buffers
bool UGeometryEngine::InitCubeBuffers()
{
  if (cubeVAO != 0)
    return true;

  float cube_shift = 1.0f / 6.0f;
  const float vertices[] = {
      // positions              // texcoords
      // Face 0 (NEAR) — V flipped for side faces (Y maps to texture V)
      -0.5f, -0.5f, 0.5f, 0.0f, 1.0f, 0.5f, -0.5f, 0.5f, cube_shift * 1.0f,
      1.0f, -0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f, cube_shift * 1.0f,
      0.0f,

      // Face 1 (RIGHT)
      0.5f, -0.5f, 0.5f, cube_shift * 1.0f, 1.0f, 0.5f, -0.5f, -0.5f,
      cube_shift * 2.0f, 1.0f, 0.5f, 0.5f, 0.5f, cube_shift * 1.0f, 0.0f, 0.5f,
      0.5f, -0.5f, cube_shift * 2.0f, 0.0f,

      // Face 2 (FAR)
      0.5f, -0.5f, -0.5f, cube_shift * 2.0f, 1.0f, -0.5f, -0.5f, -0.5f,
      cube_shift * 3.0f, 1.0f, 0.5f, 0.5f, -0.5f, cube_shift * 2.0f, 0.0f,
      -0.5f, 0.5f, -0.5f, cube_shift * 3.0f, 0.0f,

      // Face 3 (LEFT)
      -0.5f, -0.5f, -0.5f, cube_shift * 3.0f, 1.0f, -0.5f, -0.5f, 0.5f,
      cube_shift * 4.0f, 1.0f, -0.5f, 0.5f, -0.5f, cube_shift * 3.0f, 0.0f,
      -0.5f, 0.5f, 0.5f, cube_shift * 4.0f, 0.0f,

      // Face 4 (TOP)
      -0.5f, 0.5f, 0.5f, cube_shift * 4.0f, 0.0f, 0.5f, 0.5f, 0.5f,
      cube_shift * 5.0f, 0.0f, -0.5f, 0.5f, -0.5f, cube_shift * 4.0f, 1.0f,
      0.5f, 0.5f, -0.5f, cube_shift * 5.0f, 1.0f,

      // Face 5 (BOTTOM)
      -0.5f, -0.5f, -0.5f, cube_shift * 5.0f, 0.0f, 0.5f, -0.5f, -0.5f, 1.0f,
      0.0f, -0.5f, -0.5f, 0.5f, cube_shift * 5.0f, 1.0f, 0.5f, -0.5f, 0.5f,
      1.0f, 1.0f};

  const unsigned int indices[] = {
      0,  1,  2,  2,  1,  3,  4,  5,  6,  6,  5,  7,  8,  9,  10, 10, 9,  11,
      12, 13, 14, 14, 13, 15, 16, 17, 18, 18, 17, 19, 20, 21, 22, 22, 21, 23};

  glGenVertexArrays(1, &cubeVAO);
  glGenBuffers(1, &cubeVBO);
  glGenBuffers(1, &cubeEBO);
  glGenBuffers(1, &instanceVBO);

  glBindVertexArray(cubeVAO);
  glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeEBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
               GL_STATIC_DRAW);

  // Attributes: position (0), texcoord (1)
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  // Instance attribute: mat4 (locations 2,3,4,5)
  glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
  glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
  std::size_t vec4Size = sizeof(glm::vec4);
  for (int i = 0; i < 4; ++i)
  {
    glVertexAttribPointer(2 + i, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4),
                          (void *)(i * vec4Size));
    glEnableVertexAttribArray(2 + i);
    glVertexAttribDivisor(2 + i, 1);
  }

  glBindVertexArray(0);
  return cubeVAO != 0;
}

bool UGeometryEngine::InitFaceQuadBuffers()
{
  if (faceVAO != 0)
  {
    DestroyFaceQuadBuffers();
  }

  const float vertices[] = {
      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
      1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
  };
  const unsigned int indices[] = {0, 1, 2, 0, 2, 3};

  glGenVertexArrays(1, &faceVAO);
  glGenBuffers(1, &faceVBO);
  glGenBuffers(1, &faceEBO);
  glGenBuffers(1, &instanceBlockVBO);

  glBindVertexArray(faceVAO);
  glBindBuffer(GL_ARRAY_BUFFER, faceVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, faceEBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
               GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  constexpr std::size_t kStride = sizeof(glm::mat4) + sizeof(float) * 4;
  glBindBuffer(GL_ARRAY_BUFFER, instanceBlockVBO);
  glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
  std::size_t vec4Size = sizeof(glm::vec4);
  for (int i = 0; i < 4; ++i)
  {
    glVertexAttribPointer(2 + i, 4, GL_FLOAT, GL_FALSE, kStride,
                          (void *)(i * vec4Size));
    glEnableVertexAttribArray(2 + i);
    glVertexAttribDivisor(2 + i, 1);
  }
  glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, kStride,
                        (void *)sizeof(glm::mat4));
  glEnableVertexAttribArray(6);
  glVertexAttribDivisor(6, 1);

  glBindVertexArray(0);
  return faceVAO != 0;
}

void UGeometryEngine::DestroyFaceQuadBuffers()
{
  if (instanceBlockVBO)
  {
    glDeleteBuffers(1, &instanceBlockVBO);
    instanceBlockVBO = 0;
  }
  if (faceEBO)
  {
    glDeleteBuffers(1, &faceEBO);
    faceEBO = 0;
  }
  if (faceVBO)
  {
    glDeleteBuffers(1, &faceVBO);
    faceVBO = 0;
  }
  if (faceVAO)
  {
    glDeleteVertexArrays(1, &faceVAO);
    faceVAO = 0;
  }
}

void UGeometryEngine::DestroyCubeBuffers()
{
  if (cubeEBO)
  {
    glDeleteBuffers(1, &cubeEBO);
    cubeEBO = 0;
  }
  if (cubeVBO)
  {
    glDeleteBuffers(1, &cubeVBO);
    cubeVBO = 0;
  }
  if (instanceVBO)
  {
    glDeleteBuffers(1, &instanceVBO);
    instanceVBO = 0;
  }
  if (cubeVAO)
  {
    glDeleteVertexArrays(1, &cubeVAO);
    cubeVAO = 0;
  }
}

bool UGeometryEngine::InitOutlineBuffers()
{
  if (outlineVAO != 0)
  {
    return true;
  }

  constexpr float half = 0.5f + 0.005f;
  const float vertices[] = {
      -half, -half, half,  half, -half, half,  -half, half,
      half,  half,  half,  half, -half, -half, -half, half,
      -half, -half, -half, half, -half, half,  half,  -half,
  };

  const unsigned int indices[] = {
      0, 1, 1, 3, 3, 2, 2, 0, 4, 5, 5, 7, 7, 6, 6, 4, 0, 4, 1, 5, 2, 6, 3, 7,
  };

  glGenVertexArrays(1, &outlineVAO);
  glGenBuffers(1, &outlineVBO);
  glGenBuffers(1, &outlineEBO);

  glBindVertexArray(outlineVAO);
  glBindBuffer(GL_ARRAY_BUFFER, outlineVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, outlineEBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
               GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  glBindVertexArray(0);
  return outlineVAO != 0;
}

void UGeometryEngine::DestroyOutlineBuffers()
{
  if (outlineEBO)
  {
    glDeleteBuffers(1, &outlineEBO);
    outlineEBO = 0;
  }
  if (outlineVBO)
  {
    glDeleteBuffers(1, &outlineVBO);
    outlineVBO = 0;
  }
  if (outlineVAO)
  {
    glDeleteVertexArrays(1, &outlineVAO);
    outlineVAO = 0;
  }
}

namespace
{

bool UploadCreaturePartMesh(GLuint &vao, GLuint &vbo, GLuint &ebo,
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

  if (vao == 0)
  {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
  }
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kCreaturePartIndices),
               kCreaturePartIndices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glBindVertexArray(0);
  return vao != 0;
}

} // namespace

bool UGeometryEngine::InitCreaturePartBuffers()
{
  if (creaturePartVAO != 0)
  {
    return true;
  }
  float texCoords[48];
  BuildCreatureBoxTexCoords(texCoords);
  return UploadCreaturePartMesh(creaturePartVAO, creaturePartVBO,
                                creaturePartEBO, texCoords);
}

bool UGeometryEngine::InitCreatureHeadPartBuffers()
{
  if (creatureHeadPartVAO != 0)
  {
    return true;
  }
  float texCoords[48];
  BuildCreatureHeadTexCoords(texCoords);
  return UploadCreaturePartMesh(creatureHeadPartVAO, creatureHeadPartVBO,
                                creatureHeadPartEBO, texCoords);
}

bool UGeometryEngine::InitCreatureBodyPartBuffers()
{
  if (creatureBodyPartVAO != 0)
  {
    return true;
  }
  float texCoords[48];
  BuildCreatureBodyTexCoords(texCoords);
  return UploadCreaturePartMesh(creatureBodyPartVAO, creatureBodyPartVBO,
                                creatureBodyPartEBO, texCoords);
}

bool UGeometryEngine::InitCreatureRigidHeadPartBuffers()
{
  if (creatureRigidHeadPartVAO != 0)
  {
    return true;
  }
  float texCoords[48];
  BuildCreatureRigidHeadTexCoords(texCoords);
  return UploadCreaturePartMesh(creatureRigidHeadPartVAO,
                                creatureRigidHeadPartVBO,
                                creatureRigidHeadPartEBO, texCoords);
}

void UGeometryEngine::DestroyCreaturePartBuffers()
{
  if (creatureRigidHeadPartEBO)
  {
    glDeleteBuffers(1, &creatureRigidHeadPartEBO);
    creatureRigidHeadPartEBO = 0;
  }
  if (creatureRigidHeadPartVBO)
  {
    glDeleteBuffers(1, &creatureRigidHeadPartVBO);
    creatureRigidHeadPartVBO = 0;
  }
  if (creatureRigidHeadPartVAO)
  {
    glDeleteVertexArrays(1, &creatureRigidHeadPartVAO);
    creatureRigidHeadPartVAO = 0;
  }
  if (creatureBodyPartEBO)
  {
    glDeleteBuffers(1, &creatureBodyPartEBO);
    creatureBodyPartEBO = 0;
  }
  if (creatureBodyPartVBO)
  {
    glDeleteBuffers(1, &creatureBodyPartVBO);
    creatureBodyPartVBO = 0;
  }
  if (creatureBodyPartVAO)
  {
    glDeleteVertexArrays(1, &creatureBodyPartVAO);
    creatureBodyPartVAO = 0;
  }
  if (creatureHeadPartEBO)
  {
    glDeleteBuffers(1, &creatureHeadPartEBO);
    creatureHeadPartEBO = 0;
  }
  if (creatureHeadPartVBO)
  {
    glDeleteBuffers(1, &creatureHeadPartVBO);
    creatureHeadPartVBO = 0;
  }
  if (creatureHeadPartVAO)
  {
    glDeleteVertexArrays(1, &creatureHeadPartVAO);
    creatureHeadPartVAO = 0;
  }
  if (creaturePartEBO)
  {
    glDeleteBuffers(1, &creaturePartEBO);
    creaturePartEBO = 0;
  }
  if (creaturePartVBO)
  {
    glDeleteBuffers(1, &creaturePartVBO);
    creaturePartVBO = 0;
  }
  if (creaturePartVAO)
  {
    glDeleteVertexArrays(1, &creaturePartVAO);
    creaturePartVAO = 0;
  }
}

void UGeometryEngine::DrawCreatureTexturedPart(const glm::mat4 &mvp,
                                               GLuint texture,
                                               CreaturePartMesh mesh)
{
  if (texture == 0 || !defaultShader || !defaultShader->IsValid())
  {
    return;
  }
  GLuint vao = 0;
  switch (mesh)
  {
  case CreaturePartMesh::Head:
    if (creatureHeadPartVAO == 0 && !InitCreatureHeadPartBuffers())
    {
      return;
    }
    vao = creatureHeadPartVAO;
    break;
  case CreaturePartMesh::Body:
    if (creatureBodyPartVAO == 0 && !InitCreatureBodyPartBuffers())
    {
      return;
    }
    vao = creatureBodyPartVAO;
    break;
  case CreaturePartMesh::RigidHead:
    if (creatureRigidHeadPartVAO == 0 && !InitCreatureRigidHeadPartBuffers())
    {
      return;
    }
    vao = creatureRigidHeadPartVAO;
    break;
  case CreaturePartMesh::Box:
  default:
    if (creaturePartVAO == 0 && !InitCreaturePartBuffers())
    {
      return;
    }
    vao = creaturePartVAO;
    break;
  }

  GLboolean depthEnabled = GL_TRUE;
  glGetBooleanv(GL_DEPTH_TEST, &depthEnabled);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);

  glBindTexture(GL_TEXTURE_2D, texture);
  defaultShader->Use();
  defaultShader->SetInt("texture0", 0);
  defaultShader->SetInt("uAnimFrame", 0);
  defaultShader->SetInt("uAnimFrameCount", 1);
  defaultShader->SetMat4("mvp_matrix", mvp);

  glBindVertexArray(vao);
  glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);

  defaultShader->Unuse();
  glBindTexture(GL_TEXTURE_2D, 0);

  if (!depthEnabled)
  {
    glDisable(GL_DEPTH_TEST);
  }
}

void UGeometryEngine::DrawCreatureSkinnedMesh(const glm::mat4 & /*mvp*/,
                                              GLuint /*meshVao*/,
                                              GLuint /*texture*/)
{
  // glTF skinned mesh draw — TD-CRE-001
}

void UGeometryEngine::DrawBoxWireframe(const glm::mat4 &mvp,
                                       const glm::vec4 &color)
{
  if (!outlineShader || !outlineShader->IsValid())
  {
    if (!InitOutlineBuffers())
    {
      return;
    }
  }
  if (outlineVAO == 0 && !InitOutlineBuffers())
  {
    return;
  }

  GLboolean cullFaceEnabled;
  glGetBooleanv(GL_CULL_FACE, &cullFaceEnabled);
  GLfloat previousLineWidth = 1.0f;
  glGetFloatv(GL_LINE_WIDTH, &previousLineWidth);

  glDisable(GL_CULL_FACE);
  glLineWidth(2.5f);

  outlineShader->Use();
  outlineShader->SetMat4("mvp_matrix", mvp);
  outlineShader->SetVec4("color", color);

  glBindVertexArray(outlineVAO);
  glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);

  outlineShader->Unuse();

  glLineWidth(previousLineWidth);
  if (cullFaceEnabled)
  {
    glEnable(GL_CULL_FACE);
  }
  else
  {
    glDisable(GL_CULL_FACE);
  }
}

void UGeometryEngine::RenderCreatures()
{
  if (!WorldInstance)
  {
    return;
  }
  auto camera = WorldInstance->GetCurrentUserCamera();
  if (!camera)
  {
    return;
  }
  const glm::mat4 viewProj = camera->GetProjection() * camera->GetViewMatrix();
  const float dt = static_cast<float>(camera->GetDeltaTime());
  const CreatureId controlledId = WorldInstance->GetControlledCreatureId();
  WorldInstance->ForEachCreature(
      [&](UCreature &creature)
      {
        if (creature.GetId() == controlledId)
        {
          if (camera->GetPerspective() == CameraPerspective::FirstPerson)
          {
            return;
          }
        }
        const std::string animType =
            WorldInstance->ResolveAnimationTypeId(creature);
        const CreatureDefinition *def =
            WorldInstance->GetCreatureDefinition(animType);
        CreatureDefinition fallback;
        if (!def)
        {
          fallback.Id = animType;
          def = &fallback;
        }
        if (ICreatureVisual *visual = creature.GetVisual())
        {
          visual->SetAppearance(WorldInstance->GetResolvedAppearance(creature));
          const CreatureLocomotionFacts &facts = creature.GetLocomotionFacts();
          ICreaturePosePresenter *presenter =
              WorldInstance->GetPosePresenterRegistry().Get(facts.archetype);
          CreaturePoseParams pose;
          if (presenter)
          {
            pose = presenter->Compute(facts, *def, dt);
          }
          visual->UpdatePose(creature, facts, pose, *def, dt);
          visual->SubmitDraw(*this, viewProj);
        }

        if (Render.CreatureDebugBounds)
        {
          const glm::vec3 bodyOrigin = creature.GetBodyOrigin();
          const glm::vec3 maxSize = creature.GetBounds().profile.maxSizeBlocks;
          const glm::vec3 center = BoundsCollisionCenter(bodyOrigin, maxSize);
          glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
          model = glm::scale(model, maxSize);
          DrawBoxWireframe(viewProj * model,
                           glm::vec4(0.2f, 0.85f, 1.0f, 1.0f));

          const int gx = static_cast<int>(std::floor(bodyOrigin.x));
          const int gz = static_cast<int>(std::floor(bodyOrigin.z));
          const float feetY = BoundsFeetY(bodyOrigin);
          float groundY = feetY;
          float delta = 0.0f;
          if (const std::optional<float> queryY =
                  WorldInstance->QueryGroundFeetYUnder(gx, gz, feetY))
          {
            groundY = *queryY;
            delta = feetY - groundY;
          }
          const glm::vec3 groundCenter(static_cast<float>(gx) + 0.5f, groundY,
                                       static_cast<float>(gz) + 0.5f);
          glm::mat4 groundModel = glm::translate(glm::mat4(1.0f), groundCenter);
          groundModel = glm::scale(groundModel, glm::vec3(1.02f, 0.02f, 1.02f));
          const float groundColor = std::abs(delta) < 0.05f ? 0.2f : 1.0f;
          DrawBoxWireframe(
              viewProj * groundModel,
              glm::vec4(groundColor, 1.0f - groundColor * 0.5f, 0.15f, 1.0f));

          static auto lastPoseLog = std::chrono::steady_clock::now();
          const auto now = std::chrono::steady_clock::now();
          if (now - lastPoseLog >= std::chrono::seconds(2))
          {
            lastPoseLog = now;
            const float eyeY = creature.GetLocomotionEye().y;
            const bool isControlled = creature.GetId() == controlledId;
            if (isControlled || controlledId == 0)
            {
              std::cerr << "[creature_pose] Id=" << creature.GetId()
                        << " feetY=" << feetY << " groundY=" << groundY
                        << " delta=" << delta << " eyeY=" << eyeY << std::endl;
            }
          }
        }
      });
}

namespace
{

void DrawBlockOutline(UShaderProgram *shader, GLuint vao, const glm::mat4 &mvp,
                      const glm::vec4 &color)
{
  if (!shader || !shader->IsValid() || vao == 0)
  {
    return;
  }
  shader->Use();
  shader->SetMat4("mvp_matrix", mvp);
  shader->SetVec4("color", color);
  glBindVertexArray(vao);
  glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);
  shader->Unuse();
}

} // namespace

void UGeometryEngine::RenderBlockCrackOverlay()
{
  if (!WorldInstance || !WorldInstance->HasBreakSession())
  {
    return;
  }
  const std::optional<glm::ivec3> blockPos =
      WorldInstance->GetBreakSessionBlockPos();
  if (!blockPos || !outlineShader || !outlineShader->IsValid() ||
      outlineVAO == 0)
  {
    return;
  }

  auto camera = WorldInstance->GetCurrentUserCamera();
  if (!camera)
  {
    return;
  }

  const float progress = WorldInstance->GetBreakProgress();
  const glm::mat4 viewProj = camera->GetProjection() * camera->GetViewMatrix();
  const glm::mat4 mvp =
      viewProj * glm::translate(glm::mat4(1.0f), BlockCenter(*blockPos));

  GLboolean cullFaceEnabled;
  glGetBooleanv(GL_CULL_FACE, &cullFaceEnabled);
  GLfloat previousLineWidth = 1.0f;
  glGetFloatv(GL_LINE_WIDTH, &previousLineWidth);

  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glLineWidth(1.5f + progress * 4.0f);

  const float alpha = 0.35f + progress * 0.55f;
  DrawBlockOutline(outlineShader.get(), outlineVAO, mvp,
                   glm::vec4(0.25f, 0.25f, 0.25f, alpha));

  if (progress > 0.35f)
  {
    const glm::mat4 inset =
        viewProj * glm::translate(glm::mat4(1.0f), BlockCenter(*blockPos)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(0.92f));
    DrawBlockOutline(outlineShader.get(), outlineVAO, inset,
                     glm::vec4(0.1f, 0.1f, 0.1f, alpha * 0.85f));
  }

  glLineWidth(previousLineWidth);
  glDisable(GL_BLEND);
  if (cullFaceEnabled)
  {
    glEnable(GL_CULL_FACE);
  }
  else
  {
    glDisable(GL_CULL_FACE);
  }
}

void UGeometryEngine::RenderSelectionOutline()
{
  if (!outlineShader || !outlineShader->IsValid() || outlineVAO == 0)
  {
    return;
  }

  auto camera = WorldInstance->GetCurrentUserCamera();
  if (!camera)
  {
    return;
  }

  const glm::mat4 viewProj = camera->GetProjection() * camera->GetViewMatrix();

  GLboolean cullFaceEnabled;
  glGetBooleanv(GL_CULL_FACE, &cullFaceEnabled);
  GLfloat previousLineWidth = 1.0f;
  glGetFloatv(GL_LINE_WIDTH, &previousLineWidth);

  glDisable(GL_CULL_FACE);
  glLineWidth(2.0f);

  if (WorldInstance->GetIsBlockIntersectionExists())
  {
    const glm::ivec3 breakPos = WorldInstance->GetBreakBlockPos();
    const glm::mat4 breakMvp =
        viewProj * glm::translate(glm::mat4(1.0f), BlockCenter(breakPos));
    DrawBlockOutline(outlineShader.get(), outlineVAO, breakMvp,
                     glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
  }

  if (WorldInstance->HasPlaceTarget())
  {
    const glm::ivec3 placePos = WorldInstance->GetPlaceBlockPos();
    const glm::mat4 placeMvp =
        viewProj * glm::translate(glm::mat4(1.0f), BlockCenter(placePos));
    DrawBlockOutline(outlineShader.get(), outlineVAO, placeMvp,
                     glm::vec4(0.2f, 0.8f, 0.2f, 1.0f));
  }

  glLineWidth(previousLineWidth);
  if (cullFaceEnabled)
  {
    glEnable(GL_CULL_FACE);
  }
  else
  {
    glDisable(GL_CULL_FACE);
  }
}

} // namespace cutum

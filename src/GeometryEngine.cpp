
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <chrono>
#include <vector>
#include <GL/glew.h>
#include "GeometryEngine.h"
#include "CreaturePartMeshData.h"
#include "CreatureTextureStorage.h"
#include "Creature.h"
#include "CreatureBounds.h"
#include "CameraPerspective.h"
#include "CreatureDefinition.h"
#include "CreatureVisual.h"
#include "CreatureLocomotionFacts.h"
#include "CreaturePoseParams.h"
#include "pose/ICreaturePosePresenter.h"
#include <glm/gtc/matrix_transform.hpp>
#include "GridMath.h"
#include "Core.h"
#include "ObjectImplementation.h"
#include "User.h"
#include "ObjectStorage.h"
#include "Camera.h"
#include "ShaderManager.h"
#include "BlockRegistry.h"
#include "render/GreedyShaderMode.h"
#include "render/GreedyTransparentPipeline.h"
#include "render/GreedyTransparentSort.h"

namespace cutum {

GeometryEngine::GeometryEngine(std::shared_ptr<ObjectStorage> object_storage, std::shared_ptr<World> world, std::shared_ptr<TextureBaseStorage> texture_base_storage, std::shared_ptr<TextureCubeStorage> texture_cube_storage, std::shared_ptr<TextRenderer> text_renderer)
 : ObjectStorageInstance(object_storage)
 , WorldInstance(world)
 , TextureBaseStorageInstance(texture_base_storage)
 , TextureCubeStorageInstance(texture_cube_storage)
 , textRenderer(text_renderer)
 , skyColor(0.5f, 0.7f, 1.0f, 1.0f)
 , baseSkyColor_(0.5f, 0.7f, 1.0f)
 , smoothedSkyTint_(0.5f, 0.7f, 1.0f)
, useGradientSky(false) // Use simple color by default
{
}

GeometryEngine::~GeometryEngine()
{
    DestroyCubeBuffers();
    DestroyFaceQuadBuffers();
    DestroyGreedyMeshBuffers();
    DestroyPreviewBuffers();
    DestroyOutlineBuffers();
    DestroyCreaturePartBuffers();
    DestroyOverlayBuffers();
}

void GeometryEngine::SetCreatureTextureStorage(std::shared_ptr<CreatureTextureStorage> storage)
{
 CreatureTextureStorageInstance_ = std::move(storage);
}

bool GeometryEngine::InitEngine()
{
     // Initialize ShaderManager
 shaderManager = std::make_shared<ShaderManager>();
 if (!shaderManager->Initialize()) {
     std::cerr << "Failed to initialize ShaderManager" << std::endl;
     return false;
 }
 
 if(!InitShaders())
  return false;
 
 // Initialize static cube buffers
 if (!InitCubeBuffers()) {
     std::cerr << "Failed to initialize cube buffers" << std::endl;
     return false;
 }

 blockBatchesValid_ = false;
 
 // Initialize preview buffers
 InitPreviewBuffers();

 if (!InitCreaturePartBuffers() || !InitCreatureHeadPartBuffers() ||
     !InitCreatureBodyPartBuffers()) {
  std::cerr << "Failed to initialize creature part buffers" << std::endl;
  return false;
 }

 if (!InitOutlineBuffers()) {
     std::cerr << "Failed to initialize outline buffers" << std::endl;
     return false;
 }
 
 return true;
}

bool GeometryEngine::InitShaders()
{
     // Create shaders through ShaderManager
 defaultShader = shaderManager->CreateShader("default", "shaders/vshader.glsl", "shaders/fshader.glsl");
 if (!defaultShader || !defaultShader->IsValid()) {
     std::cerr << "Failed to create default shader" << std::endl;
     return false;
 }
 
 skyShader = shaderManager->CreateShader("sky", "shaders/vshader.glsl", "shaders/fshader_sky.glsl");
 if (!skyShader || !skyShader->IsValid()) {
     std::cerr << "Failed to create sky shader" << std::endl;
     return false;
 }
 
 uiShader = shaderManager->CreateShader("ui", "shaders/vshader_2d.glsl", "shaders/fshader_2d.glsl");
 if (!uiShader || !uiShader->IsValid()) {
     std::cerr << "Failed to create UI shader" << std::endl;
     return false;
 }
 
 textShader = shaderManager->CreateShader("text", "shaders/vshader_text.glsl", "shaders/fshader_text.glsl");
 if (!textShader || !textShader->IsValid()) {
     std::cerr << "Failed to create text shader" << std::endl;
     return false;
 }
 
 // Instanced cube shader (legacy block path — passthrough UV from cube VAO)
 instancedShader = shaderManager->CreateShader("instanced", "shaders/vshader_instanced.glsl", "shaders/fshader.glsl");
 if (!instancedShader || !instancedShader->IsValid()) {
     std::cerr << "Failed to create instanced shader from files, trying inline sources" << std::endl;
     const char* instancedVS = R"(
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
     const char* commonFS = R"(
 #version 330 core
 in vec2 TexCoord;
 out vec4 FragColor;
 uniform sampler2D texture0;
 void main()
 {
     FragColor = texture(texture0, TexCoord);
 }
 )";
     instancedShader = shaderManager->CreateShaderFromStrings("instanced", instancedVS, commonFS);
     if (!instancedShader || !instancedShader->IsValid()) {
         std::cerr << "Failed to create instanced shader" << std::endl;
         return false;
     }
 }

 instancedFaceShader = shaderManager->CreateShader(
     "instanced_face", "shaders/vshader_instanced_face.glsl", "shaders/fshader.glsl");
 if (!instancedFaceShader || !instancedFaceShader->IsValid()) {
     std::cerr << "Failed to create instanced face shader from files, trying inline sources" << std::endl;
     const char* instancedFaceVS = R"(
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
    if (face == 0) return vec2(insetMix(u0, u1, tx, inset.x), insetMix(1.0, 0.0, ty, inset.y));
    if (face == 1) return vec2(insetMix(u0, u1, 1.0 - tz, inset.x), insetMix(1.0, 0.0, ty, inset.y));
    if (face == 2) return vec2(insetMix(u0, u1, 1.0 - tx, inset.x), insetMix(1.0, 0.0, ty, inset.y));
    if (face == 3) return vec2(insetMix(u0, u1, tz, inset.x), insetMix(1.0, 0.0, ty, inset.y));
    if (face == 4) return vec2(insetMix(u0, u1, tx, inset.x), insetMix(0.0, 1.0, 1.0 - tz, inset.y));
    return vec2(insetMix(u0, u1, tx, inset.x), insetMix(0.0, 1.0, tz, inset.y));
 }
 void main() {
     vec4 worldPos = instanceModel * vec4(aPos, 1.0);
     gl_Position = uVP * worldPos;
     int face = int(instanceFaceIndex + 0.5);
     TexCoord = atlasUVFromWorldPos(face, worldPos.xyz);
 }
 )";
     const char* commonFS = R"(
 #version 330 core
 in vec2 TexCoord;
 out vec4 FragColor;
 uniform sampler2D texture0;
 void main()
 {
     FragColor = texture(texture0, TexCoord);
 }
 )";
     instancedFaceShader = shaderManager->CreateShaderFromStrings("instanced_face", instancedFaceVS, commonFS);
     if (!instancedFaceShader || !instancedFaceShader->IsValid()) {
         std::cerr << "Failed to create instanced face shader" << std::endl;
         return false;
     }
 }

 greedyShader = shaderManager->CreateShader("greedy", "shaders/vshader_greedy.glsl", "shaders/fshader_greedy.glsl");
 if (!greedyShader || !greedyShader->IsValid()) {
     std::cerr << "Failed to create greedy mesh shader" << std::endl;
     return false;
 }

 outlineShader = shaderManager->CreateShader("outline", "shaders/vshader.glsl", "shaders/fshader_2d.glsl");
 if (!outlineShader || !outlineShader->IsValid()) {
     std::cerr << "Failed to create outline shader" << std::endl;
     return false;
 }

 overlayShader = shaderManager->CreateShader("overlay", "shaders/vshader_overlay.glsl",
                                             "shaders/fshader_overlay.glsl");
 if (!overlayShader || !overlayShader->IsValid()) {
     std::cerr << "Failed to create overlay shader" << std::endl;
     return false;
 }
 
 return true;
}

void GeometryEngine::Paint(int width_size, int height_size, double view_duration)
{
 if (auto camera = WorldInstance->GetCurrentUserCamera()) {
  animationClock_.Tick(static_cast<float>(camera->GetDeltaTime()));
 }
 (void)view_duration;
 DrawCubeGeometry();
 if (overlayTintAlpha_ > 0.01f) {
  RenderFluidOverlay(width_size, height_size);
 }
 
     // Render crosshair
if (showCrosshair) {
    RenderCrosshair(width_size, height_size);
}
 
     // Render simple text
if (showHud) {
    RenderSimpleText(width_size, height_size);
    RenderActiveObjectPreview(width_size, height_size);
    RenderHotbarLabels(width_size, height_size);
}
 
     // Disable performance UI text rendering
    if (showPerformance) {
        RenderPerformanceText(width_size, height_size, view_duration);
    }
}

void GeometryEngine::DrawCubeGeometry()
{
 auto t_begin = std::chrono::high_resolution_clock::now();
 
 auto camera = WorldInstance->GetCurrentUserCamera();
if (!camera) {
    return;
}
 
 // Save OpenGL state
 GLboolean depthTestEnabled;
 glGetBooleanv(GL_DEPTH_TEST, &depthTestEnabled);
 GLboolean blendEnabled;
 glGetBooleanv(GL_BLEND, &blendEnabled);
 GLboolean cullFaceEnabled;
 glGetBooleanv(GL_CULL_FACE, &cullFaceEnabled);
  
  // Ensure instanced resources are ready
  if (cubeVAO == 0) {
      if (!InitCubeBuffers()) {
          std::cerr << "DrawCubeGeometry: cube buffers not initialized" << std::endl;
          return;
      }
  }

  auto textures = TextureCubeStorageInstance->GetTextures();
  const uint64_t meshRevision = WorldInstance->GetMeshRevision();
  const bool useGreedyMesh = renderSettings_.UseFaceQuadDraw();
  const size_t renderCount = useGreedyMesh
      ? WorldInstance->GetGreedyVertexCount()
      : WorldInstance->GetBlockRenderInstances().size();
  const bool useBatchCache = renderSettings_.batchCache && !useGreedyMesh;

  if (useGreedyMesh) {
   const auto& greedyBatches = WorldInstance->GetGreedyRenderBatches();
   if (!useBatchCache || !blockBatchesValid_ || greedyBatches.size() != cachedInstanceCount_
       || meshRevision != cachedMeshRevision_) {
    cachedInstanceCount_ = greedyBatches.size();
    cachedMeshRevision_ = meshRevision;
    blockBatchesValid_ = true;
   }
   const glm::mat4 vp = camera->GetProjection() * camera->GetViewMatrix();
   const uint64_t cullRev = WorldInstance->GetCullRevision();
   DrawGreedyOpaqueBatches(greedyBatches, vp, textures, meshRevision, cullRev);
   GLboolean blendWasEnabled;
   glGetBooleanv(GL_BLEND, &blendWasEnabled);
   GLboolean cullWasEnabled;
   glGetBooleanv(GL_CULL_FACE, &cullWasEnabled);
   GreedyTransparentDrawContext tctx{greedyBatches, vp, meshRevision, cullRev,
                                     camera->GetPosition(),
                                     WorldInstance->GetBlockRegistry(), textures};
   GreedyTransparentPipeline::Draw(*this, tctx);
   if (cullWasEnabled) {
    glEnable(GL_CULL_FACE);
   } else {
    glDisable(GL_CULL_FACE);
   }
   if (!blendWasEnabled) {
    glDisable(GL_BLEND);
   }
  } else {
   const auto& blockInstances = WorldInstance->GetBlockRenderInstances();
   if (!useBatchCache || !blockBatchesValid_ || renderCount != cachedInstanceCount_
       || meshRevision != cachedMeshRevision_) {
    PrepareRenderBatchesFromBlocks(blockInstances, textures);
    cachedInstanceCount_ = renderCount;
    cachedMeshRevision_ = meshRevision;
    blockBatchesValid_ = true;
   }
   glm::mat4 dummy_mvp = camera->GetMvpMatrix();
   RenderBatches(dummy_mvp);
  }

  RenderSelectionOutline();
  RenderCreatures();

  // Active object preview disabled to avoid per-frame resource churn
 
  // Restore state
  if (cullFaceEnabled) {
      glEnable(GL_CULL_FACE);
  } else {
      glDisable(GL_CULL_FACE);
  }
  if (depthTestEnabled) {
      glEnable(GL_DEPTH_TEST);
  } else {
      glDisable(GL_DEPTH_TEST);
  }
  if (blendEnabled) {
      glEnable(GL_BLEND);
  } else {
      glDisable(GL_BLEND);
  }
 
  auto t_end = std::chrono::high_resolution_clock::now();
  DurationDrawSceneMks = std::chrono::duration<double, std::micro>(t_end-t_begin).count();
}

void GeometryEngine::ShowTransientMessage(const std::string& msg, double seconds)
{
 transientMessage_ = msg;
 transientMessageUntil_ = std::chrono::duration<double>(
     std::chrono::steady_clock::now().time_since_epoch()).count() + seconds;
}

void GeometryEngine::SetRenderSettings(const RenderSettings& settings)
{
 renderSettings_ = settings;
 blockBatchesValid_ = false;
 DestroyGreedyGpuBatches();
 DestroyFaceQuadBuffers();
}

void GeometryEngine::PrepareRenderBatchesFromBlocks(const std::vector<BlockInstance>& instances,
                                                    const std::map<size_t, TextureCube>& textures)
{
 renderBatches.clear();
 std::unordered_map<size_t, RenderBatch> batchMap;

 for (const auto& instance : instances) {
  const size_t textureId = static_cast<size_t>(instance.id);
  auto& batch = batchMap[textureId];
  batch.blockTypeId = textureId;
  if (batch.textureID == 0) {
   const auto texIt = textures.find(textureId);
   if (texIt == textures.end()) {
    continue;
   }
   batch.textureID = texIt->second.GetTextureId();
  }
  batch.modelMatrices.push_back(instance.model);
  batch.faceIndices.push_back(static_cast<float>(instance.faceIndex));
  batch.quadSizes.push_back(instance.quadSize);
 }

 for (auto& pair : batchMap) {
  renderBatches.push_back(std::move(pair.second));
 }
}

void GeometryEngine::RenderBatches(const glm::mat4& mvp_matrix)
{
 for (const auto& batch : renderBatches) {
     DrawBatch(batch, mvp_matrix);
 }
}

void GeometryEngine::DrawBatch(const RenderBatch& batch, const glm::mat4& mvp_matrix)
{
 if (batch.modelMatrices.empty() && batch.objects.empty()) {
     if (verboseLogging) std::cout << "DrawBatch: Empty batch, skipping" << std::endl;
     return;
 }
 
 if (verboseLogging) std::cout << "DrawBatch: Drawing " << batch.modelMatrices.size() << " objects" << std::endl;
 
 glBindTexture(GL_TEXTURE_2D, batch.textureID);

 std::vector<glm::mat4> instanceMVPs;
 auto camera = WorldInstance->GetCurrentUserCamera();
 if (!camera) return;

 if (!batch.objects.empty()) {
  instanceMVPs.reserve(batch.objects.size());
  for (size_t i = 0; i < batch.cubeIndices.size(); ++i) {
   auto& object = batch.objects[i];
   if (!object) continue;
   size_t cubeIdx = batch.cubeIndices[i];
   if (cubeIdx >= object->GetCubes().size()) continue;
   auto& cube = object->GetCubes()[cubeIdx];
   glm::mat4 model = object->GetPose() * cube->GetInitialPose();
   glm::mat4 mvp = camera->GetProjection() * camera->GetViewMatrix() * model;
   instanceMVPs.push_back(mvp);
  }
 } else {
  instanceMVPs.reserve(batch.modelMatrices.size());
  for (const auto& model : batch.modelMatrices) {
   instanceMVPs.push_back(camera->GetProjection() * camera->GetViewMatrix() * model);
  }
 }

 const bool isBlockBatch = batch.objects.empty() && !batch.modelMatrices.empty();
 const bool drawFaceQuads = isBlockBatch && renderSettings_.UseFaceQuadDraw()
     && batch.faceIndices.size() == batch.modelMatrices.size();
 const GLsizei indexCount = drawFaceQuads ? 6 : 36;
 GLuint vao = drawFaceQuads ? faceVAO : cubeVAO;

 std::shared_ptr<ShaderProgram> activeShader = instancedShader;
 if (drawFaceQuads) {
  if (faceVAO == 0 && !InitFaceQuadBuffers()) {
   return;
  }
  vao = faceVAO;
  activeShader = instancedFaceShader;
 } else if (!instancedShader) {
  return;
 }

 if (!activeShader || !activeShader->IsValid()) {
  return;
 }

 activeShader->Use();
 activeShader->SetInt("texture0", 0);
 if (drawFaceQuads && batch.blockTypeId != 0 && TextureCubeStorageInstance) {
  SetBlockAnimUniforms(activeShader, static_cast<BlockId>(batch.blockTypeId),
                      TextureCubeStorageInstance->GetTextures());
 }

 if (drawFaceQuads) {
  struct BlockDrawInstance {
   glm::mat4 model;
   float faceIndex;
   float pad[3];
  };
  std::vector<BlockDrawInstance> blockInstances;
  blockInstances.reserve(batch.modelMatrices.size());
  const glm::mat4 vp = camera->GetProjection() * camera->GetViewMatrix();
  activeShader->SetMat4("uVP", vp);
  for (size_t i = 0; i < batch.modelMatrices.size(); ++i) {
   BlockDrawInstance inst;
   inst.model = batch.modelMatrices[i];
   inst.faceIndex = batch.faceIndices[i];
   inst.pad[0] = inst.pad[1] = inst.pad[2] = 0.0f;
   blockInstances.push_back(inst);
  }
  glBindBuffer(GL_ARRAY_BUFFER, instanceBlockVBO);
  glBufferData(GL_ARRAY_BUFFER, blockInstances.size() * sizeof(BlockDrawInstance),
               blockInstances.data(), GL_DYNAMIC_DRAW);
 } else {
  glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
  glBufferData(GL_ARRAY_BUFFER, instanceMVPs.size() * sizeof(glm::mat4), instanceMVPs.data(), GL_DYNAMIC_DRAW);
 }

 glBindVertexArray(vao);
 const GLsizei instanceCount = drawFaceQuads
     ? static_cast<GLsizei>(batch.modelMatrices.size())
     : static_cast<GLsizei>(instanceMVPs.size());
 if (instanceCount > 0) {
  glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0, instanceCount);
 }
 glBindVertexArray(0);
 glBindBuffer(GL_ARRAY_BUFFER, 0);

 activeShader->Unuse();
}

void GeometryEngine::DestroyGreedyGpuPassCache(GreedyGpuPassCache& cache)
{
 for (GreedyGpuBatch& batch : cache.batches) {
  if (batch.ebo) {
   glDeleteBuffers(1, &batch.ebo);
   batch.ebo = 0;
  }
  if (batch.vbo) {
   glDeleteBuffers(1, &batch.vbo);
   batch.vbo = 0;
  }
 }
 cache.batches.clear();
 cache.meshRevision = 0;
 cache.cullRevision = 0;
 cache.sortRevision = 0;
}

void GeometryEngine::DestroyGreedyGpuBatches()
{
 DestroyGreedyGpuPassCache(greedyGpuOpaque_);
 DestroyGreedyGpuPassCache(greedyGpuTransparent_);
}

void GeometryEngine::RefreshGreedyGpuBatches(
    const std::vector<GreedyMeshBatch>& batches,
    uint64_t meshRevision,
    uint64_t cullRevision,
    GreedyGpuPassCache& cache,
    uint64_t sortRevision)
{
 if (meshRevision == cache.meshRevision && cullRevision == cache.cullRevision
     && sortRevision == cache.sortRevision) {
  return;
 }

 DestroyGreedyGpuPassCache(cache);
 cache.batches.reserve(batches.size());

 for (const GreedyMeshBatch& batch : batches) {
  if (batch.vertices.empty() || batch.indices.empty()) {
   continue;
  }
  GreedyGpuBatch gpu;
  gpu.blockId = batch.blockId;
  gpu.indexCount = static_cast<GLsizei>(batch.indices.size());
  glGenBuffers(1, &gpu.vbo);
  glGenBuffers(1, &gpu.ebo);
  glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
  glBufferData(GL_ARRAY_BUFFER,
               batch.vertices.size() * sizeof(GreedyMeshVertex),
               batch.vertices.data(),
               GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               batch.indices.size() * sizeof(uint32_t),
               batch.indices.data(),
               GL_STATIC_DRAW);
  cache.batches.push_back(gpu);
 }

 glBindBuffer(GL_ARRAY_BUFFER, 0);
 glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
 cache.meshRevision = meshRevision;
 cache.cullRevision = cullRevision;
 cache.sortRevision = sortRevision;
}

void GeometryEngine::SetBlockAnimUniforms(const std::shared_ptr<ShaderProgram>& shader,
                                          BlockId blockId,
                                          const std::map<size_t, TextureCube>& textures)
{
 int frameCount = 1;
 const auto texIt = textures.find(static_cast<size_t>(blockId));
 if (texIt != textures.end()) {
  frameCount = static_cast<int>(texIt->second.GetNumTextureFrames());
 }
 if (frameCount < 1) {
  frameCount = 1;
 }
 const int frame = animationClock_.CurrentFrame() % frameCount;
 shader->SetInt("uAnimFrame", frame);
 shader->SetInt("uAnimFrameCount", frameCount);
}

void GeometryEngine::PrepareFrameRendering()
{
 auto camera = WorldInstance->GetCurrentUserCamera();
 if (!camera) {
  return;
 }
 const World::SampledFluidState fluid =
     WorldInstance->SampleFluidPhysics(camera->GetPosition(), camera->GetPlayerCapsule());
 BlockId eyeFluid = BLOCK_AIR;
 const bool cameraInFluid =
     WorldInstance->IsCameraInsideFluid(camera->GetPosition(), &eyeFluid);

 glm::vec3 targetSky = baseSkyColor_;
 fogEnabled_ = 0.0f;
 overlayTintAlpha_ = 0.0f;
 overlayBlockId_ = BLOCK_AIR;

 const BlockRegistry& registry = WorldInstance->GetBlockRegistry();
 if (cameraInFluid) {
  if (const FluidViewProfile* fv = registry.GetFluidView(eyeFluid)) {
   if (registry.GetRenderStyle(eyeFluid) == BlockRenderStyle::Fluid) {
    fogEnabled_ = 1.0f;
    fogStart_ = fv->fogStart;
    fogEnd_ = fv->fogEnd;
    fogMinBlend_ = fv->fogMinBlend;
    smoothedFogColor_ = glm::mix(smoothedFogColor_, fv->fogColor, 0.15f);
    targetSky = fv->fogColor;
   }
  }
 }
 if (fluid.inFluid) {
  if (const FluidViewProfile* fv = registry.GetFluidView(fluid.dominantFluid)) {
   if (fv->overlayAlpha > 0.01f
       && registry.GetRenderStyle(fluid.dominantFluid) == BlockRenderStyle::Cross) {
    overlayTintAlpha_ = fv->overlayAlpha;
    overlayTintColor_ = fv->overlayColor;
    overlayBlockId_ = fluid.dominantFluid;
   }
  }
 }

 smoothedSkyTint_ = glm::mix(smoothedSkyTint_, targetSky, 0.15f);
 skyColor = glm::vec4(smoothedSkyTint_, 1.0f);
}

void GeometryEngine::ApplyFluidFogUniforms(const std::shared_ptr<ShaderProgram>& shader,
                                         const glm::vec3& cameraPos)
{
 shader->SetVec3("uCameraPos", cameraPos);
 shader->SetVec3("uFogColor", smoothedFogColor_);
 shader->SetFloat("uFogStart", fogStart_);
 shader->SetFloat("uFogEnd", fogEnd_);
 shader->SetFloat("uFogMinBlend", fogMinBlend_);
 shader->SetFloat("uFogEnabled", fogEnabled_);
}

void GeometryEngine::SetGreedyShaderMode(const std::shared_ptr<ShaderProgram>& shader,
                                         bool transparentPass,
                                         GreedyShaderMode mode,
                                         float shellAlphaThreshold)
{
 shader->SetInt("uAlphaCutout", transparentPass ? 1 : 0);
 if (transparentPass) {
  shader->SetInt("uGreedyShaderMode", GreedyShaderModeToUniform(mode));
  shader->SetFloat("uShellAlphaThreshold", shellAlphaThreshold);
 } else {
  shader->SetInt("uGreedyShaderMode", 0);
  shader->SetFloat("uShellAlphaThreshold", 0.0f);
 }
}

void GeometryEngine::DrawGreedyGpuBatches(
    const GreedyGpuPassCache& cache,
    const glm::mat4& vp,
    const std::map<size_t, TextureCube>& textures,
    bool transparentPass,
    GreedyShaderMode mode,
    float shellAlphaThreshold)
{
 if (cache.batches.empty()) {
  return;
 }
 if (greedyMeshVAO == 0 && !InitGreedyMeshBuffers()) {
  return;
 }
 if (!greedyShader || !greedyShader->IsValid()) {
  return;
 }

 greedyShader->Use();
 greedyShader->SetMat4("mvp_matrix", vp);
 greedyShader->SetInt("texture0", 0);
 SetGreedyShaderMode(greedyShader, transparentPass, mode, shellAlphaThreshold);
 if (auto camera = WorldInstance->GetCurrentUserCamera()) {
  ApplyFluidFogUniforms(greedyShader, camera->GetPosition());
 }
 glActiveTexture(GL_TEXTURE0);

 glBindVertexArray(greedyMeshVAO);
 const GLsizei kStride = static_cast<GLsizei>(sizeof(GreedyMeshVertex));
 for (const GreedyGpuBatch& gpu : cache.batches) {
  SetBlockAnimUniforms(greedyShader, gpu.blockId, textures);
  if (gpu.indexCount <= 0) {
   continue;
  }
  const auto texIt = textures.find(static_cast<size_t>(gpu.blockId));
  if (texIt == textures.end()) {
   continue;
  }
  const GLuint textureId = texIt->second.GetTextureId();
  if (textureId == 0) {
   continue;
  }
  glBindTexture(GL_TEXTURE_2D, textureId);
  glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ebo);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride, (void*)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, kStride,
                        (void*)(offsetof(GreedyMeshVertex, faceIndex)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, kStride,
                        (void*)(offsetof(GreedyMeshVertex, u)));
  glEnableVertexAttribArray(2);
  glDrawElements(GL_TRIANGLES, gpu.indexCount, GL_UNSIGNED_INT, nullptr);
 }

 glBindVertexArray(0);
 glBindBuffer(GL_ARRAY_BUFFER, 0);
 glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
 greedyShader->Unuse();
}

void GeometryEngine::DrawGreedyOpaqueBatches(
    const std::vector<GreedyMeshBatch>& batches,
    const glm::mat4& vp,
    const std::map<size_t, TextureCube>& textures,
    uint64_t meshRevision,
    uint64_t cullRevision)
{
 std::vector<GreedyMeshBatch> filtered;
 filtered.reserve(batches.size());
 for (const GreedyMeshBatch& batch : batches) {
  if (!batch.transparent) {
   filtered.push_back(batch);
  }
 }
 if (filtered.empty()) {
  return;
 }
 RefreshGreedyGpuBatches(filtered, meshRevision, cullRevision, greedyGpuOpaque_, 0);
 DrawGreedyGpuBatches(greedyGpuOpaque_, vp, textures, false,
                      GreedyShaderMode::TransparentColor, 0.0f);
}

void GeometryEngine::PrepareTransparent(const GreedyTransparentDrawContext& ctx)
{
 std::vector<GreedyMeshBatch> filtered;
 filtered.reserve(ctx.allBatches.size());
 for (const GreedyMeshBatch& batch : ctx.allBatches) {
  if (batch.transparent) {
   filtered.push_back(batch);
  }
 }
 if (filtered.empty()) {
  greedyGpuTransparent_.batches.clear();
  preparedTransparentTextures_ = nullptr;
  return;
 }
 SortTransparentGreedyBatches(filtered, ctx.cameraPos, ctx.blockRegistry);
 const uint64_t sortRevision = GreedyTransparentSortRevision(ctx.cameraPos);
 RefreshGreedyGpuBatches(filtered, ctx.meshRevision, ctx.cullRevision,
                         greedyGpuTransparent_, sortRevision);
 preparedTransparentVp_ = ctx.viewProjection;
 preparedTransparentTextures_ = &ctx.textures;
}

void GeometryEngine::DrawPreparedTransparent(GreedyShaderMode mode, float shellAlpha)
{
 if (!preparedTransparentTextures_ || greedyGpuTransparent_.batches.empty()) {
  return;
 }
 DrawGreedyGpuBatches(greedyGpuTransparent_, preparedTransparentVp_,
                      *preparedTransparentTextures_, true, mode, shellAlpha);
}

bool GeometryEngine::InitGreedyMeshBuffers()
{
 if (greedyMeshVAO != 0) {
  return true;
 }

 glGenVertexArrays(1, &greedyMeshVAO);
 glGenBuffers(1, &greedyMeshVBO);
 glGenBuffers(1, &greedyMeshEBO);

 glBindVertexArray(greedyMeshVAO);
 glBindBuffer(GL_ARRAY_BUFFER, greedyMeshVBO);
 glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, greedyMeshEBO);

 constexpr GLsizei kStride = static_cast<GLsizei>(sizeof(GreedyMeshVertex));
 glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride, (void*)0);
 glEnableVertexAttribArray(0);
 glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, kStride,
                       (void*)(offsetof(GreedyMeshVertex, faceIndex)));
 glEnableVertexAttribArray(1);
 glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, kStride,
                       (void*)(offsetof(GreedyMeshVertex, u)));
 glEnableVertexAttribArray(2);

 glBindVertexArray(0);
 return greedyMeshVAO != 0;
}

void GeometryEngine::DestroyGreedyMeshBuffers()
{
 DestroyGreedyGpuBatches();
 if (greedyMeshEBO) { glDeleteBuffers(1, &greedyMeshEBO); greedyMeshEBO = 0; }
 if (greedyMeshVBO) { glDeleteBuffers(1, &greedyMeshVBO); greedyMeshVBO = 0; }
 if (greedyMeshVAO) { glDeleteVertexArrays(1, &greedyMeshVAO); greedyMeshVAO = 0; }
}

void GeometryEngine::DrawCube(std::shared_ptr<Cube> icube, GLuint texture)
{
 auto cube = std::dynamic_pointer_cast<CubeGL>(icube);
 if(!cube) {
     std::cout << "DrawCube: Failed to cast to CubeGL" << std::endl;
     return;
 }
 
 // debug removed

 glBindTexture(GL_TEXTURE_2D, texture);
 defaultShader->Use();
 defaultShader->SetInt("texture0", 0);


 // Tell OpenGL which VBOs to use
 if (cubeDrawVAO == 0) {
     glGenVertexArrays(1, &cubeDrawVAO);
 }
 glBindVertexArray(cubeDrawVAO);
 glBindBuffer(GL_ARRAY_BUFFER, cube->GetArrayBuf());
 glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cube->GetIndexBuf());

 // Offset for position
 size_t offset = 0;

 // Tell OpenGL programmable pipeline how to locate vertex position data
 int vertexLocation = glGetAttribLocation(defaultShader->GetProgramID(), "aPos");
 if (vertexLocation >= 0) {
     glEnableVertexAttribArray(vertexLocation);
     glVertexAttribPointer(vertexLocation, 3, GL_FLOAT, GL_FALSE, sizeof(VertexData), (void*)offset);
 } else {
     defaultShader->Unuse();
     glBindVertexArray(0);
     return;
 }

 // Offset for texture coordinate
 offset += sizeof(glm::vec3);

 // Tell OpenGL programmable pipeline how to locate vertex texture coordinate data
 int texcoordLocation = glGetAttribLocation(defaultShader->GetProgramID(), "aTexCoord");
 if (texcoordLocation >= 0) {
     glEnableVertexAttribArray(texcoordLocation);
     glVertexAttribPointer(texcoordLocation, 2, GL_FLOAT, GL_FALSE, sizeof(VertexData), (void*)offset);
 } else {
     // still draw without UVs would be pointless; abort safe
     defaultShader->Unuse();
     glBindVertexArray(0);
     return;
 }

 // Draw cube geometry using indices from VBO 1
 glDrawElements(GL_TRIANGLE_STRIP, int(std::dynamic_pointer_cast<CubeGL>(cube)->GetIndices().size()), GL_UNSIGNED_SHORT, nullptr);
 
 glBindBuffer(GL_ARRAY_BUFFER, 0);
 glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
 glBindVertexArray(0);

 defaultShader->Unuse();
}

void GeometryEngine::DrawObject(std::shared_ptr<Object> object, const std::map<size_t, TextureCube>& textures)
{
 for(size_t i=0; i<object->GetCubes().size(); i++)
 {
  auto & cube = object->GetCubes()[i];
  GLuint texture = textures.at(cube->GetTypeId()).GetTextureId();
  DrawCube(cube, texture);
 }

}

void GeometryEngine::DrawSkyGradient()
{
 // Use simple version which is more reliable
 DrawSkyGradientSimple();
}

void GeometryEngine::DrawSkyGradientSimple()
{
 // Check that sky shader is ready
 if (!skyShader->IsValid()) {
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
 
 // Create simple rectangle for sky (full screen)
static const GLfloat skyVertices[] = {
    // Positions      // Texture coordinates
     -1.0f, -1.0f, 0.0f,  0.0f, 0.0f,
      1.0f, -1.0f, 0.0f,  1.0f, 0.0f,
      1.0f,  1.0f, 0.0f,  1.0f, 1.0f,
     -1.0f,  1.0f, 0.0f,  0.0f, 1.0f
 };
 
 // Create temporary VBO for rendering
 GLuint tempVBO;
 glGenBuffers(1, &tempVBO);
 glBindBuffer(GL_ARRAY_BUFFER, tempVBO);
 glBufferData(GL_ARRAY_BUFFER, sizeof(skyVertices), skyVertices, GL_STATIC_DRAW);
 
 // Set attributes
 int vertexLocation = glGetAttribLocation(skyShader->GetProgramID(), "a_position");
 if (vertexLocation != -1) {
     glEnableVertexAttribArray(vertexLocation);
     glVertexAttribPointer(vertexLocation, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void*)0);
 }
 
 int texcoordLocation = glGetAttribLocation(skyShader->GetProgramID(), "a_texcoord");
 if (texcoordLocation != -1) {
     glEnableVertexAttribArray(texcoordLocation);
     glVertexAttribPointer(texcoordLocation, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void*)(3 * sizeof(GLfloat)));
 }
 
 // Render sky as triangles
 glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
 
 // Check OpenGL errors
 GLenum error = glGetError();
 if (error != GL_NO_ERROR) {
     std::cerr << "OpenGL error after drawing sky: " << error << std::endl;
 }
 
 // Free resources
 glBindBuffer(GL_ARRAY_BUFFER, 0);
 glDeleteBuffers(1, &tempVBO);
 skyShader->Unuse();
 
 // Enable depth test back
 glEnable(GL_DEPTH_TEST);
}

bool GeometryEngine::InitOverlayBuffers()
{
 if (overlayVAO != 0) {
  return true;
 }
 const float quad[] = {
     -1.0f, -1.0f, 0.0f, 0.0f,
      1.0f, -1.0f, 1.0f, 0.0f,
      1.0f,  1.0f, 1.0f, 1.0f,
     -1.0f,  1.0f, 0.0f, 1.0f,
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
 glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
 glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
 glEnableVertexAttribArray(0);
 glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
 glEnableVertexAttribArray(1);
 glBindVertexArray(0);
 return true;
}

void GeometryEngine::DestroyOverlayBuffers()
{
 if (overlayVBO) {
  glDeleteBuffers(1, &overlayVBO);
  overlayVBO = 0;
 }
 if (overlayVAO) {
  glDeleteVertexArrays(1, &overlayVAO);
  overlayVAO = 0;
 }
}

void GeometryEngine::RenderFluidOverlay(int width, int height)
{
 (void)width;
 (void)height;
 if (!overlayShader || overlayBlockId_ == BLOCK_AIR || !TextureCubeStorageInstance) {
  return;
 }
 if (!InitOverlayBuffers()) {
  return;
 }
 const auto textures = TextureCubeStorageInstance->GetTextures();
 const auto texIt = textures.find(static_cast<size_t>(overlayBlockId_));
 if (texIt == textures.end() || texIt->second.GetTextureId() == 0) {
  return;
 }

 GLboolean depthTestEnabled;
 glGetBooleanv(GL_DEPTH_TEST, &depthTestEnabled);
 GLboolean blendEnabled;
 glGetBooleanv(GL_BLEND, &blendEnabled);

 glDisable(GL_DEPTH_TEST);
 glEnable(GL_BLEND);
 glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

 overlayShader->Use();
 overlayShader->SetInt("texture0", 0);
 SetBlockAnimUniforms(overlayShader, overlayBlockId_, textures);
 overlayShader->SetVec3("uTintColor", overlayTintColor_);
 overlayShader->SetFloat("uTintAlpha", overlayTintAlpha_);
 glActiveTexture(GL_TEXTURE0);
 glBindTexture(GL_TEXTURE_2D, texIt->second.GetTextureId());
 glBindVertexArray(overlayVAO);
 glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
 glBindVertexArray(0);
 overlayShader->Unuse();

 if (depthTestEnabled) {
  glEnable(GL_DEPTH_TEST);
 } else {
  glDisable(GL_DEPTH_TEST);
 }
 if (!blendEnabled) {
  glDisable(GL_BLEND);
 }
}

// Methods for sky color management
void GeometryEngine::SetSkyColor(float r, float g, float b, float a)
{
 baseSkyColor_ = glm::vec3(r, g, b);
 smoothedSkyTint_ = baseSkyColor_;
 skyColor = glm::vec4(r, g, b, a);
}

void GeometryEngine::SetSkyColor(const glm::vec4& color)
{
 baseSkyColor_ = glm::vec3(color);
 smoothedSkyTint_ = baseSkyColor_;
 skyColor = color;
}

glm::vec4 GeometryEngine::GetSkyColor() const
{
 return skyColor;
}

void GeometryEngine::SetGradientSky(bool useGradient)
{
 useGradientSky = useGradient;
}

bool GeometryEngine::IsGradientSky() const
{
 return useGradientSky;
}

void GeometryEngine::RenderPerformanceText(int width_size, int height_size, double view_duration)
{
    if (!textRenderer) {
        return;
    }
    
    // Обновляем размеры окна в TextRenderer
    textRenderer->SetWindowSize(width_size, height_size);
    
    float scale = 0.7f;
    glm::vec3 textColor(1.0f, 1.0f, 0.0f); // Yellow color for performance
    
    // Calculate FPS
    double totalTime = DurationDrawSceneMks + WorldInstance->GetDurationDoMovementMks() + view_duration;
    double fps = totalTime > 0 ? 1000000.0 / totalTime : 0.0;
    
    size_t blockCount = WorldInstance->GetCachedBlockCount();
    size_t drawCount = WorldInstance->GetRenderInstanceCount();
    
    // Form performance information strings
    std::vector<std::string> performanceLines = {
        "Performance:",
        "FPS: " + std::to_string(fps).substr(0, 6),
        "Blocks: " + std::to_string(blockCount) + " draw: " + std::to_string(drawCount),
        "Scene: " + std::to_string(DurationDrawSceneMks/1000.0).substr(0, 6) + " ms",
        "Movement: " + std::to_string(WorldInstance->GetDurationDoMovementMks()/1000.0).substr(0, 6) + " ms",
        "View: " + std::to_string(view_duration/1000.0).substr(0, 6) + " ms"
    };

    const auto& md = WorldInstance->GetMovementDiagnostics();
    if (md.hitchDetected || md.fallThroughSuspected || md.streamingUnloads > 0) {
        performanceLines.push_back(
            "Dt: " + std::to_string(md.deltaTime).substr(0, 5) +
            " yDrop: " + std::to_string(md.playerYDrop).substr(0, 5));
        performanceLines.push_back(
            std::string("Feet chunk: ") + (md.feetChunkLoaded ? "OK" : "MISSING") +
            (md.feetIsAir ? " AIR" : " SOLID") +
            " unloads: " + std::to_string(md.streamingUnloads));
        if (md.fallThroughSuspected) {
            performanceLines.emplace_back("!! fall-through suspected !!");
        }
    }
    
    // Display text in top right corner
    float y = height_size - 30.0f;
    for (const auto& line : performanceLines) {
        // Calculate position for right alignment
        glm::vec2 textSize = textRenderer->GetTextSize(line, scale);
        float x = width_size - textSize.x - 10.0f; // 10 pixel margin from right edge
        
        textRenderer->RenderText(line, x, y, scale, textColor);
        y -= 18.0f; // Margin between lines
    }
}

void GeometryEngine::RenderCrosshair(int width_size, int height_size)
{
    if (!uiShader || !uiShader->IsValid()) {
        std::cerr << "UI shader is not valid" << std::endl;
        return;
    }
    
    // Сохраняем состояние OpenGL
    GLboolean depthTestEnabled;
    glGetBooleanv(GL_DEPTH_TEST, &depthTestEnabled);
    GLboolean blendEnabled;
    glGetBooleanv(GL_BLEND, &blendEnabled);
    
    // Отключаем тест глубины для 2D рендеринга
    glDisable(GL_DEPTH_TEST);
    
    // Используем UI шейдер
    uiShader->Use();
    
    // Set screen size
    uiShader->SetVec2("screenSize", glm::vec2(width_size, height_size));
    
    // Set yellow color for crosshair
uiShader->SetVec4("color", glm::vec4(1.0f, 1.0f, 0.0f, 1.0f)); // Yellow color
    
    // Crosshair dimensions
int crosshairSize = 20; // Size in pixels
int lineThickness = 2;  // Line thickness in pixels
    
    // Screen center
    int centerX = width_size / 2;
    int centerY = height_size / 2;
    
    // Create data for horizontal line
    float horizontalLine[] = {
        centerX - crosshairSize, centerY - lineThickness/2,  // Left point
centerX + crosshairSize, centerY - lineThickness/2,  // Right point
centerX - crosshairSize, centerY + lineThickness/2,  // Left point (bottom)
centerX + crosshairSize, centerY + lineThickness/2   // Right point (bottom)
    };
    
    // Create data for vertical line
    float verticalLine[] = {
        centerX - lineThickness/2, centerY - crosshairSize,  // Top point
centerX + lineThickness/2, centerY - crosshairSize,  // Top point (right)
centerX - lineThickness/2, centerY + crosshairSize,  // Bottom point
centerX + lineThickness/2, centerY + crosshairSize   // Bottom point (right)
    };
    
    // Create VAO and VBO for horizontal line
    GLuint horizontalVAO, horizontalVBO;
    glGenVertexArrays(1, &horizontalVAO);
    glGenBuffers(1, &horizontalVBO);
    
    glBindVertexArray(horizontalVAO);
    glBindBuffer(GL_ARRAY_BUFFER, horizontalVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(horizontalLine), horizontalLine, GL_STATIC_DRAW);
    
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    // Draw horizontal line
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    // Create VAO and VBO for vertical line
    GLuint verticalVAO, verticalVBO;
    glGenVertexArrays(1, &verticalVAO);
    glGenBuffers(1, &verticalVBO);
    
    glBindVertexArray(verticalVAO);
    glBindBuffer(GL_ARRAY_BUFFER, verticalVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verticalLine), verticalLine, GL_STATIC_DRAW);
    
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    // Draw vertical line
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    // Очищаем ресурсы
    glDeleteVertexArrays(1, &horizontalVAO);
    glDeleteBuffers(1, &horizontalVBO);
    glDeleteVertexArrays(1, &verticalVAO);
    glDeleteBuffers(1, &verticalVBO);
    
    // Отключаем шейдер
    uiShader->Unuse();
    
    // Восстанавливаем состояние OpenGL
    if (depthTestEnabled) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    
    if (blendEnabled) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
         }
 }
 
  void GeometryEngine::RenderSimpleText(int width_size, int height_size)
 {
     if (!textRenderer || !WorldInstance) {
         if (!textRenderer) {
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
     textRenderer->RenderTextCentered(header, static_cast<float>(height_size) - 28.0f, scale,
                                      headerColor);

     const std::vector<std::string> helpLines = {
         "WASD - Movement",
         "Space - Jump / Fly up",
         "Shift - Crouch / Fly down",
         "2xSpace - Toggle flight",
         "0-9 - Block, Alt+0-9 - Object",
         "LMB - Place block, Alt+LMB - Place object",
         "Hold LMB - Remove block, Delete - Remove",
         "Right Mouse - Camera, F1-F8 - Sky",
     };

     constexpr float helpX = 20.0f;
     float y = 20.0f;
     for (const std::string& line : helpLines) {
         textRenderer->RenderText(line, helpX, y, scale, helpColor);
         y += 20.0f;
     }

     const double now = std::chrono::duration<double>(
         std::chrono::steady_clock::now().time_since_epoch()).count();
     if (!transientMessage_.empty() && now < transientMessageUntil_) {
         textRenderer->RenderTextCentered(transientMessage_, 8.0f, 1.0f, statusColor);
     }
 }

void GeometryEngine::RenderHotbarLabels(int width_size, int height_size)
{
    if (!textRenderer || !WorldInstance) {
        return;
    }

    textRenderer->SetWindowSize(width_size, height_size);
    auto user = WorldInstance->GetCurrentUser();
    if (!user) {
        return;
    }

    constexpr int kPreviewSize = 80;
    constexpr int kPreviewMargin = 10;
    const float labelX = static_cast<float>(kPreviewMargin + kPreviewSize + 8);
    const float previewBottom = static_cast<float>(height_size - kPreviewSize - kPreviewMargin);
    const float labelScale = 0.75f;

    const float blockY = previewBottom + static_cast<float>(kPreviewSize) * 0.5f - 6.0f;
    std::string activeBlock;
    if (Creature* controlled = WorldInstance->GetControlledCreature()) {
        activeBlock = controlled->GetInventory().GetActiveBlockTypeName();
    }
    textRenderer->RenderText(activeBlock, labelX, blockY, labelScale,
                             glm::vec3(1.0f, 1.0f, 1.0f));

    std::string prefabName;
    if (Creature* controlled = WorldInstance->GetControlledCreature()) {
        prefabName = controlled->GetInventory().GetActivePrefabName();
    }
    if (!prefabName.empty()) {
        textRenderer->RenderText("Object: " + prefabName, static_cast<float>(kPreviewMargin),
                                 previewBottom - 18.0f, labelScale, glm::vec3(0.85f, 1.0f, 0.85f));
    }
}
 
void GeometryEngine::InitPreviewBuffers()
{
    // Create a simple cube for preview (smaller scale)
    float scale = 0.3f; // Smaller cube for preview
    float cube_shift = 1.0f/6.0f;
    float vertices[] = {
        // positions                   // texture coordinates
        // Face 0 (NEAR) - coordinates 0.0 - 1/6, V flipped for sides
        -scale, -scale,  scale,         0.0f, 1.0f,
         scale, -scale,  scale,         cube_shift*1.0f, 1.0f,
        -scale,  scale,  scale,         0.0f, 0.0f,
         scale,  scale,  scale,         cube_shift*1.0f, 0.0f,
        
        // Face 1 (RIGHT) - coordinates 1/6 - 2/6
         scale, -scale,  scale,         cube_shift*1.0f, 1.0f,
         scale, -scale, -scale,         cube_shift*2.0f, 1.0f,
         scale,  scale,  scale,         cube_shift*1.0f, 0.0f,
         scale,  scale, -scale,         cube_shift*2.0f, 0.0f,
          
        // Face 2 (FAR) - coordinates 2/6 - 3/6
         scale, -scale, -scale,         cube_shift*2.0f, 1.0f,
        -scale, -scale, -scale,         cube_shift*3.0f, 1.0f,
         scale,  scale, -scale,         cube_shift*2.0f, 0.0f,
        -scale,  scale, -scale,         cube_shift*3.0f, 0.0f,
          
        // Face 3 (LEFT) - coordinates 3/6 - 4/6
        -scale, -scale, -scale,         cube_shift*3.0f, 1.0f,
        -scale, -scale,  scale,         cube_shift*4.0f, 1.0f,
        -scale,  scale, -scale,         cube_shift*3.0f, 0.0f,
        -scale,  scale,  scale,         cube_shift*4.0f, 0.0f,
          
        // Face 4 (TOP) - coordinates 4/6 - 5/6
        -scale,  scale,  scale,         cube_shift*4.0f, 0.0f,
         scale,  scale,  scale,         cube_shift*5.0f, 0.0f,
        -scale,  scale, -scale,         cube_shift*4.0f, 1.0f,
         scale,  scale, -scale,         cube_shift*5.0f, 1.0f,
          
        // Face 5 (BOTTOM) - coordinates 5/6 - 1.0
        -scale, -scale, -scale,         cube_shift*5.0f, 0.0f,
         scale, -scale, -scale,         1.0f, 0.0f,
        -scale, -scale,  scale,         cube_shift*5.0f, 1.0f,
         scale, -scale,  scale,         1.0f, 1.0f
    };
    
    unsigned int indices[] = {
        0, 1, 2, 2, 1, 3,
        4, 5, 6, 6, 5, 7,
        8, 9, 10, 10, 9, 11,
        12, 13, 14, 14, 13, 15,
        16, 17, 18, 18, 17, 19,
        20, 21, 22, 22, 21, 23
    };
    
    glGenVertexArrays(1, &previewVAO);
    glGenBuffers(1, &previewVBO);
    glGenBuffers(1, &previewEBO);
    
    glBindVertexArray(previewVAO);
    glBindBuffer(GL_ARRAY_BUFFER, previewVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, previewEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    
    // Vertex attributes
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    
    // Load a default texture for preview (prefer "stone", else first available)
    if (TextureCubeStorageInstance) {
        const auto &texMap = TextureCubeStorageInstance->GetTextures();
        GLuint texId = 0;
        for (const auto &kv : texMap) {
            const TextureCube &tc = kv.second;
            if (tc.GetName() == std::string("stone")) {
                texId = tc.GetTexture();
                break;
            }
        }
        if (texId == 0 && !texMap.empty()) {
            texId = texMap.begin()->second.GetTexture();
        }
        previewTexture = texId;
    }
}

void GeometryEngine::DestroyPreviewBuffers()
{
    if (previewVAO) {
        glDeleteVertexArrays(1, &previewVAO);
        previewVAO = 0;
    }
    if (previewVBO) {
        glDeleteBuffers(1, &previewVBO);
        previewVBO = 0;
    }
    if (previewEBO) {
        glDeleteBuffers(1, &previewEBO);
        previewEBO = 0;
    }
    // Note: previewTexture is managed by TextureCubeStorage, don't delete it
}

void GeometryEngine::RenderActiveObjectPreview(int width_size, int height_size)
{
    if (!defaultShader || !defaultShader->IsValid() || !previewVAO) return;
    
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
    glm::mat4 view = glm::lookAt(
        glm::vec3(2.0f, 2.0f, 2.0f), // eye position
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
    
    // Pick texture by active block name if available
    GLuint texId = previewTexture;
    if (WorldInstance && TextureCubeStorageInstance) {
        std::string activeName;
        if (Creature* controlled = WorldInstance->GetControlledCreature()) {
            activeName = controlled->GetInventory().GetActiveBlockTypeName();
        }
        if (!activeName.empty()) {
            const auto& texMap = TextureCubeStorageInstance->GetTextures();
            for (const auto& kv : texMap) {
                const TextureCube& tc = kv.second;
                if (tc.GetName() == activeName) {
                    texId = tc.GetTexture();
                    break;
                }
            }
        }
    }
    
    // Bind chosen texture (fallback to previewTexture if not found)
    if (texId) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texId);
    }
    
    // Draw preview cube
    glBindVertexArray(previewVAO);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    
    // Restore viewport
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    
    defaultShader->Unuse();
}
 
// Initialize static cube geometry buffers
bool GeometryEngine::InitCubeBuffers()
{
    if (cubeVAO != 0) return true;

    float cube_shift = 1.0f/6.0f;
    const float vertices[] = {
        // positions              // texcoords
        // Face 0 (NEAR) — V flipped for side faces (Y maps to texture V)
        -0.5f, -0.5f,  0.5f,     0.0f,              1.0f,
         0.5f, -0.5f,  0.5f,     cube_shift*1.0f,   1.0f,
        -0.5f,  0.5f,  0.5f,     0.0f,              0.0f,
         0.5f,  0.5f,  0.5f,     cube_shift*1.0f,   0.0f,

        // Face 1 (RIGHT)
         0.5f, -0.5f,  0.5f,     cube_shift*1.0f,   1.0f,
         0.5f, -0.5f, -0.5f,     cube_shift*2.0f,   1.0f,
         0.5f,  0.5f,  0.5f,     cube_shift*1.0f,   0.0f,
         0.5f,  0.5f, -0.5f,     cube_shift*2.0f,   0.0f,

        // Face 2 (FAR)
         0.5f, -0.5f, -0.5f,     cube_shift*2.0f,   1.0f,
        -0.5f, -0.5f, -0.5f,     cube_shift*3.0f,   1.0f,
         0.5f,  0.5f, -0.5f,     cube_shift*2.0f,   0.0f,
        -0.5f,  0.5f, -0.5f,     cube_shift*3.0f,   0.0f,

        // Face 3 (LEFT)
        -0.5f, -0.5f, -0.5f,     cube_shift*3.0f,   1.0f,
        -0.5f, -0.5f,  0.5f,     cube_shift*4.0f,   1.0f,
        -0.5f,  0.5f, -0.5f,     cube_shift*3.0f,   0.0f,
        -0.5f,  0.5f,  0.5f,     cube_shift*4.0f,   0.0f,

        // Face 4 (TOP)
        -0.5f,  0.5f,  0.5f,     cube_shift*4.0f,   0.0f,
         0.5f,  0.5f,  0.5f,     cube_shift*5.0f,   0.0f,
        -0.5f,  0.5f, -0.5f,     cube_shift*4.0f,   1.0f,
         0.5f,  0.5f, -0.5f,     cube_shift*5.0f,   1.0f,

        // Face 5 (BOTTOM)
        -0.5f, -0.5f, -0.5f,     cube_shift*5.0f,   0.0f,
         0.5f, -0.5f, -0.5f,     1.0f,              0.0f,
        -0.5f, -0.5f,  0.5f,     cube_shift*5.0f,   1.0f,
         0.5f, -0.5f,  0.5f,     1.0f,              1.0f
    };

    const unsigned int indices[] = {
        0, 1, 2, 2, 1, 3,
        4, 5, 6, 6, 5, 7,
        8, 9, 10, 10, 9, 11,
        12, 13, 14, 14, 13, 15,
        16, 17, 18, 18, 17, 19,
        20, 21, 22, 22, 21, 23
    };

    glGenVertexArrays(1, &cubeVAO);
    glGenBuffers(1, &cubeVBO);
    glGenBuffers(1, &cubeEBO);
    glGenBuffers(1, &instanceVBO);

    glBindVertexArray(cubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // Attributes: position (0), texcoord (1)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Instance attribute: mat4 (locations 2,3,4,5)
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    std::size_t vec4Size = sizeof(glm::vec4);
    for (int i = 0; i < 4; ++i) {
        glVertexAttribPointer(2 + i, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void*)(i * vec4Size));
        glEnableVertexAttribArray(2 + i);
        glVertexAttribDivisor(2 + i, 1);
    }

    glBindVertexArray(0);
    return cubeVAO != 0;
}

bool GeometryEngine::InitFaceQuadBuffers()
{
    if (faceVAO != 0) {
        DestroyFaceQuadBuffers();
    }

    const float vertices[] = {
        0.0f, 0.0f, 0.0f,  0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,  1.0f, 0.0f,
        1.0f, 1.0f, 0.0f,  1.0f, 1.0f,
        0.0f, 1.0f, 0.0f,  0.0f, 1.0f,
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
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    constexpr std::size_t kStride = sizeof(glm::mat4) + sizeof(float) * 4;
    glBindBuffer(GL_ARRAY_BUFFER, instanceBlockVBO);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    std::size_t vec4Size = sizeof(glm::vec4);
    for (int i = 0; i < 4; ++i) {
        glVertexAttribPointer(2 + i, 4, GL_FLOAT, GL_FALSE, kStride, (void*)(i * vec4Size));
        glEnableVertexAttribArray(2 + i);
        glVertexAttribDivisor(2 + i, 1);
    }
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, kStride, (void*)sizeof(glm::mat4));
    glEnableVertexAttribArray(6);
    glVertexAttribDivisor(6, 1);

    glBindVertexArray(0);
    return faceVAO != 0;
}

void GeometryEngine::DestroyFaceQuadBuffers()
{
    if (instanceBlockVBO) { glDeleteBuffers(1, &instanceBlockVBO); instanceBlockVBO = 0; }
    if (faceEBO) { glDeleteBuffers(1, &faceEBO); faceEBO = 0; }
    if (faceVBO) { glDeleteBuffers(1, &faceVBO); faceVBO = 0; }
    if (faceVAO) { glDeleteVertexArrays(1, &faceVAO); faceVAO = 0; }
}

void GeometryEngine::DestroyCubeBuffers()
{
    if (cubeEBO) { glDeleteBuffers(1, &cubeEBO); cubeEBO = 0; }
    if (cubeVBO) { glDeleteBuffers(1, &cubeVBO); cubeVBO = 0; }
    if (instanceVBO) { glDeleteBuffers(1, &instanceVBO); instanceVBO = 0; }
    if (cubeVAO) { glDeleteVertexArrays(1, &cubeVAO); cubeVAO = 0; }
}

bool GeometryEngine::InitOutlineBuffers()
{
    if (outlineVAO != 0) {
        return true;
    }

    constexpr float half = 0.5f + 0.005f;
    const float vertices[] = {
        -half, -half,  half,
         half, -half,  half,
        -half,  half,  half,
         half,  half,  half,
        -half, -half, -half,
         half, -half, -half,
        -half,  half, -half,
         half,  half, -half,
    };

    const unsigned int indices[] = {
        0, 1, 1, 3, 3, 2, 2, 0,
        4, 5, 5, 7, 7, 6, 6, 4,
        0, 4, 1, 5, 2, 6, 3, 7,
    };

    glGenVertexArrays(1, &outlineVAO);
    glGenBuffers(1, &outlineVBO);
    glGenBuffers(1, &outlineEBO);

    glBindVertexArray(outlineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, outlineVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, outlineEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
    return outlineVAO != 0;
}

void GeometryEngine::DestroyOutlineBuffers()
{
    if (outlineEBO) { glDeleteBuffers(1, &outlineEBO); outlineEBO = 0; }
    if (outlineVBO) { glDeleteBuffers(1, &outlineVBO); outlineVBO = 0; }
    if (outlineVAO) { glDeleteVertexArrays(1, &outlineVAO); outlineVAO = 0; }
}

namespace {

bool UploadCreaturePartMesh(GLuint& vao, GLuint& vbo, GLuint& ebo, const float* texCoords)
{
 float vertices[24 * 5];
 for (int v = 0; v < 24; ++v) {
  vertices[v * 5 + 0] = kCreaturePartPositions[v * 3 + 0];
  vertices[v * 5 + 1] = kCreaturePartPositions[v * 3 + 1];
  vertices[v * 5 + 2] = kCreaturePartPositions[v * 3 + 2];
  vertices[v * 5 + 3] = texCoords[v * 2 + 0];
  vertices[v * 5 + 4] = texCoords[v * 2 + 1];
 }

 if (vao == 0) {
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glGenBuffers(1, &ebo);
 }
 glBindVertexArray(vao);
 glBindBuffer(GL_ARRAY_BUFFER, vbo);
 glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
 glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
 glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kCreaturePartIndices), kCreaturePartIndices,
              GL_STATIC_DRAW);
 glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
 glEnableVertexAttribArray(0);
 glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
 glEnableVertexAttribArray(1);
 glBindVertexArray(0);
 return vao != 0;
}

} // namespace

bool GeometryEngine::InitCreaturePartBuffers()
{
 if (creaturePartVAO != 0) {
  return true;
 }
 float texCoords[48];
 BuildCreatureBoxTexCoords(texCoords);
 return UploadCreaturePartMesh(creaturePartVAO, creaturePartVBO, creaturePartEBO, texCoords);
}

bool GeometryEngine::InitCreatureHeadPartBuffers()
{
 if (creatureHeadPartVAO != 0) {
  return true;
 }
 float texCoords[48];
 BuildCreatureHeadTexCoords(texCoords);
 return UploadCreaturePartMesh(creatureHeadPartVAO, creatureHeadPartVBO, creatureHeadPartEBO,
                              texCoords);
}

bool GeometryEngine::InitCreatureBodyPartBuffers()
{
 if (creatureBodyPartVAO != 0) {
  return true;
 }
 float texCoords[48];
 BuildCreatureBodyTexCoords(texCoords);
 return UploadCreaturePartMesh(creatureBodyPartVAO, creatureBodyPartVBO, creatureBodyPartEBO,
                              texCoords);
}

void GeometryEngine::DestroyCreaturePartBuffers()
{
 if (creatureBodyPartEBO) {
  glDeleteBuffers(1, &creatureBodyPartEBO);
  creatureBodyPartEBO = 0;
 }
 if (creatureBodyPartVBO) {
  glDeleteBuffers(1, &creatureBodyPartVBO);
  creatureBodyPartVBO = 0;
 }
 if (creatureBodyPartVAO) {
  glDeleteVertexArrays(1, &creatureBodyPartVAO);
  creatureBodyPartVAO = 0;
 }
 if (creatureHeadPartEBO) {
  glDeleteBuffers(1, &creatureHeadPartEBO);
  creatureHeadPartEBO = 0;
 }
 if (creatureHeadPartVBO) {
  glDeleteBuffers(1, &creatureHeadPartVBO);
  creatureHeadPartVBO = 0;
 }
 if (creatureHeadPartVAO) {
  glDeleteVertexArrays(1, &creatureHeadPartVAO);
  creatureHeadPartVAO = 0;
 }
 if (creaturePartEBO) {
  glDeleteBuffers(1, &creaturePartEBO);
  creaturePartEBO = 0;
 }
 if (creaturePartVBO) {
  glDeleteBuffers(1, &creaturePartVBO);
  creaturePartVBO = 0;
 }
 if (creaturePartVAO) {
  glDeleteVertexArrays(1, &creaturePartVAO);
  creaturePartVAO = 0;
 }
}

void GeometryEngine::DrawCreatureTexturedPart(const glm::mat4& mvp, GLuint texture,
                                              CreaturePartMesh mesh)
{
 if (texture == 0 || !defaultShader || !defaultShader->IsValid()) {
  return;
 }
 GLuint vao = 0;
 switch (mesh) {
 case CreaturePartMesh::Head:
  if (creatureHeadPartVAO == 0 && !InitCreatureHeadPartBuffers()) {
   return;
  }
  vao = creatureHeadPartVAO;
  break;
 case CreaturePartMesh::Body:
  if (creatureBodyPartVAO == 0 && !InitCreatureBodyPartBuffers()) {
   return;
  }
  vao = creatureBodyPartVAO;
  break;
 case CreaturePartMesh::Box:
 default:
  if (creaturePartVAO == 0 && !InitCreaturePartBuffers()) {
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

 if (!depthEnabled) {
  glDisable(GL_DEPTH_TEST);
 }
}

void GeometryEngine::DrawBoxWireframe(const glm::mat4& mvp, const glm::vec4& color)
{
 if (!outlineShader || !outlineShader->IsValid()) {
  if (!InitOutlineBuffers()) {
   return;
  }
 }
 if (outlineVAO == 0 && !InitOutlineBuffers()) {
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
 if (cullFaceEnabled) {
  glEnable(GL_CULL_FACE);
 } else {
  glDisable(GL_CULL_FACE);
 }
}

void GeometryEngine::RenderCreatures()
{
 if (!WorldInstance) {
  return;
 }
 auto camera = WorldInstance->GetCurrentUserCamera();
 if (!camera) {
  return;
 }
 const glm::mat4 viewProj = camera->GetProjection() * camera->GetViewMatrix();
 const float dt = static_cast<float>(camera->GetDeltaTime());
 const CreatureId controlledId = WorldInstance->GetControlledCreatureId();
 WorldInstance->ForEachCreature([&](Creature& creature) {
  if (creature.GetId() == controlledId) {
   if (camera->GetPerspective() == CameraPerspective::FirstPerson) {
    return;
   }
  }
  const std::string animType = WorldInstance->ResolveAnimationTypeId(creature);
  const CreatureDefinition* def = WorldInstance->GetCreatureDefinition(animType);
  CreatureDefinition fallback;
  if (!def) {
   fallback.id = animType;
   def = &fallback;
  }
  if (ICreatureVisual* visual = creature.GetVisual()) {
   visual->SetAppearance(WorldInstance->GetResolvedAppearance(creature));
   const CreatureLocomotionFacts& facts = creature.GetLocomotionFacts();
   ICreaturePosePresenter* presenter =
       WorldInstance->GetPosePresenterRegistry().Get(facts.archetype);
   CreaturePoseParams pose;
   if (presenter) {
    pose = presenter->Compute(facts, *def, dt);
   }
   visual->UpdatePose(creature, facts, pose, *def, dt);
   visual->SubmitDraw(*this, viewProj);
  }

  if (renderSettings_.creatureDebugBounds) {
   const glm::vec3 bodyOrigin = creature.GetBodyOrigin();
   const glm::vec3 maxSize = creature.GetBounds().profile.maxSizeBlocks;
   const glm::vec3 center = BoundsCollisionCenter(bodyOrigin, maxSize);
   glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
   model = glm::scale(model, maxSize);
   DrawBoxWireframe(viewProj * model, glm::vec4(0.2f, 0.85f, 1.0f, 1.0f));

   const int gx = static_cast<int>(std::floor(bodyOrigin.x));
   const int gz = static_cast<int>(std::floor(bodyOrigin.z));
   const float feetY = BoundsFeetY(bodyOrigin);
   float groundY = feetY;
   float delta = 0.0f;
   if (const std::optional<float> queryY =
           WorldInstance->QueryGroundFeetYUnder(gx, gz, feetY)) {
    groundY = *queryY;
    delta = feetY - groundY;
   }
   const glm::vec3 groundCenter(static_cast<float>(gx) + 0.5f, groundY, static_cast<float>(gz) + 0.5f);
   glm::mat4 groundModel = glm::translate(glm::mat4(1.0f), groundCenter);
   groundModel = glm::scale(groundModel, glm::vec3(1.02f, 0.02f, 1.02f));
   const float groundColor = std::abs(delta) < 0.05f ? 0.2f : 1.0f;
   DrawBoxWireframe(viewProj * groundModel,
                    glm::vec4(groundColor, 1.0f - groundColor * 0.5f, 0.15f, 1.0f));

   static auto lastPoseLog = std::chrono::steady_clock::now();
   const auto now = std::chrono::steady_clock::now();
   if (now - lastPoseLog >= std::chrono::seconds(2)) {
    lastPoseLog = now;
    const float eyeY = creature.GetLocomotionEye().y;
    const bool isControlled = creature.GetId() == controlledId;
    if (isControlled || controlledId == 0) {
     std::cerr << "[creature_pose] id=" << creature.GetId()
               << " feetY=" << feetY << " groundY=" << groundY << " delta=" << delta
               << " eyeY=" << eyeY << std::endl;
    }
   }
  }
 });
}

void GeometryEngine::RenderSelectionOutline()
{
    if (!WorldInstance->GetIsBlockIntersectionExists()) {
        return;
    }

    if (!outlineShader || !outlineShader->IsValid() || outlineVAO == 0) {
        return;
    }

    auto camera = WorldInstance->GetCurrentUserCamera();
    if (!camera) {
        return;
    }

    const glm::ivec3 blockPos = WorldInstance->GetIntersectionBlockPos();
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), BlockCenter(blockPos));
    const glm::mat4 mvp = camera->GetProjection() * camera->GetViewMatrix() * model;

    GLboolean cullFaceEnabled;
    glGetBooleanv(GL_CULL_FACE, &cullFaceEnabled);
    GLfloat previousLineWidth = 1.0f;
    glGetFloatv(GL_LINE_WIDTH, &previousLineWidth);

    glDisable(GL_CULL_FACE);
    glLineWidth(2.0f);

    outlineShader->Use();
    outlineShader->SetMat4("mvp_matrix", mvp);
    outlineShader->SetVec4("color", glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

    glBindVertexArray(outlineVAO);
    glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    outlineShader->Unuse();

    glLineWidth(previousLineWidth);
    if (cullFaceEnabled) {
        glEnable(GL_CULL_FACE);
    } else {
        glDisable(GL_CULL_FACE);
    }
}

}


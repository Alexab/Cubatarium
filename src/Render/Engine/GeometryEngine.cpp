
#include "Render/Engine/GeometryEngine.h"
#include "Render/Effects/InfluenceFxSystem.h"
#include "Render/Mesh/GpuMeshPipeline.h"
#include "Render/Mesh/GpuMeshSlotAllocator.h"
#include "World/Core/WorldLoadDiagnostics.h"
#include "Blocks/BlockRegistry.h"
#include "App/Settings/GraphicsQualityProfile.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include "Creatures/Player/User.h"
#include "Creatures/Visual/CreatureMeshGpuCache.h"
#include "Creatures/Visual/CreatureVisibility.h"
#include "Creatures/Visual/CreatureVisual.h"
#include "Pose/CreaturePoseParams.h"
#include "Pose/IUCreaturePosePresenter.h"
#include "Render/Camera/Camera.h"
#include "Render/Camera/CameraPerspective.h"
#include "Render/Camera/Frustum.h"
#include "Render/Engine/CreatureDrawPass.h"
#include "Render/Engine/DistanceFog.h"
#include "Render/Engine/HorizonFogColor.h"
#include "Render/Engine/FluidSurfaceMap.h"
#include "Render/Engine/IUFluidSurfaceProvider.h"
#include "Render/Mesh/GreedyMeshBatch.h"
#include "Render/Mesh/GpuFluidColumnScan.h"
#include "Render/Engine/FluidUnderwaterFogLogic.h"
#include "Render/Engine/IUMeshGpuStore.h"
#include "Render/Engine/MdiVertexPoolStore.h"
#include "Render/Backend/RenderBackendFactory.h"
#include "Render/Backend/RenderBackendCaps.h"
#include "Render/Backend/AndroidGpuPolicy.h"
#include "Render/Backend/GpuHotPathFallback.h"
#include "Render/Mesh/IUChunkCull.h"
#include "Render/Mesh/IUChunkMesher.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/GlIncludes.h"
#include "Render/Pipeline/GlStateMask.h"
#include "Render/Pipeline/GlStateScope.h"
#include "Render/Pipeline/GreedyTransparentPipeline.h"
#include "Render/Pipeline/GreedyTransparentSort.h"
#include "World/Adapters/WorldRenderReadAdapter.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/World.h"
#include "World/Lighting/IULightingPipeline.h"
#include "World/Math/GridMath.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Physics/LiquidDebugTrace.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"
#include "WorldGen/Sampling/BiomeRegistry.h"
#include "WorldGen/Sampling/BiomeSampler.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace cutum
{

namespace
{

float EnvironmentSkyLightScale(const UWorld::EnvironmentState &env)
{
  float scale = env.WeatherSkyAttenuation;
  if (env.PrecipitationIntensity > 0.05f)
  {
    scale *= 1.0f - std::clamp(env.PrecipitationIntensity, 0.0f, 1.0f) * 0.12f;
  }
  return std::clamp(scale, 0.82f, 1.0f);
}

void ApplyGreedyEnvironmentUniformsToShader(
    const std::shared_ptr<UShaderProgram> &shader,
    const UWorld::EnvironmentState *env,
    const UWorld::LightingSettings *lighting)
{
  if (env && lighting)
  {
    shader->SetFloat("uEnvFogMultiplier", env->WeatherFogMultiplier);
    shader->SetFloat("uEnvMinAmbient", lighting->MinAmbient);
    shader->SetFloat("uEnvDayFactor", env->DayNightFactor);
    shader->SetFloat("uEnvNightFactor", env->MoonNightFactor);
    shader->SetFloat("uEnvSkyLightScale", EnvironmentSkyLightScale(*env));
    shader->SetFloat("uEnvLightDebug", lighting->DebugEnabled ? 1.0f : 0.0f);
    shader->SetFloat("uEnvLightDebugMode",
                     static_cast<float>(lighting->DebugMode));
    shader->SetFloat("uEnvPrecipIntensity",
                     std::clamp(env->PrecipitationIntensity, 0.0f, 1.0f));
    shader->SetFloat("uEnvWetness", std::clamp(env->SurfaceWetness, 0.0f, 1.0f));
    return;
  }
  shader->SetFloat("uEnvFogMultiplier", 1.0f);
  shader->SetFloat("uEnvMinAmbient", 0.12f);
  shader->SetFloat("uEnvDayFactor", 1.0f);
  shader->SetFloat("uEnvNightFactor", 0.0f);
  shader->SetFloat("uEnvSkyLightScale", 1.0f);
  shader->SetFloat("uEnvLightDebug", 0.0f);
  shader->SetFloat("uEnvLightDebugMode", 0.0f);
  shader->SetFloat("uEnvPrecipIntensity", 0.0f);
  shader->SetFloat("uEnvWetness", 0.0f);
}

} // namespace

void UGeometryEngine::ApplyGreedyEnvironmentUniforms(
    const std::shared_ptr<UShaderProgram> &shader)
{
  if (WorldInstance)
  {
    ApplyGreedyEnvironmentUniformsToShader(
        shader, &WorldInstance->GetEnvironmentState(),
        &WorldInstance->GetLightingSettings());
    return;
  }
  ApplyGreedyEnvironmentUniformsToShader(shader, nullptr, nullptr);
}

UGeometryEngine::UGeometryEngine(
    std::shared_ptr<UWorld> world,
    std::shared_ptr<UTextureBaseStorage> texture_base_storage,
    std::shared_ptr<UTextureCubeStorage> texture_cube_storage,
    std::shared_ptr<UTextRenderer> text_renderer)
    : WorldInstance(world),
      WorldRenderReadModel(std::make_unique<UWorldRenderReadAdapter>(world)),
      TextureBaseStorageInstance(texture_base_storage),
      TextureCubeStorageInstance(texture_cube_storage),
      textRenderer(text_renderer), skyColor(0.5f, 0.7f, 1.0f, 1.0f),
      BaseSkyColor(0.5f, 0.7f, 1.0f), useGradientSky(true)
{
}

UGeometryEngine::~UGeometryEngine()
{
  DestroyCubeBuffers();
  DestroyFaceQuadBuffers();
  DestroyGreedyMeshBuffers();
  FluidMap().DestroyGpuResources();
  OpaqueDepthCapture.DestroyGpuResources();
  WeatherPass.DestroyGpuResources();
  BlockCrackPass.DestroyGpuResources();
  BlockBreakFx.DestroyGpuResources();
  InfluenceFx.DestroyGpuResources();
  DestroyPreviewBuffers();
  DestroyOutlineBuffers();
  CreatureDraw_.DestroyBuffers();
  CreatureMeshGpuCache::Instance().DestroyAll();
  DestroyOverlayBuffers();
  SkyGradientPass_.InvalidateGpuResources();
}

void UGeometryEngine::SetCreatureTextureStorage(
    std::shared_ptr<UCreatureTextureStorage> storage)
{
  CreatureTextureStorage = std::move(storage);
}

void UGeometryEngine::EnsureRenderBackendsBound()
{
  if (!URenderBackendFactory::IsBound(RenderBackends))
  {
    RenderBackendCaps caps = GetActiveRenderBackendCaps();
    if (!caps.ProbeCompleted)
    {
      // Bind before GL probe (tests / early init): keep platform defaults.
      caps = DetectRenderBackendCaps();
    }
    AndroidGpuAllowlistConfig allowlist =
        LoadAndroidGpuAllowlist("assets/config/android_gpu.json");
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
    // Prefer APK-extracted assets root when present.
    {
      const AndroidGpuAllowlistConfig from_assets =
          LoadAndroidGpuAllowlist("config/android_gpu.json");
      if (!from_assets.AllowRenderers.empty())
      {
        allowlist = from_assets;
      }
    }
#endif
    ApplyAndroidGpuPolicy(caps, Render.AndroidGpuEnabled, &allowlist);
    SetActiveRenderBackendCaps(caps);
    URenderBackendFactory::BindOnce(RenderBackends, caps);
  }
  if (!FluidSurfaceProvider)
  {
    FluidSurfaceProvider =
        CreateFluidSurfaceProvider(GetActiveRenderBackendCaps());
  }
  if (WorldInstance && RenderBackends.Store)
  {
    bool prefer_patch = RenderBackends.Store->SupportsMultiDrawIndirect();
    if (const char *env = std::getenv("CUBATARIUM_PREFER_GPU_STORE_PATCH"))
    {
      if (env[0] == '0')
      {
        prefer_patch = false;
      }
      else if (env[0] == '1')
      {
        prefer_patch = true;
      }
    }
    WorldInstance->GetMeshService().SetPreferGpuStorePatch(prefer_patch);
  }
  if (WorldInstance)
  {
    auto &mesh = WorldInstance->GetMeshService();
    mesh.SetCullBackend(RenderBackends.Cull.get());
    mesh.SetMesherBackend(RenderBackends.Mesher.get());
  }
}

IUMeshGpuStore &UGeometryEngine::MeshStore()
{
  EnsureRenderBackendsBound();
  return *RenderBackends.Store;
}

bool UGeometryEngine::InitEngine()
{
  EnsureRenderBackendsBound();

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

  if (!CreatureDraw_.InitBuffers(*shaderManager))
  {
    std::cerr << "Failed to initialize creature draw pass" << std::endl;
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
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
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
#endif
  }

  if (!instancedShader || !instancedShader->IsValid())
  {
    std::cerr << "Failed to create instanced shader" << std::endl;
    return false;
  }

  instancedFaceShader = shaderManager->CreateShader(
      "instanced_face", "shaders/vshader_instanced_face.glsl",
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
      "shaders/fshader_instanced_face.glsl");
#else
      "shaders/fshader.glsl");
#endif
  if (!instancedFaceShader || !instancedFaceShader->IsValid())
  {
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
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
#endif
  }

  if (!instancedFaceShader || !instancedFaceShader->IsValid())
  {
    std::cerr << "Failed to create instanced face shader" << std::endl;
    return false;
  }

  greedyShader = shaderManager->CreateShader(
      "greedy", "shaders/vshader_greedy.glsl", "shaders/fshader_greedy.glsl");
  if (!greedyShader || !greedyShader->IsValid())
  {
    std::cerr << "Failed to create greedy mesh shader" << std::endl;
    return false;
  }

  packedGreedyShader = shaderManager->CreateShader(
      "packed_greedy", "shaders/vshader_packed_greedy.glsl",
      "shaders/fshader_greedy.glsl");
  if (!packedGreedyShader || !packedGreedyShader->IsValid())
  {
    std::cerr << "Failed to create packed greedy mesh shader" << std::endl;
    return false;
  }

  crossInstancedShader = shaderManager->CreateShader(
      "cross_instanced", "shaders/vshader_cross_instanced.glsl",
      "shaders/fshader_greedy.glsl");
  if (!crossInstancedShader || !crossInstancedShader->IsValid())
  {
    std::cerr << "Failed to create cross instanced shader" << std::endl;
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

  if (!WeatherPass.InitShaders(shaderManager))
  {
    return false;
  }

  // Cosmetic pass: crack falls back to wireframe when the shader is absent.
  if (!BlockCrackPass.InitShader(shaderManager))
  {
    std::cerr << "Textured block crack disabled (shader load failed)"
              << std::endl;
  }
  if (!BlockBreakFx.InitShaders(shaderManager))
  {
    std::cerr << "Block break debris disabled (shader load failed)"
              << std::endl;
  }
  if (!InfluenceFx.InitShaders(shaderManager))
  {
    std::cerr << "Influence FX disabled (shader load failed)" << std::endl;
  }

  return true;
}

void UGeometryEngine::Paint(int width_size, int height_size,
                            double view_duration)
{
  if (WorldInstance)
  {
    WorldInstance->GetPhysicsTelemetryMutable().GpuDrawCmds = 0;
    WorldInstance->GetPhysicsTelemetryMutable().GpuCullIndirect = 0.0;
    WorldInstance->GetPhysicsTelemetryMutable().GpuMeshVboDispatch = 0;
  }
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
  const auto post_begin = std::chrono::high_resolution_clock::now();
  if (OverlayTintAlpha > 0.01f)
  {
    RenderFluidOverlay(width_size, height_size);
  }
  RenderWeatherOverlay(width_size, height_size);
  if (ShowCrosshair)
  {
    bool drawCrosshair = true;
    if (WorldInstance)
    {
      if (auto camera = WorldInstance->GetCurrentUserCamera())
      {
        // Isometric aims with the OS cursor; center crosshair is misleading.
        drawCrosshair = !camera->IsIsometricProjection();
      }
    }
    if (drawCrosshair)
    {
      RenderCrosshair(width_size, height_size);
    }
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
  if (WorldInstance)
  {
    WorldInstance->SetLastPostSceneMs(
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - post_begin)
            .count());
  }
}

void UGeometryEngine::DrawCubeGeometry()
{
  auto t_begin = std::chrono::high_resolution_clock::now();

  auto camera = WorldRenderReadModel
                    ? WorldRenderReadModel->GetCurrentUserCamera()
                    : WorldInstance->GetCurrentUserCamera();
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
  UWorldMeshService *mesh_service =
      WorldRenderReadModel ? WorldRenderReadModel->TryGetMeshService()
                           : &WorldInstance->GetMeshService();
  if (!mesh_service)
  {
    return;
  }
  static bool logged_first_paint_diag = false;
  if (!logged_first_paint_diag && WorldInstance)
  {
    logged_first_paint_diag = true;
    LogWorldLoadDiag("first_paint", *WorldInstance, camera->GetPosition());
  }
  mesh_service->GetCache().SetSurfaceWetness(std::clamp(
      WorldInstance->GetEnvironmentState().SurfaceWetness, 0.0f, 1.0f));
  const uint64_t meshRevision = mesh_service->GetMeshRevision();
  const bool useGreedyMesh = Render.UseFaceQuadDraw();
  const size_t renderCount =
      useGreedyMesh
          ? mesh_service->GetGreedyVertexCount()
          : mesh_service
                ->PrepareFaceInstances(WorldInstance->GetBlockWorld(),
                                       WorldInstance->GetBlockRegistry(),
                                       camera)
                .size();
  const bool useBatchCache = Render.BatchCache && !useGreedyMesh;

  if (useGreedyMesh)
  {
    auto filter_render_ready_refs = [&](const std::vector<GreedyBatchRef> &in)
    {
      std::unordered_map<int64_t, bool> ready_cache;
      std::vector<GreedyBatchRef> out;
      out.reserve(in.size());
      for (const GreedyBatchRef &ref : in)
      {
        // P2: per-cy slice key (not column xz alone).
        const int64_t key =
            (static_cast<int64_t>(ref.chunkCoord.x) << 42) ^
            ((static_cast<int64_t>(ref.chunkCoord.y) & 0x3ffll) << 32) ^
            (static_cast<int64_t>(ref.chunkCoord.z) & 0xffffffffll);
        auto it = ready_cache.find(key);
        bool ready = false;
        if (it != ready_cache.end())
        {
          ready = it->second;
        }
        else
        {
          ready = WorldInstance->IsChunkSliceRenderReady(ref.chunkCoord);
          ready_cache.emplace(key, ready);
        }
        if (ready)
        {
          out.push_back(ref);
        }
      }
      return out;
    };
    const UWorldMeshService::GreedyDrawSnapshot draw =
        mesh_service->PrepareGreedyDraw(WorldInstance->GetBlockWorld(),
                                        WorldInstance->GetBlockRegistry(),
                                        camera);
    std::vector<GreedyBatchRef> filtered_opaque =
        filter_render_ready_refs(draw.opaqueCutoutRefs);
    std::vector<GreedyBatchRef> filtered_transparent =
        filter_render_ready_refs(draw.transparentRefs);
    {
      auto &phys = WorldInstance->GetPhysicsTelemetryMutable();
      phys.OpaqueRefsCpuVis =
          static_cast<uint64_t>(draw.opaqueCutoutRefs.size());
      phys.OpaqueRefsRenderReady =
          static_cast<uint64_t>(filtered_opaque.size());
    }
    const std::vector<GreedyBatchRef> &opaqueCutoutRefs = filtered_opaque;
    if (!useBatchCache || !BlockBatchesValid ||
        opaqueCutoutRefs.size() != CachedInstanceCount ||
        draw.meshRevision != CachedMeshRevision)
    {
      CachedInstanceCount = opaqueCutoutRefs.size();
      CachedMeshRevision = draw.meshRevision;
      BlockBatchesValid = true;
    }
    const glm::mat4 vp = camera->GetProjection() * camera->GetViewMatrix();
    DrawGreedyOpaqueBatches(draw.cache, opaqueCutoutRefs, vp,
                            camera->GetPosition(), textures,
                            draw.meshRevision, draw.cullRevision);
    DrawCrossInstancedBatches(draw.crossBatches, vp, textures,
                              draw.meshRevision, draw.cullRevision);
    OpaqueDepthCapture.CaptureFromDefaultFramebuffer();
    GLboolean blendWasEnabled;
    glGetBooleanv(GL_BLEND, &blendWasEnabled);
    GLboolean cullWasEnabled;
    glGetBooleanv(GL_CULL_FACE, &cullWasEnabled);
    GreedyTransparentDrawContext tctx{draw.cache,
                                      filtered_transparent,
                                      vp,
                                      draw.meshRevision,
                                      draw.cullRevision,
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
    auto blockInstances = mesh_service->PrepareFaceInstances(
        WorldInstance->GetBlockWorld(), WorldInstance->GetBlockRegistry(),
        camera);
    blockInstances.erase(
        std::remove_if(
            blockInstances.begin(), blockInstances.end(),
            [&](const BlockInstance &inst)
            {
              const glm::vec3 world_pos(inst.model[3]);
              const glm::ivec3 chunk = UChunkManager::WorldToChunk(
                  glm::ivec3(static_cast<int>(std::floor(world_pos.x)),
                             static_cast<int>(std::floor(world_pos.y)),
                             static_cast<int>(std::floor(world_pos.z))));
              const glm::ivec3 ground(chunk.x, 0, chunk.z);
              return !WorldInstance->IsColumnRenderReady(ground);
            }),
        blockInstances.end());
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
  if (WorldInstance)
  {
    if (auto break_camera = WorldInstance->GetCurrentUserCamera())
    {
      const glm::mat4 view = break_camera->GetViewMatrix();
      const glm::mat4 proj = break_camera->GetProjection();
      const glm::mat4 view_inv = glm::inverse(view);
      const glm::vec3 camera_right = glm::normalize(glm::vec3(view_inv[0]));
      const glm::vec3 camera_up = glm::normalize(glm::vec3(view_inv[1]));
      const float dt = static_cast<float>(break_camera->GetDeltaTime());
      BlockBreakFx.UpdateAndRender(*WorldInstance, dt, proj * view,
                                   camera_right, camera_up);
    }
  }
  RenderBiomeDebugOverlay();
  if (WorldInstance)
  {
    CreatureDraw_.Render(*WorldInstance, *this, Render);
    SetInfluenceFxWorld(WorldInstance.get());
    UInfluenceFxSystem::Get().RegisterSink();
    if (auto camera = WorldInstance->GetCurrentUserCamera())
    {
      const glm::mat4 view_proj =
          camera->GetProjection() * camera->GetViewMatrix();
      InfluenceFx.UpdateAndRender(view_proj,
                                  static_cast<float>(camera->GetDeltaTime()));
    }
  }

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

GLuint UGeometryEngine::InspectBlockGpuTexture(BlockId block_id,
                                               bool *map_has_entry) const
{
  if (!TextureCubeStorageInstance)
  {
    if (map_has_entry)
    {
      *map_has_entry = false;
    }
    return 0;
  }
  const auto &textures = TextureCubeStorageInstance->GetTextures();
  const auto it = textures.find(static_cast<size_t>(block_id));
  if (it == textures.end())
  {
    if (map_has_entry)
    {
      *map_has_entry = false;
    }
    return 0;
  }
  if (map_has_entry)
  {
    *map_has_entry = true;
  }
  return it->second.GetTextureId();
}

void UGeometryEngine::SetRenderSettings(const RenderSettings &settings)
{
  Render = settings;
  SetGradientSky(Render.GradientSky);
  BlockBatchesValid = false;
  MeshStore().DestroyAll(GreedyGpuOpaque, GreedyGpuCutout,
                         GreedyGpuTransparent);
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
  if (batch.ModelMatrices.empty())
  {
    if (VerboseLogging)
      std::cout << "DrawBatch: Empty batch, skipping" << std::endl;
    return;
  }

  if (VerboseLogging)
    std::cout << "DrawBatch: Drawing " << batch.ModelMatrices.size()
              << " instances" << std::endl;

  glBindTexture(GL_TEXTURE_2D, batch.textureID);

  std::vector<glm::mat4> instanceMVPs;
  auto camera = WorldInstance->GetCurrentUserCamera();
  if (!camera)
    return;

  instanceMVPs.reserve(batch.ModelMatrices.size());
  for (const auto &model : batch.ModelMatrices)
  {
    instanceMVPs.push_back(camera->GetProjection() * camera->GetViewMatrix() *
                           model);
  }

  const bool isBlockBatch = !batch.ModelMatrices.empty();
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
  if (!WorldInstance)
  {
    return;
  }
  WorldInstance->EnsureDefaultCelestialBodies();
  WorldInstance->RefreshSkyVisualStateForRender();
  glm::mat3 inv_view_rot(1.0f);
  if (auto camera = WorldInstance->GetCurrentUserCamera())
  {
    inv_view_rot = glm::transpose(glm::mat3(camera->GetViewMatrix()));
  }
  FluidMap().ClearLastFrameStats();
  UnderwaterFogPass_.Update(*WorldInstance, Render, FluidMap(), BaseSkyColor,
                            inv_view_rot);
  skyColor = glm::vec4(UnderwaterFogPass_.GetSkyTint(), 1.0f);
  OverlayTintColor = UnderwaterFogPass_.GetOverlayTintColor();
  OverlayTintAlpha = UnderwaterFogPass_.GetOverlayTintAlpha();
  OverlayBlockId = UnderwaterFogPass_.GetOverlayBlockId();
  const FluidSurfaceMapFrameStats &fluid_stats =
      FluidMap().GetLastFrameStats();
  WorldInstance->SetLastFluidMapCpuMs(fluid_stats.CpuMs);
  WorldInstance->SetLastFluidMapGpuMs(fluid_stats.GpuMs);
  WorldInstance->SetLastFluidMapDirtyChunks(fluid_stats.DirtyChunksPending);
  WorldInstance->SetLastFluidMapFullRebuild(fluid_stats.FullRebuild);
}

PerformancePreset UGeometryEngine::EffectiveWeatherPreset() const
{
  if (!WorldInstance)
  {
    return Render.Preset;
  }
  // When SwapBuffers stalls (Wall ≪ Sim), cut weather/sky GPU cost.
  if (WorldInstance->GetLastSwapWaitMs() > 40.0 ||
      WorldInstance->GetWallFrameDelta() * 1000.0 > 50.0)
  {
    return PerformancePreset::Fast;
  }
  return Render.Preset;
}

void UGeometryEngine::ApplyFogUniforms(
    const std::shared_ptr<UShaderProgram> &shader, const glm::vec3 &cameraPos,
    bool applyBelowSurfaceFog)
{
  UnderwaterFogPass_.ApplyUniforms(shader, cameraPos, FluidMap(),
                                   applyBelowSurfaceFog);
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
  const bool opaqueDepthGuard =
      transparentPass && mode != GreedyShaderMode::ShellDepthPrepass;
  if (opaqueDepthGuard)
  {
    OpaqueDepthCapture.Bind();
  }
  OpaqueDepthCapture.ApplyShaderUniforms(greedyShader, opaqueDepthGuard);
  if (auto camera = WorldInstance->GetCurrentUserCamera())
  {
    // alphaCutout here is shader discard mode (GPF5 merges solid+cutout), not
    // a dedicated cutout-only pass — still apply underwater fog to opaque.
    ApplyFogUniforms(
        greedyShader, camera->GetPosition(),
        cutum::ShouldApplyBelowSurfaceFogToPass(transparentPass,
                                                /*alpha_cutout=*/false));
  }
  ApplyGreedyEnvironmentUniforms(greedyShader);
  glActiveTexture(GL_TEXTURE0);

  glBindVertexArray(greedyMeshVAO);
  const GLsizei kStride = static_cast<GLsizei>(sizeof(GreedyMeshVertex));
  uint64_t draw_cmds = 0;

  IUMeshGpuStore &store = MeshStore();
  const bool use_mdi = store.SupportsMultiDrawIndirect() &&
                       cache.usesVertexPool && cache.poolVbo != 0 &&
                       cache.poolEbo != 0;
  if (use_mdi)
  {
    // Local indices + baseVertex: attribs at buffer origin once.
    glBindBuffer(GL_ARRAY_BUFFER, cache.poolVbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cache.poolEbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        1, 1, GL_FLOAT, GL_FALSE, kStride,
        reinterpret_cast<void *>(offsetof(GreedyMeshVertex, faceIndex)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        2, 2, GL_FLOAT, GL_FALSE, kStride,
        reinterpret_cast<void *>(offsetof(GreedyMeshVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        3, 1, GL_FLOAT, GL_FALSE, kStride,
        reinterpret_cast<void *>(offsetof(GreedyMeshVertex, skyLight)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(
        4, 1, GL_FLOAT, GL_FALSE, kStride,
        reinterpret_cast<void *>(offsetof(GreedyMeshVertex, blockLight)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(
        5, 1, GL_FLOAT, GL_FALSE, kStride,
        reinterpret_cast<void *>(offsetof(GreedyMeshVertex, wetness)));
    glEnableVertexAttribArray(5);

    std::vector<DrawElementsIndirectCommand> cmds;
    size_t i = 0;
    while (i < cache.batches.size())
    {
      const GreedyGpuBatch &head = cache.batches[i];
      if (!head.pooled || head.indexCountGl <= 0)
      {
        ++i;
        continue;
      }
      const auto texIt = textures.find(static_cast<size_t>(head.blockId));
      if (texIt == textures.end() || texIt->second.GetTextureId() == 0)
      {
        ++i;
        continue;
      }

      size_t j = i + 1;
      while (j < cache.batches.size() && cache.batches[j].pooled &&
             cache.batches[j].indexCountGl > 0 &&
             cache.batches[j].blockId == head.blockId)
      {
        ++j;
      }

      SetBlockAnimUniforms(greedyShader, head.blockId, textures);
      glBindTexture(GL_TEXTURE_2D, texIt->second.GetTextureId());
      // P2: prefer GPU-resident 1:1 cmd table (instanceCount from compact).
      if (store.SubmitIndirectCommandsGpuRange(cache, i, j))
      {
        ++draw_cmds;
      }
      else if (store.BuildIndirectCommandsRange(cache, i, j, cmds) > 0 &&
               store.SubmitIndirectCommands(cmds))
      {
        NoteGpuHotPathFallback();
        ++draw_cmds;
      }
      else
      {
        NoteGpuHotPathFallback();
        for (size_t k = i; k < j; ++k)
        {
          const GreedyGpuBatch &gpu = cache.batches[k];
          if (gpu.drawInstanceCount == 0)
          {
            continue;
          }
          const GLint base_vertex = static_cast<GLint>(
              gpu.vboByteOffset / sizeof(GreedyMeshVertex));
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
          // GLES 3.1 core has no glDrawElementsBaseVertex (desktop MDI
          // fallback only). Staging store avoids this path at runtime.
          (void)base_vertex;
          continue;
#else
          glDrawElementsBaseVertex(
              GL_TRIANGLES, gpu.indexCountGl, GL_UNSIGNED_INT,
              reinterpret_cast<void *>(gpu.eboByteOffset), base_vertex);
          ++draw_cmds;
#endif
        }
      }
      i = j;
    }

    // Non-pooled leftovers (should be rare when usesVertexPool).
    for (const GreedyGpuBatch &gpu : cache.batches)
    {
      if (gpu.pooled || gpu.indexCountGl <= 0)
      {
        continue;
      }
      if (gpu.vbo == 0 || gpu.ebo == 0)
      {
        continue;
      }
      const auto texIt = textures.find(static_cast<size_t>(gpu.blockId));
      if (texIt == textures.end() || texIt->second.GetTextureId() == 0)
      {
        continue;
      }
      SetBlockAnimUniforms(greedyShader, gpu.blockId, textures);
      glBindTexture(GL_TEXTURE_2D, texIt->second.GetTextureId());
      glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ebo);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride, nullptr);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(
          1, 1, GL_FLOAT, GL_FALSE, kStride,
          reinterpret_cast<void *>(offsetof(GreedyMeshVertex, faceIndex)));
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(
          2, 2, GL_FLOAT, GL_FALSE, kStride,
          reinterpret_cast<void *>(offsetof(GreedyMeshVertex, u)));
      glEnableVertexAttribArray(2);
      glVertexAttribPointer(
          3, 1, GL_FLOAT, GL_FALSE, kStride,
          reinterpret_cast<void *>(offsetof(GreedyMeshVertex, skyLight)));
      glEnableVertexAttribArray(3);
      glVertexAttribPointer(
          4, 1, GL_FLOAT, GL_FALSE, kStride,
          reinterpret_cast<void *>(offsetof(GreedyMeshVertex, blockLight)));
      glEnableVertexAttribArray(4);
      glVertexAttribPointer(
          5, 1, GL_FLOAT, GL_FALSE, kStride,
          reinterpret_cast<void *>(offsetof(GreedyMeshVertex, wetness)));
      glEnableVertexAttribArray(5);
      glDrawElements(GL_TRIANGLES, gpu.indexCountGl, GL_UNSIGNED_INT, nullptr);
      NoteGpuHotPathFallback();
      ++draw_cmds;
    }
  }
  else
  {
    for (const GreedyGpuBatch &gpu : cache.batches)
    {
      SetBlockAnimUniforms(greedyShader, gpu.blockId, textures);
      if (gpu.indexCountGl <= 0)
      {
        continue;
      }
      const GLuint vbo = gpu.pooled ? cache.poolVbo : gpu.vbo;
      const GLuint ebo = gpu.pooled ? cache.poolEbo : gpu.ebo;
      if (vbo == 0 || ebo == 0)
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
      glBindBuffer(GL_ARRAY_BUFFER, vbo);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
      glVertexAttribPointer(
          0, 3, GL_FLOAT, GL_FALSE, kStride,
          reinterpret_cast<void *>(gpu.pooled ? gpu.vboByteOffset : 0));
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(
          1, 1, GL_FLOAT, GL_FALSE, kStride,
          reinterpret_cast<void *>((gpu.pooled ? gpu.vboByteOffset : 0) +
                                   offsetof(GreedyMeshVertex, faceIndex)));
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(
          2, 2, GL_FLOAT, GL_FALSE, kStride,
          reinterpret_cast<void *>((gpu.pooled ? gpu.vboByteOffset : 0) +
                                   offsetof(GreedyMeshVertex, u)));
      glEnableVertexAttribArray(2);
      glVertexAttribPointer(
          3, 1, GL_FLOAT, GL_FALSE, kStride,
          reinterpret_cast<void *>((gpu.pooled ? gpu.vboByteOffset : 0) +
                                   offsetof(GreedyMeshVertex, skyLight)));
      glEnableVertexAttribArray(3);
      glVertexAttribPointer(
          4, 1, GL_FLOAT, GL_FALSE, kStride,
          reinterpret_cast<void *>((gpu.pooled ? gpu.vboByteOffset : 0) +
                                   offsetof(GreedyMeshVertex, blockLight)));
      glEnableVertexAttribArray(4);
      glVertexAttribPointer(
          5, 1, GL_FLOAT, GL_FALSE, kStride,
          reinterpret_cast<void *>((gpu.pooled ? gpu.vboByteOffset : 0) +
                                   offsetof(GreedyMeshVertex, wetness)));
      glEnableVertexAttribArray(5);
      glDrawElements(
          GL_TRIANGLES, gpu.indexCountGl, GL_UNSIGNED_INT,
          reinterpret_cast<void *>(gpu.pooled ? gpu.eboByteOffset : 0));
      NoteGpuHotPathFallback();
      ++draw_cmds;
    }
  }

  if (WorldInstance)
  {
    auto &phys = WorldInstance->GetPhysicsTelemetryMutable();
    phys.GpuDrawCmds += draw_cmds;
    phys.GpuCullMs = WorldInstance->GetMeshService().GetLastGpuCullMs();
    if (RenderBackends.Mesher)
    {
      phys.BackendMesher = RenderBackends.Mesher->BackendName();
    }
    if (RenderBackends.Store)
    {
      phys.BackendStore = RenderBackends.Store->BackendName();
    }
    if (RenderBackends.Cull)
    {
      phys.BackendCull = RenderBackends.Cull->BackendName();
    }
    if (FluidSurfaceProvider)
    {
      phys.BackendFluid = FluidSurfaceProvider->BackendName();
      phys.GpuFluidScanOn = PreferGpuFluidColumnScan() ? 1.0 : 0.0;
    }
    {
      const LightingMode mode = WorldInstance->GetLightingPipeline().GetMode();
      phys.BackendLightingMode =
          mode == LightingMode::Flat
              ? "flat"
              : ((phys.BackendMesher.find("gpu_") != std::string::npos ||
                  phys.BackendMesher.find("android_gpu") == 0)
                     ? "gpu_full"
                     : "full");
    }
    {
      const RenderBackendCaps &caps = GetActiveRenderBackendCaps();
      phys.CapsHasCompute = caps.HasCompute ? 1.0 : 0.0;
      phys.CapsHasSsbo = caps.HasSsbo ? 1.0 : 0.0;
      phys.CapsProbeCompleted = caps.ProbeCompleted ? 1.0 : 0.0;
      phys.AndroidGpuUserPref = Render.AndroidGpuEnabled ? 1.0 : 0.0;
      phys.AndroidGpuEffective = caps.AllowAndroidGpu ? 1.0 : 0.0;
      phys.AndroidGpuDenyReason = caps.AndroidGpuDenyReason;
      phys.GlVersion = caps.GlVersion;
      phys.GlRenderer = caps.GlRenderer;
    }
  }

  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  if (opaqueDepthGuard)
  {
    glActiveTexture(GL_TEXTURE0);
  }
  greedyShader->Unuse();
}

void UGeometryEngine::ResetWorldRenderState()
{
  FluidMap().DestroyGpuResources();
  MeshStore().DestroyAll(GreedyGpuOpaque, GreedyGpuCutout,
                         GreedyGpuTransparent);
  BlockBatchesValid = false;
  CachedMeshRevision = 0;
  CachedInstanceCount = 0;
  PreparedTransparentTextures = nullptr;
  glm::vec3 sky_tint = BaseSkyColor;
  glm::vec3 fog_color(0.05f, 0.15f, 0.35f);
  if (WorldInstance)
  {
    WorldInstance->RefreshSkyVisualStateForRender();
    const UWorld::EnvironmentState &env = WorldInstance->GetEnvironmentState();
    HorizonFogColorInput color_in;
    color_in.base_sky = BaseSkyColor;
    color_in.day = env.DayNightFactor;
    color_in.moon = env.MoonNightFactor;
    color_in.weather_atten = env.WeatherSkyAttenuation;
    color_in.cloudiness = env.Cloudiness;
    color_in.precip = env.PrecipitationIntensity;
    color_in.celestial_bodies = &env.CelestialBodies;
    const AtmosphericSkyColors atmospheric =
        ComputeAtmosphericSkyColors(color_in);
    sky_tint = atmospheric.sky_tint;
    fog_color = atmospheric.fog_color;
    if (Render.DistanceFog)
    {
      const DistanceFogParams distance_fog = ComputeDistanceFog(
          WorldInstance->GetEffectiveFogRenderDistance(), atmospheric.fog_color,
          Render.DistanceFogStartRatio, WorldInstance->GetEffectiveFogStartRatio(),
          Render.DistanceFogDensity,
          WorldInstance->GetEffectiveFogEndMarginBlocks(
              Render.DistanceFogEndMarginBlocks));
      fog_color = distance_fog.Color;
    }
  }
  UnderwaterFogPass_.ResetAtmosphericColors(sky_tint, fog_color);
}

void UGeometryEngine::WarmupGreedyGpuFromWorld()
{
  if (!WorldInstance || !TextureCubeStorageInstance ||
      !Render.UseFaceQuadDraw())
  {
    return;
  }
  auto camera = WorldRenderReadModel
                    ? WorldRenderReadModel->GetCurrentUserCamera()
                    : WorldInstance->GetCurrentUserCamera();
  if (!camera)
  {
    return;
  }

  UWorldMeshService *mesh_service =
      WorldRenderReadModel ? WorldRenderReadModel->TryGetMeshService()
                           : &WorldInstance->GetMeshService();
  if (!mesh_service)
  {
    return;
  }
  mesh_service->GetCache().SetSurfaceWetness(std::clamp(
      WorldInstance->GetEnvironmentState().SurfaceWetness, 0.0f, 1.0f));
  const UWorldMeshService::GreedyDrawSnapshot draw =
      mesh_service->PrepareGreedyDraw(WorldInstance->GetBlockWorld(),
                                      WorldInstance->GetBlockRegistry(),
                                      camera);
  auto filter_render_ready_refs = [&](const std::vector<GreedyBatchRef> &in)
  {
    std::unordered_map<int64_t, bool> ready_cache;
    std::vector<GreedyBatchRef> out;
    out.reserve(in.size());
    for (const GreedyBatchRef &ref : in)
    {
      const int64_t key =
          (static_cast<int64_t>(ref.chunkCoord.x) << 42) ^
          ((static_cast<int64_t>(ref.chunkCoord.y) & 0x3ffll) << 32) ^
          (static_cast<int64_t>(ref.chunkCoord.z) & 0xffffffffll);
      auto it = ready_cache.find(key);
      bool ready = false;
      if (it != ready_cache.end())
      {
        ready = it->second;
      }
      else
      {
        ready = WorldInstance->IsChunkSliceRenderReady(ref.chunkCoord);
        ready_cache.emplace(key, ready);
      }
      if (ready)
      {
        out.push_back(ref);
      }
    }
    return out;
  };
  const std::vector<GreedyBatchRef> filtered_opaque =
      filter_render_ready_refs(draw.opaqueCutoutRefs);
  const std::vector<GreedyBatchRef> filtered_transparent =
      filter_render_ready_refs(draw.transparentRefs);
  const glm::mat4 vp = camera->GetProjection() * camera->GetViewMatrix();
  const auto textures = TextureCubeStorageInstance->GetTextures();

  DrawGreedyOpaqueBatches(draw.cache, filtered_opaque, vp,
                          camera->GetPosition(), textures, draw.meshRevision,
                          draw.cullRevision);
  DrawCrossInstancedBatches(draw.crossBatches, vp, textures, draw.meshRevision,
                            draw.cullRevision);

  GreedyTransparentDrawContext tctx{draw.cache,
                                    filtered_transparent,
                                    vp,
                                    draw.meshRevision,
                                    draw.cullRevision,
                                    camera->GetPosition(),
                                    WorldInstance->GetBlockRegistry(),
                                    textures};
  PrepareTransparent(tctx);

  CachedInstanceCount = filtered_opaque.size();
  CachedMeshRevision = draw.meshRevision;
  BlockBatchesValid = true;
}

void UGeometryEngine::DrawCrossInstancedBatches(
    const std::vector<CrossInstanceBatch> &batches, const glm::mat4 &vp,
    const std::map<size_t, UTextureCube> &textures, uint64_t meshRevision,
    uint64_t cullRevision)
{
  if (batches.empty())
  {
    return;
  }
  if (!CrossGpuBackend.EnsureTemplateMesh())
  {
    return;
  }
  if (!crossInstancedShader || !crossInstancedShader->IsValid())
  {
    return;
  }

  std::unordered_map<int64_t, bool> render_ready_cache;
  auto is_slice_render_ready = [&](glm::ivec3 chunk) -> bool
  {
    const int64_t key =
        (static_cast<int64_t>(chunk.x) << 42) ^
        ((static_cast<int64_t>(chunk.y) & 0x3ffll) << 32) ^
        (static_cast<int64_t>(chunk.z) & 0xffffffffll);
    const auto it = render_ready_cache.find(key);
    if (it != render_ready_cache.end())
    {
      return it->second;
    }
    const bool ready = WorldInstance->IsChunkSliceRenderReady(chunk);
    render_ready_cache.emplace(key, ready);
    return ready;
  };
  std::vector<CrossInstanceBatch> filtered_batches;
  filtered_batches.reserve(batches.size());
  for (const CrossInstanceBatch &batch : batches)
  {
    CrossInstanceBatch fb;
    fb.blockId = batch.blockId;
    for (const CrossInstanceGpu &inst : batch.instances)
    {
      const glm::ivec3 chunk = UChunkManager::WorldToChunk(
          glm::ivec3(static_cast<int>(std::floor(inst.center.x)),
                     static_cast<int>(std::floor(inst.center.y)),
                     static_cast<int>(std::floor(inst.center.z))));
      if (is_slice_render_ready(chunk))
      {
        fb.instances.push_back(inst);
      }
    }
    if (!fb.instances.empty())
    {
      filtered_batches.push_back(std::move(fb));
    }
  }
  if (filtered_batches.empty())
  {
    return;
  }
  CrossGpuBackend.RefreshPass(CrossGpuPass, filtered_batches, meshRevision,
                              cullRevision);
  if (WorldInstance)
  {
    WorldInstance->GetPhysicsTelemetryMutable().CrossBatchCount =
        CrossGpuPass.batches.size();
  }
  if (CrossGpuPass.batches.empty())
  {
    return;
  }

  GLboolean cullWasEnabled;
  glGetBooleanv(GL_CULL_FACE, &cullWasEnabled);
  glDisable(GL_CULL_FACE);

  crossInstancedShader->Use();
  crossInstancedShader->SetMat4("mvp_matrix", vp);
  crossInstancedShader->SetInt("texture0", 0);
  SetGreedyShaderMode(crossInstancedShader, true, false,
                      GreedyShaderMode::TransparentColor, 0.0f);
  if (auto camera = WorldInstance->GetCurrentUserCamera())
  {
    ApplyFogUniforms(crossInstancedShader, camera->GetPosition(), false);
  }
  ApplyGreedyEnvironmentUniforms(crossInstancedShader);
  glActiveTexture(GL_TEXTURE0);

  const GLsizei index_count = CrossGpuBackend.GetTemplateIndexCount();
  for (const CrossGpuBatch &gpu : CrossGpuPass.batches)
  {
    if (gpu.instanceCount == 0 || gpu.instanceVbo == 0 || gpu.vao == 0)
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
    SetBlockAnimUniforms(crossInstancedShader, gpu.blockId, textures);
    glBindVertexArray(gpu.vao);
    glDrawElementsInstanced(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr,
                            static_cast<GLsizei>(gpu.instanceCount));
  }
  glBindVertexArray(0);
  crossInstancedShader->Unuse();

  if (cullWasEnabled)
  {
    glEnable(GL_CULL_FACE);
  }
}

void UGeometryEngine::DrawGreedyOpaqueBatches(
    const UChunkMeshCache &cache,
    const std::vector<GreedyBatchRef> &opaqueCutoutRefs, const glm::mat4 &vp,
    const glm::vec3 &cameraPos, const std::map<size_t, UTextureCube> &textures,
    uint64_t meshRevision, uint64_t cullRevision)
{
  IUMeshGpuStore &store = MeshStore();
  const bool mdi_indirect_cull = store.SupportsMultiDrawIndirect();

  // V2 / Closeout D hard lock: always use caller-filtered slice-ready refs.
  // CollectAll must not bypass IsChunkSliceRenderReady. Opaque must NEVER
  // gate on ColumnEmergeState::RenderReady (settle telem ≠ draw residency).
  // Empty refs means nothing ready — do not re-expand from the pool.
  // Frustum cull still runs via ApplyGpuCompactCull on this gated set.
  std::vector<GreedyBatchRef> upload_refs = opaqueCutoutRefs;

  if (WorldInstance)
  {
    const glm::ivec3 focus_block = WorldInstance->GetPreferredLoadFocusBlock();
    const glm::ivec3 focus_chunk = UChunkManager::WorldToChunk(focus_block);
    bool underfeet_in_draw = false;
    for (const GreedyBatchRef &ref : upload_refs)
    {
      if (ref.chunkCoord.x == focus_chunk.x &&
          ref.chunkCoord.z == focus_chunk.z)
      {
        underfeet_in_draw = true;
        break;
      }
    }
    WorldInstance->GetPhysicsTelemetryMutable().UnderfeetOpaquePresent =
        underfeet_in_draw ? 1 : 0;
    // Post-draw reconcile: streaming sampled underfeet before opaque pass.
    if (underfeet_in_draw)
    {
      auto &phys = WorldInstance->GetPhysicsTelemetryMutable();
      phys.UnderfeetHasMesh = 1;
      if (phys.UnderfeetReason ==
              static_cast<int>(
                  ColumnRenderableState::BlockReason::NotReadyState) ||
          phys.UnderfeetReason ==
              static_cast<int>(ColumnRenderableState::BlockReason::NotLoaded))
      {
        phys.UnderfeetReason =
            static_cast<int>(ColumnRenderableState::BlockReason::None);
      }
    }
  }

  std::vector<GreedyBatchRef> solid;
  std::vector<GreedyBatchRef> cutout;
  solid.reserve(upload_refs.size());
  cutout.reserve(upload_refs.size());
  for (const GreedyBatchRef &ref : upload_refs)
  {
    const GreedyMeshBatch *batch = cache.TryGetGreedyBatch(ref);
    if (!batch)
    {
      continue;
    }
    if (batch->AlphaCutout)
    {
      cutout.push_back(ref);
    }
    else
    {
      solid.push_back(ref);
    }
  }
  // GPF5: one opaque MDI pass for solid+cutout (alphaCutout shader path).
  std::vector<GreedyBatchRef> opaque_draw;
  opaque_draw.reserve(solid.size() + cutout.size());
  opaque_draw.insert(opaque_draw.end(), solid.begin(), solid.end());
  opaque_draw.insert(opaque_draw.end(), cutout.begin(), cutout.end());
  if (WorldInstance)
  {
    WorldInstance->GetPhysicsTelemetryMutable().OpaqueMdiEligible =
        static_cast<uint64_t>(opaque_draw.size());
  }
  // Sort by blockId so MDI can MultiDraw contiguous same-texture runs.
  // sort_revision=1 invalidates pre-sort pool layouts (was always 0).
  constexpr uint64_t kBlockIdSortRev = 1;
  auto by_block_id = [&](const GreedyBatchRef &ra, const GreedyBatchRef &rb)
  {
    const GreedyMeshBatch *a = cache.TryGetGreedyBatch(ra);
    const GreedyMeshBatch *b = cache.TryGetGreedyBatch(rb);
    if (!a || !b)
    {
      return a != nullptr;
    }
    return a->blockId < b->blockId;
  };

  auto *mdi = mdi_indirect_cull
                  ? dynamic_cast<UMdiVertexPoolStore *>(&store)
                  : nullptr;
  const Frustum frustum = Frustum::FromViewProjection(vp);
  // Must match ChunkMeshCache::RebuildVisible (distance admit). 0 = plane-only
  // and false-negatives hide terrain while cross (built with MaxCullDistance) remains.
  const float max_cull_distance = cache.MaxCullDistance();
  const bool horizontal_cull = cache.UseHorizontalCullDistance();

  if (!opaque_draw.empty())
  {
    std::sort(opaque_draw.begin(), opaque_draw.end(), by_block_id);
    GLboolean cullWasEnabled = GL_TRUE;
    if (!cutout.empty())
    {
      glGetBooleanv(GL_CULL_FACE, &cullWasEnabled);
      glDisable(GL_CULL_FACE);
    }
    store.RefreshPassRefs(GreedyGpuOpaque, cache, opaque_draw, meshRevision,
                          cullRevision, kBlockIdSortRev);
    if (mdi)
    {
      mdi->SetCullStatsReadbackEnabled(ShowPerformance);
      mdi->ApplyGpuCompactCull(GreedyGpuOpaque, frustum, cameraPos,
                               max_cull_distance, horizontal_cull);
    }
    DrawGreedyGpuBatches(GreedyGpuOpaque, vp, textures, true, false,
                         GreedyShaderMode::TransparentColor, 0.0f);
    if (!cutout.empty() && cullWasEnabled)
    {
      glEnable(GL_CULL_FACE);
    }
  }
  if (!cutout.empty())
  {
    MeshStore().DestroyPass(GreedyGpuCutout);
  }
  if (WorldInstance)
  {
    const size_t used = GreedyGpuOpaque.VertexPool.UsedBytes() +
                        GreedyGpuCutout.VertexPool.UsedBytes() +
                        GreedyGpuTransparent.VertexPool.UsedBytes();
    const size_t cap = GreedyGpuOpaque.VertexPool.CapacityBytes() +
                       GreedyGpuCutout.VertexPool.CapacityBytes() +
                       GreedyGpuTransparent.VertexPool.CapacityBytes();
    auto &phys = WorldInstance->GetPhysicsTelemetryMutable();
    phys.GpuCullGpuMs = 0.0;
    phys.GpuPoolUsedMb = static_cast<double>(used) / (1024.0 * 1024.0);
    phys.GpuPoolCapMb = static_cast<double>(cap) / (1024.0 * 1024.0);
    phys.VertexPoolFill =
        cap > 0 ? static_cast<double>(used) / static_cast<double>(cap) : 0.0;
    phys.PoolUnsyncUploads =
        GreedyGpuOpaque.VertexPool.ConsumeUnsyncUploads() +
        GreedyGpuCutout.VertexPool.ConsumeUnsyncUploads() +
        GreedyGpuTransparent.VertexPool.ConsumeUnsyncUploads();
    phys.PoolFenceWaitMs =
        GreedyGpuOpaque.VertexPool.ConsumeFenceWaitMs() +
        GreedyGpuCutout.VertexPool.ConsumeFenceWaitMs() +
        GreedyGpuTransparent.VertexPool.ConsumeFenceWaitMs();
    if (mdi)
    {
      phys.OpaqueCmdTotal = mdi->LastCullOpaqueTotal();
      phys.OpaqueCmdOn = mdi->LastCullOpaqueOn();
      phys.CpuAabbWouldOn = mdi->LastCpuAabbWouldOn();
      phys.ChunkMeshedCulled0 =
          phys.OpaqueCmdTotal > phys.OpaqueCmdOn
              ? phys.OpaqueCmdTotal - phys.OpaqueCmdOn
              : 0;
      phys.GpuCullIndirect = 1.0;
      phys.GpuCullGpuMs = mdi->LastCompactCullGpuMs();
    }
  }

  const size_t packed_opaque_drawn =
      DrawPackedGpuMeshes(cache, cache.GetGpuPackedOpaqueRefs(), vp, textures, false,
                          GreedyShaderMode::TransparentColor, 0.0f);
  if (WorldInstance)
  {
    auto &phys = WorldInstance->GetPhysicsTelemetryMutable();
    phys.OpaqueGpuPackedN = static_cast<uint64_t>(packed_opaque_drawn);
    phys.OpaqueDrawN = phys.OpaqueCmdOn + phys.OpaqueGpuPackedN;
  }
}

size_t UGeometryEngine::DrawPackedGpuMeshes(
    const UChunkMeshCache &cache,
    const std::vector<GpuPackedChunkRef> &chunk_refs, const glm::mat4 &vp,
    const std::map<size_t, UTextureCube> &textures, bool transparent_pass,
    GreedyShaderMode mode, float shell_alpha)
{
  const UGpuMeshPipeline *pipeline = cache.GetGpuMeshPipeline();
  if (!pipeline || !pipeline->IsReady() || chunk_refs.empty())
  {
    return 0;
  }
  if (!packedGreedyShader || !packedGreedyShader->IsValid())
  {
    return 0;
  }
  if (greedyMeshVAO == 0 && !InitGreedyMeshBuffers())
  {
    return 0;
  }

  packedGreedyShader->Use();
  packedGreedyShader->SetMat4("mvp_matrix", vp);
  packedGreedyShader->SetInt("texture0", 0);
  SetGreedyShaderMode(packedGreedyShader, false, transparent_pass, mode,
                      shell_alpha);
  const bool opaque_depth_guard =
      transparent_pass && mode != GreedyShaderMode::ShellDepthPrepass;
  if (opaque_depth_guard)
  {
    OpaqueDepthCapture.Bind();
  }
  OpaqueDepthCapture.ApplyShaderUniforms(packedGreedyShader, opaque_depth_guard);
  if (WorldInstance)
  {
    if (auto camera = WorldInstance->GetCurrentUserCamera())
    {
      ApplyFogUniforms(
          packedGreedyShader, camera->GetPosition(),
          cutum::ShouldApplyBelowSurfaceFogToPass(transparent_pass, false));
    }
    ApplyGreedyEnvironmentUniforms(packedGreedyShader);
  }
  glActiveTexture(GL_TEXTURE0);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0,
                   pipeline->GetAllocator().GetQuadSsbo());
  glBindVertexArray(greedyMeshVAO);

  size_t packed_draw_chunks = 0;
  for (const GpuPackedChunkRef &chunk : chunk_refs)
  {
    const GpuMeshSlot *slot =
        pipeline->GetAllocator().GetSlot(chunk.chunkCoord);
    if (!slot || slot->QuadCount == 0)
    {
      continue;
    }
    ++packed_draw_chunks;
    const glm::vec3 origin =
        glm::vec3(chunk.chunkCoord * CHUNK_SIZE);
    packedGreedyShader->SetVec3("chunkOrigin", origin);
    for (const GpuBlockDrawRange &range : chunk.blockRanges)
    {
      if (range.Transparent != transparent_pass)
      {
        continue;
      }
      const auto texIt = textures.find(static_cast<size_t>(range.blockId));
      if (texIt == textures.end())
      {
        continue;
      }
      const GLuint texture_id = texIt->second.GetTextureId();
      if (texture_id == 0)
      {
        continue;
      }
      SetBlockAnimUniforms(packedGreedyShader, range.blockId, textures);
      glBindTexture(GL_TEXTURE_2D, texture_id);
      const GLint first =
          static_cast<GLint>((slot->OffsetQuads + range.quadOffset) * 6u);
      const GLsizei count = static_cast<GLsizei>(range.quadCount * 6u);
      if (count <= 0)
      {
        continue;
      }
      glDrawArrays(GL_TRIANGLES, first, count);
    }
  }

  glBindVertexArray(0);
  packedGreedyShader->Unuse();
  return packed_draw_chunks;
}

namespace
{

uint64_t TransparentRefListFingerprint(const std::vector<GreedyBatchRef> &refs)
{
  uint64_t h = refs.size();
  for (const GreedyBatchRef &r : refs)
  {
    h ^= (static_cast<uint64_t>(static_cast<uint32_t>(r.chunkCoord.x)) << 42) ^
         (static_cast<uint64_t>(static_cast<uint32_t>(r.chunkCoord.y)) << 21) ^
         static_cast<uint64_t>(static_cast<uint32_t>(r.chunkCoord.z));
    h ^= static_cast<uint64_t>(r.batchIndex) * 0x9e3779b97f4a7c15ull;
  }
  return h;
}

} // namespace

void UGeometryEngine::PrepareTransparent(
    const GreedyTransparentDrawContext &ctx)
{
  PreparedTransparentCache = &ctx.cache;
  std::vector<GreedyBatchRef> filtered;
  filtered.reserve(ctx.transparentRefs.size());
  for (const GreedyBatchRef &ref : ctx.transparentRefs)
  {
    const GreedyMeshBatch *batch = ctx.cache.TryGetGreedyBatch(ref);
    if (!batch)
    {
      continue;
    }
    if (ctx.blockRegistry.GetRenderStyle(batch->blockId) ==
        BlockRenderStyle::Cross)
    {
      continue;
    }
    filtered.push_back(ref);
  }
  if (filtered.empty())
  {
    MeshStore().DestroyPass(GreedyGpuTransparent);
    PreparedTransparentTextures = nullptr;
    CachedTransparentSortedRefs.clear();
    CachedTransparentSortRevision = 0;
    CachedTransparentMeshRevision = 0;
    CachedTransparentRefFingerprint = 0;
    return;
  }
  const uint64_t sortRevision = GreedyTransparentSortRevision(ctx.cameraPos);
  const uint64_t refFingerprint = TransparentRefListFingerprint(filtered);
  const bool sort_inputs_unchanged =
      sortRevision == CachedTransparentSortRevision &&
      ctx.meshRevision == CachedTransparentMeshRevision &&
      refFingerprint == CachedTransparentRefFingerprint &&
      !CachedTransparentSortedRefs.empty();
  if (sort_inputs_unchanged && !CachedTransparentSortedRefs.empty())
  {
    filtered = CachedTransparentSortedRefs;
  }
  else
  {
    SortTransparentGreedyBatches(filtered, ctx.cache, ctx.cameraPos,
                                 ctx.blockRegistry);
    CachedTransparentSortedRefs = filtered;
    CachedTransparentSortRevision = sortRevision;
    CachedTransparentMeshRevision = ctx.meshRevision;
    CachedTransparentRefFingerprint = refFingerprint;
  }
  MeshStore().RefreshPassRefs(GreedyGpuTransparent, ctx.cache, filtered,
                                   ctx.meshRevision, ctx.cullRevision,
                                   sortRevision);
  if (auto *mdi = dynamic_cast<UMdiVertexPoolStore *>(&MeshStore()))
  {
    const Frustum frustum = Frustum::FromViewProjection(ctx.viewProjection);
    mdi->ApplyGpuCompactCull(GreedyGpuTransparent, frustum, ctx.cameraPos,
                             ctx.cache.MaxCullDistance(),
                             ctx.cache.UseHorizontalCullDistance());
    if (WorldInstance)
    {
      WorldInstance->GetPhysicsTelemetryMutable().GpuCullGpuMs +=
          mdi->LastCompactCullGpuMs();
    }
  }
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
  const bool kAlphaCutout =
      !GetActiveRenderBackendCaps().PreferSinglePassTransparent;
  DrawGreedyGpuBatches(GreedyGpuTransparent, PreparedTransparentVp,
                       *PreparedTransparentTextures, kAlphaCutout, true, mode,
                       shellAlpha);
  if (PreparedTransparentCache)
  {
    DrawPackedGpuMeshes(*PreparedTransparentCache,
                        PreparedTransparentCache->GetGpuPackedTransparentRefs(),
                        PreparedTransparentVp, *PreparedTransparentTextures,
                        true, mode, shellAlpha);
  }
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
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, kStride,
                        (void *)(offsetof(GreedyMeshVertex, skyLight)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, kStride,
                        (void *)(offsetof(GreedyMeshVertex, blockLight)));
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, kStride,
                        (void *)(offsetof(GreedyMeshVertex, wetness)));
  glEnableVertexAttribArray(5);
  glBindVertexArray(0);
  return greedyMeshVAO != 0;
}

void UGeometryEngine::DestroyGreedyMeshBuffers()
{
  MeshStore().DestroyAll(GreedyGpuOpaque, GreedyGpuCutout,
                         GreedyGpuTransparent);
  CrossGpuBackend.DestroyAll(CrossGpuPass);
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

void UGeometryEngine::DrawSkyGradient()
{
  if (!WorldInstance)
  {
    return;
  }

  WorldInstance->EnsureDefaultCelestialBodies();
  WorldInstance->RefreshSkyVisualStateForRender();

  float horizon_boost = 0.0f;
  const UWorld::EnvironmentState &env = WorldInstance->GetEnvironmentState();
  horizon_boost = std::clamp(
      env.Cloudiness * 0.14f + env.PrecipitationIntensity * 0.1f, 0.0f, 0.35f);
  if (Render.AltitudeAdaptiveFog)
  {
    const float altitude = WorldInstance->GetAltitudeAboveTerrain();
    const float threshold =
        static_cast<float>(std::max(1, Render.AltitudeFogThresholdBlocks));
    horizon_boost += std::clamp(altitude / (threshold * 2.0f), 0.0f, 0.25f);
  }

  glm::mat3 inv_view_rot(1.0f);
  glm::vec3 camera_pos(0.0f);
  if (auto camera = WorldInstance->GetCurrentUserCamera())
  {
    const glm::mat3 view_rot = glm::mat3(camera->GetViewMatrix());
    inv_view_rot = glm::transpose(view_rot);
    camera_pos = camera->GetPosition();
  }

  SkyGradientPass_.Draw(skyShader, skyColor, UnderwaterFogPass_, env, Render,
                        EffectiveWeatherPreset(), AnimationClock.ElapsedSeconds(),
                        inv_view_rot, camera_pos, horizon_boost);
  DurationSkyGradientMks = SkyGradientPass_.GetLastDrawMs();
}

void UGeometryEngine::DrawSkyGradientSimple() { DrawSkyGradient(); }

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

void UGeometryEngine::RenderWeatherOverlay(int width, int height)
{
  if (!WorldInstance || width <= 0 || height <= 0 || !InitOverlayBuffers())
  {
    return;
  }

  auto camera = WorldInstance->GetCurrentUserCamera();
  if (!camera)
  {
    return;
  }

  const glm::mat4 view = camera->GetViewMatrix();
  const glm::mat4 proj = camera->GetProjection();
  const glm::mat4 view_proj = proj * view;
  const glm::mat3 view_rot = glm::mat3(view);
  const glm::vec3 camera_right = glm::normalize(glm::vec3(view_rot[0]));
  const glm::vec3 camera_up = glm::normalize(glm::vec3(view_rot[1]));

  WeatherRenderContext ctx;
  ctx.Width = width;
  ctx.Height = height;
  ctx.OverlayVao = overlayVAO;
  ctx.ViewProj = view_proj;
  ctx.CameraPos = camera->GetPosition();
  ctx.CameraRight = camera_right;
  ctx.CameraUp = camera_up;
  ctx.ElapsedSec = AnimationClock.ElapsedSeconds();
  ctx.DeltaSec = static_cast<float>(camera->GetDeltaTime());
  ctx.Preset = EffectiveWeatherPreset();

  WeatherPass.Render(ctx, *WorldInstance);
  DurationWeatherStreakMks = WeatherPass.GetLastStreakMs();
  DurationWeatherParticleMks = WeatherPass.GetLastParticleMs();
}

// Methods for sky color management
void UGeometryEngine::SetSkyColor(float r, float g, float b, float a)
{
  BaseSkyColor = glm::vec3(r, g, b);
  UnderwaterFogPass_.ResetSkyTint(BaseSkyColor);
  skyColor = glm::vec4(r, g, b, a);
}

void UGeometryEngine::SetSkyColor(const glm::vec4 &color)
{
  BaseSkyColor = glm::vec3(color);
  UnderwaterFogPass_.ResetSkyTint(BaseSkyColor);
  skyColor = color;
}

glm::vec4 UGeometryEngine::GetSkyColor() const { return skyColor; }

void UGeometryEngine::SetGradientSky(bool useGradient)
{
  useGradientSky = useGradient;
}

bool UGeometryEngine::IsGradientSky() const { return useGradientSky; }

void UGeometryEngine::SetOverlayMargins(int left, int right, int top)
{
  OverlayMarginLeft = std::max(10, left);
  OverlayMarginRight = std::max(10, right);
  OverlayMarginTop = std::max(30, top);
}

namespace
{

struct OverlaySection
{
  const char *Title{nullptr};
  std::vector<std::string> Lines;
};

size_t OverlaySectionLineCount(const OverlaySection &section)
{
  if (section.Lines.empty())
  {
    return 0;
  }
  return 1 + section.Lines.size();
}

void AppendOverlaySection(std::vector<std::string> &out,
                          const OverlaySection &section)
{
  if (section.Lines.empty())
  {
    return;
  }
  out.emplace_back(section.Title);
  out.insert(out.end(), section.Lines.begin(), section.Lines.end());
}

size_t CountOverlayLines(const std::vector<OverlaySection> &sections)
{
  size_t total = 0;
  for (const OverlaySection &section : sections)
  {
    total += OverlaySectionLineCount(section);
  }
  return total;
}

} // namespace

void UGeometryEngine::RenderPerformanceText(int width_size, int height_size,
                                            double view_duration)
{
  if (!textRenderer)
  {
    return;
  }

  textRenderer->SetWindowSize(width_size, height_size);

  constexpr float kScale = 0.7f;
  constexpr float kLineHeight = 18.0f;
  const glm::vec3 textColor(1.0f, 1.0f, 0.0f);

  const PhysicsTelemetry &phys = WorldInstance->GetPhysicsTelemetry();
  const double sim_ms = phys.PhysicsStepMs + (view_duration / 1000.0) +
                        (DurationDrawSceneMks / 1000.0);
  const double wall_ms = WorldInstance->GetWallFrameDelta() * 1000.0;
  const double sim_fps = sim_ms > 0.0 ? 1000.0 / sim_ms : 0.0;
  const double wall_fps = wall_ms > 0.0 ? 1000.0 / wall_ms : sim_fps;

  const size_t blockCount = WorldInstance->GetCachedBlockCount();
  const size_t drawCount = WorldInstance->GetRenderInstanceCount();
  const auto &md = WorldInstance->GetMovementDiagnostics();

  OverlaySection frame{"Frame:",
                       {"Wall FPS: " + std::to_string(wall_fps).substr(0, 6),
                        "Sim FPS: " + std::to_string(sim_fps).substr(0, 6),
                        "Swap: " +
                            std::to_string(WorldInstance->GetLastSwapWaitMs())
                                .substr(0, 6) +
                            " ms",
                        "Blocks: " + std::to_string(blockCount) +
                            " draw: " + std::to_string(drawCount)}};

  OverlaySection timing{
      "Timing:",
      {"Phys: " + std::to_string(phys.PhysicsStepMs).substr(0, 6) + " ms" +
           " steps: " + std::to_string(phys.SimulationStepsThisFrame),
       "Move: " + std::to_string(phys.MovementStepMs).substr(0, 6) + " ms" +
           " Block: " + std::to_string(phys.BlockStepMs).substr(0, 6) + " ms" +
           " Drain: " + std::to_string(phys.DrainStepMs).substr(0, 6) + " ms",
       "Scene: " + std::to_string(DurationDrawSceneMks / 1000.0).substr(0, 6) +
           " ms" + " View: " +
           std::to_string(view_duration / 1000.0).substr(0, 6) + " ms",
       "Weather: streak " +
           std::to_string(DurationWeatherStreakMks).substr(0, 5) + " ms" +
           " particle " +
           std::to_string(DurationWeatherParticleMks).substr(0, 5) + " ms" +
           " sky " + std::to_string(DurationSkyGradientMks).substr(0, 5) +
           " ms" + " n=" +
           std::to_string(WeatherPass.GetActiveParticleCount())}};

  OverlaySection streaming{"Streaming:", {}};
  streaming.Lines.push_back(
      "Flat: " + std::to_string(md.flatRebuildMs).substr(0, 5) + " ms" +
      " Greedy: " + std::to_string(md.greedyCacheEntries) +
      " Dirty: " + std::to_string(md.dirtyChunksPending) +
      " MeshAsync: " + std::to_string(md.asyncMeshInFlight) +
      " GenQ: " + std::to_string(md.genQueuePending) + "/" +
      std::to_string(md.genInFlight));
  streaming.Lines.push_back(
      "Populate: " + std::to_string(md.populateMsLast).substr(0, 5) + " ms" +
      " ema " + std::to_string(md.populateMsEma).substr(0, 5) + " (t " +
      std::to_string(md.populateTerrainMs).substr(0, 4) + " c " +
      std::to_string(md.populateCarveMs).substr(0, 4) + " p " +
      std::to_string(md.populatePostMs).substr(0, 4) + ")");
  streaming.Lines.push_back(
      "Relight: p=" + std::to_string(md.pendingPlayerRelights) +
      " bg=" + std::to_string(md.pendingBgRelights) +
      " inflight=" + std::to_string(md.asyncRelightInflight) +
      " late=" + std::to_string(md.relightDiscardedLate));
  streaming.Lines.push_back(
      "rebuilt: " + std::to_string(md.meshRebuildsThisFrame) +
      " hitch: " + (md.hitchDetected ? "yes" : "no"));
  if (md.streamingGenMs > 0.01 || md.meshRebuildMs > 0.01 ||
      md.streamingIoMs > 0.01)
  {
    streaming.Lines.push_back(
        "Gen: " + std::to_string(md.streamingGenMs).substr(0, 5) + " ms" +
        " Mesh: " + std::to_string(md.meshRebuildMs).substr(0, 5) + " ms" +
        " IO: " + std::to_string(md.streamingIoMs).substr(0, 5) + " ms");
  }

  OverlaySection physics{"Physics:", {}};
  {
    const bool collision_ready =
        md.feetChunkLoaded && !md.feetInUnloadList && !md.fallThroughSuspected;
    physics.Lines.push_back(
        std::string("Phys: ") +
        cutum::ToString(WorldInstance->GetPhysicsProfile()) +
        " CollReady: " + (collision_ready ? "yes" : "no"));
    physics.Lines.push_back(
        "BlockQ: " + std::to_string(md.physicsBlockQueueDepth) +
        " LiqQ: " + std::to_string(md.physicsLiquidQueueDepth) +
        " Purged: " + std::to_string(md.physicsPurgedUpdates));
    physics.Lines.push_back(
        "RemeshQ: " + std::to_string(md.physicsVisualRemeshBacklog) +
        " CollQ: " + std::to_string(md.physicsCollisionRebuildBacklog));
    physics.Lines.push_back(
        "BP rej: " + std::to_string(md.physicsCollisionBroadphaseRejects) +
        " fb: " + std::to_string(md.physicsCollisionBroadphaseFallbacks) +
        " wait: " +
        std::to_string(md.physicsCollisionReadyWaitMs).substr(0, 6) + "ms");
    if (WorldInstance->GetPhysicsFeatureFlags().LiquidDebugTrace)
    {
      std::vector<cutum::LiquidDebugEntry> liquid_trace;
      cutum::ULiquidDebugTrace::Instance().CopyRecent(liquid_trace);
      const size_t start =
          liquid_trace.size() > 3 ? liquid_trace.size() - 3 : 0;
      for (size_t i = start; i < liquid_trace.size(); ++i)
      {
        const cutum::LiquidDebugEntry &entry = liquid_trace[i];
        physics.Lines.push_back(
            "Liq " + std::string(entry.Reason) + " (" +
            std::to_string(entry.From.x) + "," + std::to_string(entry.From.y) +
            "," + std::to_string(entry.From.z) + ")->(" +
            std::to_string(entry.To.x) + "," + std::to_string(entry.To.y) +
            "," + std::to_string(entry.To.z) + ")");
      }
    }
  }

  OverlaySection world{"World:", {}};
  world.Lines.push_back("Weather: " + WorldInstance->GetWeatherName());
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
    world.Lines.push_back(std::string("Biome: ") + BiomeIdToString(biome) +
                          " T:" + std::to_string(temperature).substr(0, 4) +
                          " M:" + std::to_string(moisture).substr(0, 4));
  }

  OverlaySection creatures{
      "Creatures:",
      {"Creatures: " + std::to_string(CreatureDraw_.GetStats().CreaturesDrawn) +
       "/" + std::to_string(CreatureDraw_.GetStats().CreaturesConsidered) +
       " culled: " + std::to_string(CreatureDraw_.GetStats().CreaturesCulled) +
       " draws: " + std::to_string(CreatureDraw_.GetStats().CreatureDrawCalls) +
       " bone uploads: " +
       std::to_string(CreatureDraw_.GetStats().CreatureBoneMatrixUploads)}};

  OverlaySection safety{"Safety:", {}};
  if (md.fallThroughSuspected || md.feetInUnloadList)
  {
    safety.Lines.push_back("Dt: " + std::to_string(md.deltaTime).substr(0, 5) +
                           " yDrop: " +
                           std::to_string(md.playerYDrop).substr(0, 5));
    safety.Lines.push_back(
        std::string("Feet chunk: ") + (md.feetChunkLoaded ? "OK" : "MISSING") +
        (md.feetIsAir ? " AIR" : " SOLID") +
        " unloads: " + std::to_string(md.streamingUnloads));
    if (md.fallThroughSuspected)
    {
      safety.Lines.emplace_back("!! fall-through suspected !!");
    }
  }

  std::vector<OverlaySection> rightSections = {std::move(frame),
                                               std::move(timing),
                                               std::move(streaming)};
  std::vector<OverlaySection> leftSections = {
      std::move(physics), std::move(world), std::move(creatures),
      std::move(safety)};

  const float availH = static_cast<float>(height_size) -
                       static_cast<float>(OverlayMarginTop) -
                       static_cast<float>(OverlayMarginBottom);
  const size_t maxLines =
      availH > 0.0f ? static_cast<size_t>(availH / kLineHeight) : 0;
  const size_t totalLines =
      CountOverlayLines(rightSections) + CountOverlayLines(leftSections);
  const bool useDualColumn = totalLines > maxLines && maxLines > 0;

  if (!useDualColumn)
  {
    leftSections.insert(leftSections.begin(),
                        std::make_move_iterator(rightSections.begin()),
                        std::make_move_iterator(rightSections.end()));
    rightSections.clear();
    rightSections.swap(leftSections);
  }
  else
  {
    while (CountOverlayLines(rightSections) > maxLines &&
           rightSections.size() > 1)
    {
      leftSections.insert(leftSections.begin(),
                          std::move(rightSections.back()));
      rightSections.pop_back();
    }
  }

  auto renderColumn = [&](const std::vector<OverlaySection> &sections,
                          bool rightAlign)
  {
    std::vector<std::string> lines;
    for (const OverlaySection &section : sections)
    {
      AppendOverlaySection(lines, section);
    }
    if (maxLines > 0 && lines.size() > maxLines)
    {
      lines.resize(maxLines);
    }

    float y =
        static_cast<float>(height_size) - static_cast<float>(OverlayMarginTop);
    for (const std::string &line : lines)
    {
      const glm::vec2 textSize = textRenderer->GetTextSize(line, kScale);
      const float x =
          rightAlign
              ? static_cast<float>(width_size) - textSize.x -
                    static_cast<float>(OverlayMarginRight)
              : static_cast<float>(OverlayMarginLeft);
      textRenderer->RenderText(line, x, y, kScale, textColor);
      y -= kLineHeight;
    }
  };

  if (useDualColumn)
  {
    renderColumn(leftSections, false);
  }
  renderColumn(rightSections, true);
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

  UGlStateScope glGuard(kGlMaskOverlay2D);
  glDisable(GL_DEPTH_TEST);

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
      "0-9 / mouse wheel - Primary hotbar; objects via HUD / palette",
      "Classic: mouse look, hold LMB break, RMB place/use slot",
      "Cubatarium: RMB drag look, LMB tap place/use slot / hold break",
      "Delete - Instant break, F8 weather",
  };

  constexpr float helpX = 20.0f;
  float y = 20.0f;
  for (const std::string &line : helpLines)
  {
    textRenderer->RenderText(line, helpX, y, scale, helpColor);
    y += 20.0f;
  }

  const UWorld::EnvironmentState &env = WorldInstance->GetEnvironmentState();
  std::vector<std::string> weatherLines;
  weatherLines.push_back(
      "Weather: " + UWorld::WeatherTypeToString(env.Weather) + " -> " +
      UWorld::WeatherTypeToString(env.TargetWeather));
  weatherLines.push_back(
      "Precip: " + std::to_string(env.PrecipitationIntensity).substr(0, 4) +
      " Overlay: " +
      (WorldInstance->GetLightingSettings().WeatherOverlayEnabled ? "on"
                                                                  : "off") +
      " Particles: " +
      (WorldInstance->GetLightingSettings().WeatherParticlesEnabled ? "on"
                                                                    : "off"));
  float weatherY = static_cast<float>(height_size) - 52.0f;
  for (const std::string &line : weatherLines)
  {
    const glm::vec2 textSize = textRenderer->GetTextSize(line, 0.75f);
    const float weatherX = static_cast<float>(width_size) - textSize.x - 16.0f;
    textRenderer->RenderText(line, weatherX, weatherY, 0.75f,
                             glm::vec3(0.9f, 0.95f, 1.0f));
    weatherY -= 18.0f;
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

  UGlStateScope glGuard(kGlMaskOverlay2D);
  glDisable(GL_DEPTH_TEST);

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
    prefabName = controlled->GetInventory().GetActiveObjectName();
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
  glDisable(GL_DEPTH_TEST);

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

void UGeometryEngine::DrawCreatureTexturedPart(const glm::mat4 &mvp,
                                               GLuint texture,
                                               CreaturePartMesh mesh)
{
  CreatureDraw_.DrawTexturedPart(mvp, texture, mesh);
}

void UGeometryEngine::DrawCreatureBoneSkeletonMesh(
    const glm::mat4 &mvp, GLuint texture, const BoneSkeletonCubeMeshCpu &mesh)
{
  CreatureDraw_.DrawBoneSkeletonMesh(mvp, texture, mesh);
}

void UGeometryEngine::DrawCreatureGltfMesh(const glm::mat4 &mvp, GLuint texture,
                                           const BoneSkeletonCubeMeshCpu &mesh)
{
  CreatureDraw_.DrawGltfMesh(mvp, texture, mesh);
}

void UGeometryEngine::DrawCreatureSkinnedMesh(
    const glm::mat4 &mvp, GLuint texture, const GltfPrimitiveCpu &mesh,
    const std::vector<glm::mat4> &boneMatrices)
{
  CreatureDraw_.DrawSkinnedMesh(mvp, texture, mesh, boneMatrices);
}

void UGeometryEngine::DrawCreatureBillboard(const glm::mat4 &mvp,
                                            GLuint texture,
                                            const glm::vec4 &tint)
{
  CreatureDraw_.DrawBillboard(mvp, texture, tint);
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
  auto camera = WorldInstance->GetCurrentUserCamera();
  if (!blockPos || !camera)
  {
    return;
  }

  const glm::mat4 view = camera->GetViewMatrix();
  const glm::mat4 proj = camera->GetProjection();
  BlockCrackOverlayRequest crack_request;
  crack_request.ViewProj = proj * view;
  crack_request.BlockPos = *blockPos;
  crack_request.Progress = WorldInstance->GetBreakProgress();
  if (BlockCrackPass.Render(crack_request))
  {
    return;
  }

  // TD-BB-004: wireframe stand-in while destroy_stage textures are unavailable.
  if (!outlineShader || !outlineShader->IsValid() || outlineVAO == 0)
  {
    return;
  }

  const float progress = WorldInstance->GetBreakProgress();
  const glm::mat4 viewProj = proj * view;
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

namespace
{

glm::vec4 BiomeDebugColor(BiomeId biome)
{
  const float hue = static_cast<float>(BiomeIndex(biome)) * 0.618033988f;
  return glm::vec4(0.45f + 0.45f * std::sin(hue),
                   0.45f + 0.45f * std::sin(hue + 2.094f),
                   0.45f + 0.45f * std::sin(hue + 4.188f), 0.55f);
}

} // namespace

void UGeometryEngine::RenderBiomeDebugOverlay()
{
  if (!WorldInstance)
  {
    return;
  }
  const ProceduralSettings settings = WorldInstance->GetProceduralSettings();
  if (!settings.DebugWorldGenOverlay)
  {
    return;
  }
  auto camera = WorldInstance->GetCurrentUserCamera();
  if (!camera)
  {
    return;
  }
  const glm::vec3 camPos = camera->GetPosition();
  const int centerX = static_cast<int>(std::floor(camPos.x));
  const int centerZ = static_cast<int>(std::floor(camPos.z));
  constexpr int kRadius = 12;
  constexpr int kStep = 2;
  const glm::mat4 mvp = camera->GetMvpMatrix();

  for (int dx = -kRadius; dx <= kRadius; dx += kStep)
  {
    for (int dz = -kRadius; dz <= kRadius; dz += kStep)
    {
      const int worldX = centerX + dx;
      const int worldZ = centerZ + dz;
      const std::optional<int> surfaceY =
          WorldInstance->FindHighestSolidY(worldX, worldZ);
      if (!surfaceY)
      {
        continue;
      }
      float temperature = 0.0f;
      float moisture = 0.0f;
      ComputeBiomeClimate(worldX, worldZ, settings.Seed, temperature, moisture);
      const float localHeightNorm =
          std::clamp(static_cast<float>(*surfaceY - settings.SeaLevel) /
                         static_cast<float>(std::max(1, settings.MaxHeight -
                                                            settings.SeaLevel)),
                     0.0f, 1.0f);
      const BiomeId biome =
          ClassifyBiome(temperature, moisture, localHeightNorm);
      const glm::vec4 color = BiomeDebugColor(biome);
      glm::mat4 model(1.0f);
      model =
          glm::translate(model, glm::vec3(static_cast<float>(worldX) + 0.5f,
                                          static_cast<float>(*surfaceY) + 0.08f,
                                          static_cast<float>(worldZ) + 0.5f));
      model = glm::scale(model, glm::vec3(0.92f, 0.04f, 0.92f));
      DrawBoxWireframe(mvp * model, color);
    }
  }
}

} // namespace cutum

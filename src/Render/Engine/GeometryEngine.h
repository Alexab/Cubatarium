#ifndef GEOMETRYENGINE_H
#define GEOMETRYENGINE_H

#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/CreatureRenderStats.h"
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include "Game/Interfaces/IUGameContent.h"
#include "Render/Engine/CreatureDrawPass.h"

// GLEW will be included in .cpp file after GLFW initialization
// Forward declaration for OpenGL Types
typedef unsigned int GLuint;
typedef float GLfloat;
typedef int GLint;

#include "App/Settings/RenderSettings.h"
#include "Render/Engine/AnimationClock.h"
#include "Render/Engine/CrossGpuBackend.h"
#include "Render/Engine/FluidSurfaceMap.h"
#include "Render/Engine/IUFluidSurfaceProvider.h"
#include "Render/Engine/GreedyGpuBackend.h"
#include "Render/Engine/IUMeshGpuStore.h"
#include "Render/Backend/RenderBackendFactory.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/Engine/SkyGradientPass.h"
#include "Render/Engine/TextRenderer.h"
#include "Render/Engine/UnderwaterFogPass.h"
#include "Render/Mesh/ChunkMeshCache.h"
#include "Render/Mesh/GreedyMeshVertex.h"
#include "Render/Pipeline/GreedyShaderMode.h"
#include "Render/Pipeline/IUGreedyTransparentBackend.h"
#include "Render/Pipeline/OpaqueDepthCapture.h"
#include "Render/Primitives/CubeGL.h"
#include "Render/Textures/TextureBase.h"
#include "Render/Textures/TextureCube.h"
#include "Render/Weather/WeatherRenderPass.h"
#include "World/Core/FluidColumnSurfaceQuery.h"
#include "World/Interfaces/IUWorldRenderReadModel.h"
#include "World/Math/GridMath.h"
#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace cutum
{

class IUGameContent;
class UCreatureTextureStorage;
class UWorld;

// Structure for batch rendering
struct RenderBatch
{
  GLuint textureID; // Replace QOpenGLTexture with GLuint
  size_t blockTypeId{0};
  std::vector<glm::mat4> ModelMatrices; // Per-instance model (blocks)
  std::vector<float> faceIndices;
  std::vector<glm::vec2> quadSizes;
};

class UGeometryEngine : public IUGreedyTransparentBackend
{
public:
  UGeometryEngine(std::shared_ptr<UWorld> world,
                  std::shared_ptr<UTextureBaseStorage> texture_base_storage,
                  std::shared_ptr<UTextureCubeStorage> texture_cube_storage,
                  std::shared_ptr<UTextRenderer> text_renderer = nullptr);
  virtual ~UGeometryEngine();

  bool InitEngine();
  bool InitShaders();

  void Paint(int width_size, int height_size, double view_duration);

  // Methods for sky color management
  void SetSkyColor(float r, float g, float b, float a = 1.0f);
  void SetSkyColor(const glm::vec4 &color); // Replace QVector4D with glm::vec4
  glm::vec4 GetSkyColor() const;            // Replace QVector4D with glm::vec4
  void SetGradientSky(bool useGradient);
  bool IsGradientSky() const;
  void DrawSkyGradient();
  void WarmupGreedyGpuFromWorld();
  void ResetWorldRenderState();

  // Debug/Logging
  void SetVerboseLogging(bool enabled) { VerboseLogging = enabled; }

  // HUD toggles
  void SetShowHud(bool enabled) { ShowHud = enabled; }
  void SetShowCrosshair(bool enabled) { ShowCrosshair = enabled; }
  void SetShowPerformance(bool enabled) { ShowPerformance = enabled; }
  bool GetShowHud() const { return ShowHud; }
  bool GetShowCrosshair() const { return ShowCrosshair; }
  bool GetShowPerformance() const { return ShowPerformance; }
  void SetOverlayMargins(int left, int right, int top);

  void ShowTransientMessage(const std::string &msg, double seconds);

  /// Debug: GL texture id for block greedy/cross draw (0 if missing).
  GLuint InspectBlockGpuTexture(BlockId block_id,
                                bool *map_has_entry = nullptr) const;

  /// Unit cube wireframe (1x1 centered) with given MVP and color.
  void DrawBoxWireframe(const glm::mat4 &mvp, const glm::vec4 &color);

  void
  SetCreatureTextureStorage(std::shared_ptr<UCreatureTextureStorage> storage);
  void SetGameContent(const IUGameContent *content) { GameContent = content; }
  const IUGameContent *GetGameContent() const { return GameContent; }
  std::shared_ptr<UCreatureTextureStorage> GetCreatureTextureStorage() const
  {
    return CreatureTextureStorage;
  }
  void DrawCreatureTexturedPart(const glm::mat4 &mvp, GLuint texture,
                                CreaturePartMesh mesh = CreaturePartMesh::Box);
  void DrawCreatureBoneSkeletonMesh(const glm::mat4 &mvp, GLuint texture,
                                    const BoneSkeletonCubeMeshCpu &mesh);
  void DrawCreatureGltfMesh(const glm::mat4 &mvp, GLuint texture,
                            const BoneSkeletonCubeMeshCpu &mesh);
  void DrawCreatureSkinnedMesh(const glm::mat4 &mvp, GLuint texture,
                               const GltfPrimitiveCpu &mesh,
                               const std::vector<glm::mat4> &boneMatrices);
  void DrawCreatureBillboard(const glm::mat4 &mvp, GLuint texture,
                             const glm::vec4 &tint);

  void SetRenderSettings(const RenderSettings &settings);
  const RenderSettings &GetRenderSettings() const { return Render; }
  const std::shared_ptr<UWorld> &GetWorld() const { return WorldInstance; }
  void InvalidateBlockBatchCache() { BlockBatchesValid = false; }
  const CreatureRenderStats &GetCreatureRenderStats() const
  {
    return CreatureDraw_.GetStats();
  }
  CreatureDrawQueue &GetCreatureDrawQueue()
  {
    return CreatureDraw_.GetDrawQueue();
  }
  std::shared_ptr<UShaderManager> GetShaderManager() const
  {
    return shaderManager;
  }

  /// Updates sky tint and fluid fog state from the camera; call before glClear.
  void PrepareFrameRendering();

  /// Drop weather/sky quality when present stall (swap_wait) is high.
  PerformancePreset EffectiveWeatherPreset() const;

  void PrepareTransparent(const GreedyTransparentDrawContext &ctx) override;
  void DrawPreparedTransparent(GreedyShaderMode mode,
                               float shellAlpha) override;

private:
  // Static cube geometry (one VAO/VBO/EBO reused for all cubes)
  bool InitCubeBuffers();
  void DestroyCubeBuffers();
  bool InitFaceQuadBuffers();
  void DestroyFaceQuadBuffers();
  bool InitGreedyMeshBuffers();
  void DestroyGreedyMeshBuffers();
  void SetBlockAnimUniforms(const std::shared_ptr<UShaderProgram> &shader,
                            BlockId blockId,
                            const std::map<size_t, UTextureCube> &textures);
  void ApplyFogUniforms(const std::shared_ptr<UShaderProgram> &shader,
                        const glm::vec3 &cameraPos,
                        bool applyBelowSurfaceFog = true);
  void ApplyGreedyEnvironmentUniforms(
      const std::shared_ptr<UShaderProgram> &shader);
  void RenderFluidOverlay(int width, int height);
  void RenderWeatherOverlay(int width, int height);
  bool InitOverlayBuffers();
  void DestroyOverlayBuffers();
  GLuint cubeVAO = 0;
  GLuint cubeVBO = 0;
  GLuint cubeEBO = 0;
  GLuint faceVAO = 0;
  GLuint faceVBO = 0;
  GLuint faceEBO = 0;
  GLuint greedyMeshVAO = 0;
  GLuint greedyMeshVBO = 0;
  GLuint greedyMeshEBO = 0;
  GLuint instanceVBO = 0;      // instance buffer for per-instance MVP (cubes)
  GLuint instanceBlockVBO = 0; // interleaved model + faceIndex (blocks)
  GLuint cubeDrawVAO = 0;      // VAO used for DrawCube path (UCubeGL VBO/EBO)
  GLuint previewVAO = 0, previewVBO = 0, previewEBO = 0; // Preview cube buffers
  GLuint previewTexture = 0;                             // Preview texture
  GLuint outlineVAO = 0, outlineVBO = 0, outlineEBO = 0;
  bool EnsureCubeDrawVAO();
  bool InitOutlineBuffers();
  void DestroyOutlineBuffers();
  void RenderSelectionOutline();
  void RenderBlockCrackOverlay();
  void RenderBiomeDebugOverlay();

  void DrawCubeGeometry();
  void DrawCube(std::shared_ptr<UCube> icube,
                GLuint texture); // Replace QOpenGLTexture with GLuint
  void DrawSkyGradientSimple();  // Simple version without VBO

  // New optimized methods
  void PrepareRenderBatchesFromBlocks(
      const std::vector<BlockInstance> &instances,
      const std::map<size_t, UTextureCube> &textures);
  void RenderBatches(
      const glm::mat4 &mvp_matrix); // Replace QMatrix4x4 with glm::mat4
  void
  DrawBatch(const RenderBatch &batch,
            const glm::mat4 &mvp_matrix); // Replace QMatrix4x4 with glm::mat4
  // Methods for text rendering
  void RenderPerformanceText(int width_size, int height_size,
                             double view_duration);

  // Method for crosshair rendering
  void RenderCrosshair(int width_size, int height_size);

  // Method for simple 2D text rendering
  void RenderSimpleText(int width_size, int height_size);

private:
  // OpenGL uniform locations and values
  GLint alphaUniformLocation;
  GLfloat alpha;

  // UI helpers
  void RenderActiveObjectPreview(int width_size, int height_size);
  void RenderHotbarLabels(int width_size, int height_size);
  void InitPreviewBuffers();
  void DestroyPreviewBuffers();

  std::shared_ptr<UShaderManager> shaderManager; // Replace QOpenGLShaderProgram
  std::shared_ptr<UShaderProgram> defaultShader;
  std::shared_ptr<UShaderProgram> skyShader;  // Shader for sky
  std::shared_ptr<UShaderProgram> uiShader;   // Shader for UI elements
  std::shared_ptr<UShaderProgram> TextShader; // Shader for text
  std::shared_ptr<UShaderProgram>
      instancedShader; // Instanced cubes (legacy blocks)
  std::shared_ptr<UShaderProgram>
      instancedFaceShader; // Instanced greedy face quads
  std::shared_ptr<UShaderProgram>
      greedyShader; // Greedy world mesh (UV in fragment shader)
  std::shared_ptr<UShaderProgram>
      crossInstancedShader; // Instanced cross vegetation sprites
  std::shared_ptr<UShaderProgram> overlayShader;
  std::shared_ptr<UShaderProgram>
      outlineShader; // Shader for block selection outline
  GLuint overlayVAO{0};
  GLuint overlayVBO{0};

  std::shared_ptr<UTextureBaseStorage> TextureBaseStorageInstance;
  std::shared_ptr<UTextureCubeStorage> TextureCubeStorageInstance;
  std::shared_ptr<UCreatureTextureStorage> CreatureTextureStorage;
  std::shared_ptr<UWorld> WorldInstance;
  std::unique_ptr<IUWorldRenderReadModel> WorldRenderReadModel;
  const IUGameContent *GameContent{nullptr};
  std::shared_ptr<UTextRenderer> textRenderer;

  // performance data
  double DurationDrawSceneMks;
  double DurationWeatherStreakMks{0.0};
  double DurationWeatherParticleMks{0.0};
  double DurationSkyGradientMks{0.0};

  // Sky color
  glm::vec4 skyColor; // Replace QVector4D with glm::vec4
  glm::vec3 BaseSkyColor{0.5f, 0.7f, 1.0f};
  std::unique_ptr<IUFluidSurfaceProvider> FluidSurfaceProvider;
  UFluidSurfaceMap &FluidMap()
  {
    if (!FluidSurfaceProvider)
    {
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
      FluidSurfaceProvider = std::make_unique<UCpuFluidSurfaceMap>();
#else
      FluidSurfaceProvider = std::make_unique<UGpuFluidSurfaceMap>();
#endif
    }
    return FluidSurfaceProvider->Map();
  }
  UUnderwaterFogPass UnderwaterFogPass_;
  USkyGradientPass SkyGradientPass_;
  UOpaqueDepthCapture OpaqueDepthCapture;
  UWeatherRenderPass WeatherPass;
  glm::vec3 OverlayTintColor{0.0f};
  float OverlayTintAlpha{0.0f};
  BlockId OverlayBlockId{BLOCK_AIR};
  bool useGradientSky; // Use gradient sky

  // Logging
  bool VerboseLogging = false;

  // HUD toggles
  bool ShowHud = true;
  bool ShowCrosshair = true;
  bool ShowPerformance = false;
  int OverlayMarginLeft{10};
  int OverlayMarginRight{10};
  int OverlayMarginTop{30};
  int OverlayMarginBottom{20};

  // Rendering optimization
  std::vector<RenderBatch> renderBatches;
  size_t CachedInstanceCount{0};
  uint64_t CachedMeshRevision{0};
  bool BlockBatchesValid{false};
  RenderSettings Render;
  UAnimationClock AnimationClock;

  UCreatureDrawPass CreatureDraw_;
  URenderBackendBundle RenderBackends;
  UCrossGpuBackend CrossGpuBackend;
  GreedyGpuPassCache GreedyGpuOpaque;
  GreedyGpuPassCache GreedyGpuCutout;
  GreedyGpuPassCache GreedyGpuTransparent;
  CrossGpuPassCache CrossGpuPass;
  glm::mat4 PreparedTransparentVp{};
  const std::map<size_t, UTextureCube> *PreparedTransparentTextures{nullptr};
  IUMeshGpuStore &MeshStore();
  void EnsureRenderBackendsBound();
  void DrawGreedyOpaqueBatches(
      const UChunkMeshCache &cache,
      const std::vector<GreedyBatchRef> &opaqueCutoutRefs, const glm::mat4 &vp,
      const glm::vec3 &cameraPos,
      const std::map<size_t, UTextureCube> &textures, uint64_t meshRevision,
      uint64_t cullRevision);
  void DrawCrossInstancedBatches(const std::vector<CrossInstanceBatch> &batches,
                                 const glm::mat4 &vp,
                                 const std::map<size_t, UTextureCube> &textures,
                                 uint64_t meshRevision, uint64_t cullRevision);
  void SetGreedyShaderMode(const std::shared_ptr<UShaderProgram> &shader,
                           bool alphaCutout, bool transparentPass,
                           GreedyShaderMode mode, float shellAlphaThreshold);
  void DrawGreedyGpuBatches(const GreedyGpuPassCache &cache,
                            const glm::mat4 &vp,
                            const std::map<size_t, UTextureCube> &textures,
                            bool alphaCutout, bool transparentPass,
                            GreedyShaderMode mode, float shellAlphaThreshold);

  std::string TransientMessage;
  double TransientMessageUntil{0.0};
};

} // namespace cutum

#endif // GEOMETRYENGINE_H

#ifndef GEOMETRYENGINE_H
#define GEOMETRYENGINE_H

#include "CreaturePartMeshData.h"

// GLEW will be included in .cpp file after GLFW initialization
// Forward declaration for OpenGL types
typedef unsigned int GLuint;
typedef float GLfloat;
typedef int GLint;

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Object.h"
#include "TextureBase.h"
#include "TextureCube.h"
#include "World.h"
#include "ChunkMeshCache.h"
#include "GreedyMeshVertex.h"
#include "RenderSettings.h"
#include "CubeGL.h"
#include "ShaderManager.h"
#include "TextRenderer.h"
#include "AnimationClock.h"
#include "render/GreedyShaderMode.h"
#include "render/IGreedyTransparentBackend.h"
#include <optional>
#include <unordered_map>
#include <vector>
#include <memory>

namespace cutum {

class UCore;
class UCreatureTextureStorage;

// Structure for batch rendering
struct RenderBatch {
    GLuint textureID; // Replace QOpenGLTexture with GLuint
    size_t blockTypeId{0};
    std::vector<glm::mat4> modelMatrices; // Per-instance model (blocks) or unused for objects
    std::vector<float> faceIndices;
    std::vector<glm::vec2> quadSizes;
    std::vector<std::shared_ptr<UObject>> objects;
    std::vector<size_t> cubeIndices;
};

class UGeometryEngine : public IGreedyTransparentBackend
{
public:
 UGeometryEngine(std::shared_ptr<UObjectStorage> object_storage, std::shared_ptr<UWorld> world, std::shared_ptr<UTextureBaseStorage> texture_base_storage, std::shared_ptr<UTextureCubeStorage> texture_cube_storage, std::shared_ptr<UTextRenderer> text_renderer = nullptr);
 virtual ~UGeometryEngine();

 bool InitEngine();
 bool InitShaders();

 void Paint(int width_size, int height_size, double view_duration);

 // Methods for sky color management
 void SetSkyColor(float r, float g, float b, float a = 1.0f);
 void SetSkyColor(const glm::vec4& color); // Replace QVector4D with glm::vec4
 glm::vec4 GetSkyColor() const; // Replace QVector4D with glm::vec4
 void SetGradientSky(bool useGradient);
 bool IsGradientSky() const;
 
 // Debug/Logging
 void SetVerboseLogging(bool enabled) { VerboseLogging = enabled; }
 
 // HUD toggles
 void SetShowHud(bool enabled) { ShowHud = enabled; }
 void SetShowCrosshair(bool enabled) { ShowCrosshair = enabled; }
 void SetShowPerformance(bool enabled) { ShowPerformance = enabled; }
 bool GetShowHud() const { return ShowHud; }
 bool GetShowCrosshair() const { return ShowCrosshair; }
 bool GetShowPerformance() const { return ShowPerformance; }

 void ShowTransientMessage(const std::string& msg, double seconds);

 /// Unit cube wireframe (1x1 centered) with given MVP and color.
 void DrawBoxWireframe(const glm::mat4& mvp, const glm::vec4& color);

 void SetCreatureTextureStorage(std::shared_ptr<UCreatureTextureStorage> storage);
 std::shared_ptr<UCreatureTextureStorage> GetCreatureTextureStorage() const {
  return CreatureTextureStorageInstance_;
 }
 void DrawCreatureTexturedPart(const glm::mat4& mvp, GLuint texture,
                               CreaturePartMesh mesh = CreaturePartMesh::Box);

 void SetRenderSettings(const RenderSettings& settings);
 const RenderSettings& GetRenderSettings() const { return Render; }
 std::shared_ptr<UShaderManager> GetShaderManager() const { return shaderManager; }

 /// Updates sky tint and fluid fog state from the camera; call before glClear.
 void PrepareFrameRendering();

 void PrepareTransparent(const GreedyTransparentDrawContext& ctx) override;
 void DrawPreparedTransparent(GreedyShaderMode mode, float shellAlpha) override;

private:
 // Static cube geometry (one VAO/VBO/EBO reused for all cubes)
 bool InitCubeBuffers();
 void DestroyCubeBuffers();
 bool InitFaceQuadBuffers();
 void DestroyFaceQuadBuffers();
 bool InitGreedyMeshBuffers();
 void DestroyGreedyMeshBuffers();
 struct GreedyGpuPassCache;
 void SetBlockAnimUniforms(const std::shared_ptr<UShaderProgram>& shader, BlockId blockId,
                           const std::map<size_t, UTextureCube>& textures);
 void ApplyFluidFogUniforms(const std::shared_ptr<UShaderProgram>& shader,
                            const glm::vec3& cameraPos);
 void RenderFluidOverlay(int width, int height);
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
GLuint instanceVBO = 0; // instance buffer for per-instance MVP (cubes)
GLuint instanceBlockVBO = 0; // interleaved model + faceIndex (blocks)
GLuint cubeDrawVAO = 0; // VAO used for DrawCube path (UCubeGL VBO/EBO)
GLuint previewVAO = 0, previewVBO = 0, previewEBO = 0; // Preview cube buffers
GLuint previewTexture = 0; // Preview texture
GLuint outlineVAO = 0, outlineVBO = 0, outlineEBO = 0;
GLuint creaturePartVAO = 0;
GLuint creaturePartVBO = 0;
GLuint creaturePartEBO = 0;
GLuint creatureHeadPartVAO = 0;
GLuint creatureHeadPartVBO = 0;
GLuint creatureHeadPartEBO = 0;
GLuint creatureBodyPartVAO = 0;
GLuint creatureBodyPartVBO = 0;
GLuint creatureBodyPartEBO = 0;
 bool EnsureCubeDrawVAO();
 bool InitOutlineBuffers();
 void DestroyOutlineBuffers();
 bool InitCreaturePartBuffers();
 bool InitCreatureHeadPartBuffers();
 bool InitCreatureBodyPartBuffers();
 void DestroyCreaturePartBuffers();
 void RenderSelectionOutline();
 void RenderBlockCrackOverlay();
 void RenderCreatures();
 
 void DrawCubeGeometry();
 void DrawCube(std::shared_ptr<UCube> icube, GLuint texture); // Replace QOpenGLTexture with GLuint
 void DrawObject(std::shared_ptr<UObject> object, const std::map<size_t, UTextureCube>& textures);
 void DrawSkyGradient();
 void DrawSkyGradientSimple(); // Simple version without VBO

 // New optimized methods
 void PrepareRenderBatchesFromBlocks(const std::vector<BlockInstance>& instances,
                                     const std::map<size_t, UTextureCube>& textures);
 void RenderBatches(const glm::mat4& mvp_matrix); // Replace QMatrix4x4 with glm::mat4
 void DrawBatch(const RenderBatch& batch, const glm::mat4& mvp_matrix); // Replace QMatrix4x4 with glm::mat4
 // Methods for text rendering
 void RenderPerformanceText(int width_size, int height_size, double view_duration);
 
 // Method for crosshair rendering
 void RenderCrosshair(int width_size, int height_size);
 
   // Method for simple 2D text rendering
  void RenderSimpleText(int width_size, int height_size);
  
 private:
 //OpenGL uniform locations and values
 GLint alphaUniformLocation;
 GLfloat alpha;

    // UI helpers
    void RenderActiveObjectPreview(int width_size, int height_size);
    void RenderHotbarLabels(int width_size, int height_size);
    void InitPreviewBuffers();
    void DestroyPreviewBuffers();

 std::shared_ptr<UShaderManager> shaderManager; // Replace QOpenGLShaderProgram
 std::shared_ptr<UShaderProgram> defaultShader;
 std::shared_ptr<UShaderProgram> skyShader; // Shader for sky
std::shared_ptr<UShaderProgram> uiShader; // Shader for UI elements
std::shared_ptr<UShaderProgram> TextShader; // Shader for text
std::shared_ptr<UShaderProgram> instancedShader; // Instanced cubes (legacy blocks)
std::shared_ptr<UShaderProgram> instancedFaceShader; // Instanced greedy face quads
std::shared_ptr<UShaderProgram> greedyShader; // Greedy world mesh (UV in fragment shader)
std::shared_ptr<UShaderProgram> overlayShader;
std::shared_ptr<UShaderProgram> outlineShader; // Shader for block selection outline
GLuint overlayVAO{0};
GLuint overlayVBO{0};

 std::shared_ptr<UTextureBaseStorage> TextureBaseStorageInstance;
 std::shared_ptr<UTextureCubeStorage> TextureCubeStorageInstance;
 std::shared_ptr<UCreatureTextureStorage> CreatureTextureStorageInstance_;
 std::shared_ptr<UWorld> WorldInstance;
 std::shared_ptr<UObjectStorage> ObjectStorageInstance;
 std::shared_ptr<UTextRenderer> textRenderer;

 // performance data
 double DurationDrawSceneMks;
 
   // Sky color
  glm::vec4 skyColor; // Replace QVector4D with glm::vec4
  glm::vec3 baseSkyColor_{0.5f, 0.7f, 1.0f};
  glm::vec3 smoothedSkyTint_{0.5f, 0.7f, 1.0f};
  glm::vec3 smoothedFogColor_{0.05f, 0.15f, 0.35f};
  float fogStart_{0.0f};
  float fogEnd_{1000.0f};
  float fogMinBlend_{0.0f};
  float fogEnabled_{0.0f};
  glm::vec3 overlayTintColor_{0.0f};
  float overlayTintAlpha_{0.0f};
  BlockId overlayBlockId_{BLOCK_AIR};
  bool useGradientSky; // Use gradient sky
 
 // Logging
 bool VerboseLogging = false;
 
 // HUD toggles
 bool ShowHud = true;
 bool ShowCrosshair = true;
 bool ShowPerformance = true;
 
 // Rendering optimization
 std::vector<RenderBatch> renderBatches;
 size_t cachedInstanceCount_{0};
 uint64_t cachedMeshRevision_{0};
 bool blockBatchesValid_{false};
 RenderSettings Render;
 UAnimationClock animationClock_;

 struct GreedyGpuBatch {
  BlockId blockId{BLOCK_AIR};
  GLuint vbo{0};
  GLuint ebo{0};
  GLsizei indexCount{0};
 };
 struct GreedyGpuPassCache {
  std::vector<GreedyGpuBatch> batches;
  uint64_t meshRevision{0};
  uint64_t cullRevision{0};
  uint64_t sortRevision{0};
 };
 GreedyGpuPassCache greedyGpuOpaque_;
 GreedyGpuPassCache greedyGpuTransparent_;
 glm::mat4 preparedTransparentVp_{};
 const std::map<size_t, UTextureCube>* preparedTransparentTextures_{nullptr};
 void DrawGreedyOpaqueBatches(const std::vector<GreedyMeshBatch>& batches, const glm::mat4& vp,
                              const std::map<size_t, UTextureCube>& textures,
                              uint64_t meshRevision, uint64_t cullRevision);
 void SetGreedyShaderMode(const std::shared_ptr<UShaderProgram>& shader, bool transparentPass,
                          GreedyShaderMode mode, float shellAlphaThreshold);
 void DrawGreedyGpuBatches(const GreedyGpuPassCache& cache, const glm::mat4& vp,
                           const std::map<size_t, UTextureCube>& textures, bool transparentPass,
                           GreedyShaderMode mode, float shellAlphaThreshold);
 void RefreshGreedyGpuBatches(const std::vector<GreedyMeshBatch>& batches,
                              uint64_t meshRevision, uint64_t cullRevision,
                              GreedyGpuPassCache& cache, uint64_t sortRevision);
 void DestroyGreedyGpuPassCache(GreedyGpuPassCache& cache);
 void DestroyGreedyGpuBatches();

 std::string transientMessage_;
 double transientMessageUntil_{0.0};
};

}

#endif // GEOMETRYENGINE_H

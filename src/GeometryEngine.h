#ifndef GEOMETRYENGINE_H
#define GEOMETRYENGINE_H

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
#include "RenderSettings.h"
#include "CubeGL.h"
#include "ShaderManager.h"
#include "TextRenderer.h"
#include <optional>
#include <unordered_map>
#include <vector>
#include <memory>

namespace cutum {

class Core;

// Structure for batch rendering
struct RenderBatch {
    GLuint textureID; // Replace QOpenGLTexture with GLuint
    std::vector<glm::mat4> modelMatrices; // Per-instance model (blocks) or unused for objects
    std::vector<float> faceIndices;
    std::vector<glm::vec2> quadSizes;
    std::vector<std::shared_ptr<Object>> objects;
    std::vector<size_t> cubeIndices;
};

class GeometryEngine
{
public:
 GeometryEngine(std::shared_ptr<ObjectStorage> object_storage, std::shared_ptr<World> world, std::shared_ptr<TextureBaseStorage> texture_base_storage, std::shared_ptr<TextureCubeStorage> texture_cube_storage, std::shared_ptr<TextRenderer> text_renderer = nullptr);
 virtual ~GeometryEngine();

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
 void SetVerboseLogging(bool enabled) { verboseLogging = enabled; }
 
 // HUD toggles
 void SetShowHud(bool enabled) { showHud = enabled; }
 void SetShowCrosshair(bool enabled) { showCrosshair = enabled; }
 void SetShowPerformance(bool enabled) { showPerformance = enabled; }
 bool GetShowHud() const { return showHud; }
 bool GetShowCrosshair() const { return showCrosshair; }
 bool GetShowPerformance() const { return showPerformance; }

 void ShowTransientMessage(const std::string& msg, double seconds);

 void SetRenderSettings(const RenderSettings& settings);
 const RenderSettings& GetRenderSettings() const { return renderSettings_; }
 
private:
 // Static cube geometry (one VAO/VBO/EBO reused for all cubes)
 bool InitCubeBuffers();
 void DestroyCubeBuffers();
 bool InitFaceQuadBuffers();
 void DestroyFaceQuadBuffers();
GLuint cubeVAO = 0;
GLuint cubeVBO = 0;
GLuint cubeEBO = 0;
GLuint faceVAO = 0;
GLuint faceVBO = 0;
GLuint faceEBO = 0;
GLuint instanceVBO = 0; // instance buffer for per-instance MVP (cubes)
GLuint instanceBlockVBO = 0; // interleaved MVP + quadSize + faceIndex (blocks)
GLuint cubeDrawVAO = 0; // VAO used for DrawCube path (CubeGL VBO/EBO)
GLuint previewVAO = 0, previewVBO = 0, previewEBO = 0; // Preview cube buffers
GLuint previewTexture = 0; // Preview texture
GLuint outlineVAO = 0, outlineVBO = 0, outlineEBO = 0;
 bool EnsureCubeDrawVAO();
 bool InitOutlineBuffers();
 void DestroyOutlineBuffers();
 void RenderSelectionOutline();
 
 void DrawCubeGeometry();
 void DrawCube(std::shared_ptr<Cube> icube, GLuint texture); // Replace QOpenGLTexture with GLuint
 void DrawObject(std::shared_ptr<Object> object, const std::map<size_t, TextureCube>& textures);
 void DrawSkyGradient();
 void DrawSkyGradientSimple(); // Simple version without VBO

 // New optimized methods
 void PrepareRenderBatchesFromBlocks(const std::vector<BlockInstance>& instances,
                                     const std::map<size_t, TextureCube>& textures);
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
    void InitPreviewBuffers();
    void DestroyPreviewBuffers();

 std::shared_ptr<ShaderManager> shaderManager; // Replace QOpenGLShaderProgram
 std::shared_ptr<ShaderProgram> defaultShader;
 std::shared_ptr<ShaderProgram> skyShader; // Shader for sky
std::shared_ptr<ShaderProgram> uiShader; // Shader for UI elements
std::shared_ptr<ShaderProgram> textShader; // Shader for text
std::shared_ptr<ShaderProgram> instancedShader; // Instanced cubes (legacy blocks)
std::shared_ptr<ShaderProgram> instancedFaceShader; // Instanced greedy face quads
std::shared_ptr<ShaderProgram> outlineShader; // Shader for block selection outline

 std::shared_ptr<TextureBaseStorage> TextureBaseStorageInstance;
 std::shared_ptr<TextureCubeStorage> TextureCubeStorageInstance;
 std::shared_ptr<World> WorldInstance;
 std::shared_ptr<ObjectStorage> ObjectStorageInstance;
 std::shared_ptr<TextRenderer> textRenderer;

 // performance data
 double DurationDrawSceneMks;
 
   // Sky color
  glm::vec4 skyColor; // Replace QVector4D with glm::vec4
  bool useGradientSky; // Use gradient sky
 
 // Logging
 bool verboseLogging = false;
 
 // HUD toggles
 bool showHud = true;
 bool showCrosshair = true;
 bool showPerformance = true;
 
 // Rendering optimization
 std::vector<RenderBatch> renderBatches;
 size_t cachedInstanceCount_{0};
 uint64_t cachedMeshRevision_{0};
 bool blockBatchesValid_{false};
 RenderSettings renderSettings_;

 std::string transientMessage_;
 double transientMessageUntil_{0.0};
};

}

#endif // GEOMETRYENGINE_H

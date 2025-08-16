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
    std::vector<glm::mat4> modelMatrices; // Replace QMatrix4x4 with glm::mat4
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

private:
 void DrawCubeGeometry();
 void DrawCube(std::shared_ptr<Cube> icube, GLuint texture); // Replace QOpenGLTexture with GLuint
 void DrawObject(std::shared_ptr<Object> object, size_t object_index, const std::map<size_t, TextureCube>& textures, bool is_intersection_exists, size_t intersecion_object_index, size_t intersecion_cube_index);
 void DrawCubeGeometry(const std::vector<std::shared_ptr<Object>>& objects, const glm::mat4& mvp_matrix, bool is_intersection_exists, size_t intersecion_object_index, size_t intersecion_cube_index); // Replace QMatrix4x4 with glm::mat4
 void DrawSkyGradient();
 void DrawSkyGradientSimple(); // Simple version without VBO

 // New optimized methods
 void PrepareRenderBatches(const std::vector<std::shared_ptr<Object>>& objects, 
                          const std::map<size_t, TextureCube>& textures,
                          bool is_intersection_exists, 
                          size_t intersecion_object_index, 
                          size_t intersecion_cube_index);
 void RenderBatches(const glm::mat4& mvp_matrix); // Replace QMatrix4x4 with glm::mat4
 void DrawBatch(const RenderBatch& batch, const glm::mat4& mvp_matrix); // Replace QMatrix4x4 with glm::mat4
 void UpdateFrustumCulling(const glm::mat4& view_projection); // Replace QMatrix4x4 with glm::mat4
 bool IsObjectInFrustum(const std::shared_ptr<Object>& object);
 
 // Methods for text rendering
 void RenderPerformanceText(int width_size, int height_size, double view_duration);
 
 // Method for crosshair rendering
 void RenderCrosshair(int width_size, int height_size);
 
   // Method for simple 2D text rendering
  void RenderSimpleText(int width_size, int height_size);
  
       // Method for test cube rendering
  void RenderTestCube();
  
    // Method for 3D cube rendering with perspective projection
   void Render3DCubeWithPerspective();

private:
 //OpenGL uniform locations and values
 GLint alphaUniformLocation;
 GLfloat alpha;

 std::shared_ptr<ShaderManager> shaderManager; // Replace QOpenGLShaderProgram
 std::shared_ptr<ShaderProgram> defaultShader;
 std::shared_ptr<ShaderProgram> skyShader; // Shader for sky
std::shared_ptr<ShaderProgram> uiShader; // Shader for UI elements
std::shared_ptr<ShaderProgram> textShader; // Shader for text

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
 
 // Rendering optimization
 std::vector<RenderBatch> renderBatches;
 std::vector<std::shared_ptr<Object>> visibleObjects;
 
 // Frustum culling
 struct FrustumPlane {
     glm::vec4 normal; // QVector4D -> glm::vec4
     float distance;
 };
 std::array<FrustumPlane, 6> frustumPlanes;
};

}

#endif // GEOMETRYENGINE_H

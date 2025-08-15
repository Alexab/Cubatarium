#ifndef GEOMETRYENGINE_H
#define GEOMETRYENGINE_H

// GLEW будет включен в .cpp файле после инициализации GLFW
// Forward declaration для OpenGL типов
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

// Структура для batch-рендеринга
struct RenderBatch {
    GLuint textureID; // Заменяем QOpenGLTexture на GLuint
    std::vector<glm::mat4> modelMatrices; // QMatrix4x4 -> glm::mat4
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

 // Методы для управления цветом неба
 void SetSkyColor(float r, float g, float b, float a = 1.0f);
 void SetSkyColor(const glm::vec4& color); // QVector4D -> glm::vec4
 glm::vec4 GetSkyColor() const; // QVector4D -> glm::vec4
 void SetGradientSky(bool useGradient);
 bool IsGradientSky() const;

private:
 void DrawCubeGeometry();
 void DrawCube(std::shared_ptr<Cube> icube, GLuint texture); // QOpenGLTexture -> GLuint
 void DrawObject(std::shared_ptr<Object> object, size_t object_index, const std::map<size_t, TextureCube>& textures, bool is_intersection_exists, size_t intersecion_object_index, size_t intersecion_cube_index);
 void DrawCubeGeometry(const std::vector<std::shared_ptr<Object>>& objects, const glm::mat4& mvp_matrix, bool is_intersection_exists, size_t intersecion_object_index, size_t intersecion_cube_index); // QMatrix4x4 -> glm::mat4
 void DrawSkyGradient();
 void DrawSkyGradientSimple(); // Простая версия без VBO

 // Новые оптимизированные методы
 void PrepareRenderBatches(const std::vector<std::shared_ptr<Object>>& objects, 
                          const std::map<size_t, TextureCube>& textures,
                          bool is_intersection_exists, 
                          size_t intersecion_object_index, 
                          size_t intersecion_cube_index);
 void RenderBatches(const glm::mat4& mvp_matrix); // QMatrix4x4 -> glm::mat4
 void DrawBatch(const RenderBatch& batch, const glm::mat4& mvp_matrix); // QMatrix4x4 -> glm::mat4
 void UpdateFrustumCulling(const glm::mat4& view_projection); // QMatrix4x4 -> glm::mat4
 bool IsObjectInFrustum(const std::shared_ptr<Object>& object);
 
 // Методы для отображения текста
 void RenderPerformanceText(int width_size, int height_size, double view_duration);

private:
 //OpenGL uniform locations and values
 GLint alphaUniformLocation;
 GLfloat alpha;

 std::shared_ptr<ShaderManager> shaderManager; // Заменяем QOpenGLShaderProgram
 std::shared_ptr<ShaderProgram> defaultShader;
 std::shared_ptr<ShaderProgram> skyShader; // Шейдер для неба

 std::shared_ptr<TextureBaseStorage> TextureBaseStorageInstance;
 std::shared_ptr<TextureCubeStorage> TextureCubeStorageInstance;
 std::shared_ptr<World> WorldInstance;
 std::shared_ptr<ObjectStorage> ObjectStorageInstance;
 std::shared_ptr<TextRenderer> textRenderer;

 // performance data
 double DurationDrawSceneMks;
 
   // Цвет неба
  glm::vec4 skyColor; // QVector4D -> glm::vec4
  bool useGradientSky; // Использовать градиентное небо
 
 // Оптимизация рендеринга
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

#ifndef GEOMETRYENGINE_GLM_H
#define GEOMETRYENGINE_GLM_H

// GLEW будет включен в .cpp файле после инициализации GLFW
// Forward declaration для OpenGL типов
typedef unsigned int GLuint;

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Object.h"
#include "TextureBase.h"
#include "TextureCube.h"
#include "World.h"
#include "CubeGL.h"
#include "ShaderManager.h"
#include <optional>
#include <unordered_map>
#include <vector>
#include <memory>

namespace cutum {

class Core;

// Структура для batch-рендеринга (обновленная для нативных OpenGL)
struct RenderBatch {
    GLuint textureID; // Заменяем QOpenGLTexture на GLuint
    std::vector<glm::mat4> modelMatrices; // Заменяем QMatrix4x4 на glm::mat4
    std::vector<std::shared_ptr<Object>> objects;
    std::vector<size_t> cubeIndices;
};

class GeometryEngine
{
public:
    GeometryEngine(std::shared_ptr<ObjectStorage> object_storage, 
                   std::shared_ptr<World> world, 
                   std::shared_ptr<TextureBaseStorage> texture_base_storage, 
                   std::shared_ptr<TextureCubeStorage> texture_cube_storage);
    virtual ~GeometryEngine();

    bool InitEngine();
    bool InitShaders();

    // Удаляем Qt Painter
    // void SetPainter(std::shared_ptr<QPainter> painter);

    void Paint(int width_size, int height_size, double view_duration);

    // Методы для управления цветом неба (обновленные для GLM)
    void SetSkyColor(float r, float g, float b, float a = 1.0f);
    void SetSkyColor(const glm::vec4& color); // Заменяем QVector4D на glm::vec4
    glm::vec4 GetSkyColor() const; // Заменяем QVector4D на glm::vec4
    void SetGradientSky(bool useGradient);
    bool IsGradientSky() const;

private:
    void DrawCubeGeometry();
    void DrawCube(std::shared_ptr<Cube> icube, GLuint textureID); // Заменяем QOpenGLTexture на GLuint
    void DrawObject(std::shared_ptr<Object> object, size_t object_index, 
                   const std::map<size_t, TextureCube>& textures, 
                   bool is_intersection_exists, size_t intersecion_object_index, 
                   size_t intersecion_cube_index);
    void DrawCubeGeometry(const std::vector<std::shared_ptr<Object>>& objects, 
                         const glm::mat4& mvp_matrix, // Заменяем QMatrix4x4 на glm::mat4
                         bool is_intersection_exists, size_t intersecion_object_index, 
                         size_t intersecion_cube_index);
    void DrawSkyGradient();
    void DrawSkyGradientSimple();

    // Новые оптимизированные методы
    void PrepareRenderBatches(const std::vector<std::shared_ptr<Object>>& objects, 
                             const std::map<size_t, TextureCube>& textures,
                             bool is_intersection_exists, 
                             size_t intersecion_object_index, 
                             size_t intersecion_cube_index);
    void RenderBatches(const glm::mat4& mvp_matrix); // Заменяем QMatrix4x4 на glm::mat4
    void DrawBatch(const RenderBatch& batch, const glm::mat4& mvp_matrix);
    void UpdateFrustumCulling(const glm::mat4& view_projection); // Заменяем QMatrix4x4 на glm::mat4
    bool IsObjectInFrustum(const std::shared_ptr<Object>& object);

private:
    // OpenGL uniform locations and values
    GLint alphaUniformLocation;
    GLfloat alpha = 0.5f;

    // Заменяем Qt шейдеры на наш ShaderManager
    std::shared_ptr<ShaderManager> shaderManager;
    std::shared_ptr<ShaderProgram> defaultShader;
    std::shared_ptr<ShaderProgram> skyShader;

    // Удаляем Qt Painter
    // std::shared_ptr<QPainter> Painter;

    std::shared_ptr<TextureBaseStorage> TextureBaseStorageInstance;
    std::shared_ptr<TextureCubeStorage> TextureCubeStorageInstance;
    std::shared_ptr<World> WorldInstance;
    std::shared_ptr<ObjectStorage> ObjectStorageInstance;

    // performance data
    double DurationDrawSceneMks;
    
    // Цвет неба (заменяем QVector4D на glm::vec4)
    glm::vec4 skyColor;
    bool useGradientSky;
    
    // Оптимизация рендеринга
    std::vector<RenderBatch> renderBatches;
    std::vector<std::shared_ptr<Object>> visibleObjects;
    
    // Frustum culling (заменяем QVector4D на glm::vec4)
    struct FrustumPlane {
        glm::vec4 normal; // Заменяем QVector4D на glm::vec4
        float distance;
    };
    std::array<FrustumPlane, 6> frustumPlanes;
};

} // namespace cutum

#endif // GEOMETRYENGINE_GLM_H


#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <chrono>
#include <GL/glew.h>
#include "GeometryEngine.h"
#include "Core.h"
#include "ObjectImplementation.h"
#include "User.h"
#include "ObjectStorage.h"
#include "Camera.h"
#include "ShaderManager.h"

namespace cutum {

GeometryEngine::GeometryEngine(std::shared_ptr<ObjectStorage> object_storage, std::shared_ptr<World> world, std::shared_ptr<TextureBaseStorage> texture_base_storage, std::shared_ptr<TextureCubeStorage> texture_cube_storage)
 : ObjectStorageInstance(object_storage)
 , WorldInstance(world)
 , TextureBaseStorageInstance(texture_base_storage)
 , TextureCubeStorageInstance(texture_cube_storage)
 , skyColor(0.5f, 0.7f, 1.0f, 1.0f) // Инициализируем цвет неба (голубой)
 , useGradientSky(false) // По умолчанию используем простой цвет
{
}

GeometryEngine::~GeometryEngine()
{
}

bool GeometryEngine::InitEngine()
{
 // Инициализируем ShaderManager
 shaderManager = std::make_shared<ShaderManager>();
 if (!shaderManager->Initialize()) {
     std::cerr << "Failed to initialize ShaderManager" << std::endl;
     return false;
 }
 
 if(!InitShaders())
  return false;

 return true;
}

bool GeometryEngine::InitShaders()
{
 // Создаем шейдеры через ShaderManager
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

 return true;
}

void GeometryEngine::Paint(int width_size, int height_size, double view_duration)
{
 DrawCubeGeometry();
 
 // Отрисовка UI текста (заменяем QPainter на OpenGL)
 // TODO: Добавить систему отрисовки текста через OpenGL
 std::cout << "Cubatarium version - Scene t=" << DurationDrawSceneMks/1000.0 << " ms" << std::endl;
 std::cout << "DoMovement t=" << WorldInstance->GetDurationDoMovementMks()/1000.0 << " ms" << std::endl;
 std::cout << "ViewUpdate t=" << view_duration/1000.0 << " ms" << std::endl;
}

void GeometryEngine::DrawCubeGeometry()
{
 auto t_begin = std::chrono::high_resolution_clock::now();
 DrawCubeGeometry(WorldInstance->GetObjects(), WorldInstance->GetCurrentUserCamera()->GetMvpMatrix(),
                              WorldInstance->GetIsIntersectionExists(),
                              WorldInstance->GetIntersectionObjectIndex(),
                              WorldInstance->GetIntersectionCubeIndex());
 auto t_end = std::chrono::high_resolution_clock::now();
 DurationDrawSceneMks = std::chrono::duration<double, std::micro>(t_end-t_begin).count();
}

void GeometryEngine::PrepareRenderBatches(const std::vector<std::shared_ptr<Object>>& objects, 
                                         const std::map<size_t, TextureCube>& textures,
                                         bool is_intersection_exists, 
                                         size_t intersecion_object_index, 
                                         size_t intersecion_cube_index)
{
 renderBatches.clear();
 std::unordered_map<size_t, RenderBatch> batchMap;

 for(size_t j = 0; j < objects.size(); j++)
 {
  auto& object = objects[j];
  
  for(size_t i = 0; i < object->GetCubes().size(); i++)
  {
   auto& cube = object->GetCubes()[i];
   size_t textureId;
   
   if(is_intersection_exists && intersecion_object_index == j && intersecion_cube_index == i)
   {
    textureId = TextureCubeStorageInstance->GetTypeIdByName("selection");
   }
   else
   {
    textureId = cube->GetTypeId();
   }
   
       auto& batch = batchMap[textureId];
    if (batch.textureID == 0) {
        batch.textureID = textures.at(textureId).GetTextureId(); // Используем GLuint вместо QOpenGLTexture
    }
    
         // Использовать единичную матрицу, так как кубы уже имеют правильные локальные координаты
     glm::mat4 modelMatrix = glm::mat4(1.0f);
     batch.modelMatrices.push_back(modelMatrix);
     batch.objects.push_back(object);
     batch.cubeIndices.push_back(i);
  }
 }

 // Преобразовать map в vector
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
 if (batch.modelMatrices.empty()) return;
 
 glBindTexture(GL_TEXTURE_2D, batch.textureID);
 defaultShader->Use();
 defaultShader->SetInt("texture0", 0);
 
 // Здесь можно добавить instanced rendering для еще большей оптимизации
 // Пока используем простой batch-рендеринг
 for (size_t i = 0; i < batch.modelMatrices.size(); ++i) {
     // Использовать MVP матрицу напрямую, так как кубы уже имеют правильные координаты
     defaultShader->SetMat4("mvp_matrix", mvp_matrix);
     
     // Найти куб для отрисовки
     auto& object = batch.objects[i];
     auto& cube = object->GetCubes()[batch.cubeIndices[i]];
     
     DrawCube(cube, batch.textureID);
 }
 
 defaultShader->Unuse();
}

void GeometryEngine::UpdateFrustumCulling(const glm::mat4& view_projection)
{
 // Извлечение плоскостей frustum из матрицы view-projection
 glm::mat4 m = glm::transpose(view_projection);
 
 // Левая плоскость
 frustumPlanes[0].normal = glm::vec4(m[3][0] + m[0][0], m[3][1] + m[0][1], m[3][2] + m[0][2], m[3][3] + m[0][3]);
 frustumPlanes[0].distance = frustumPlanes[0].normal.w;
 frustumPlanes[0].normal.w = 0.0f;
 frustumPlanes[0].normal = glm::normalize(frustumPlanes[0].normal);
 
 // Правая плоскость
 frustumPlanes[1].normal = glm::vec4(m[3][0] - m[0][0], m[3][1] - m[0][1], m[3][2] - m[0][2], m[3][3] - m[0][3]);
 frustumPlanes[1].distance = frustumPlanes[1].normal.w;
 frustumPlanes[1].normal.w = 0.0f;
 frustumPlanes[1].normal = glm::normalize(frustumPlanes[1].normal);
 
 // Нижняя плоскость
 frustumPlanes[2].normal = glm::vec4(m[3][0] + m[1][0], m[3][1] + m[1][1], m[3][2] + m[1][2], m[3][3] + m[1][3]);
 frustumPlanes[2].distance = frustumPlanes[2].normal.w;
 frustumPlanes[2].normal.w = 0.0f;
 frustumPlanes[2].normal = glm::normalize(frustumPlanes[2].normal);
 
 // Верхняя плоскость
 frustumPlanes[3].normal = glm::vec4(m[3][0] - m[1][0], m[3][1] - m[1][1], m[3][2] - m[1][2], m[3][3] - m[1][3]);
 frustumPlanes[3].distance = frustumPlanes[3].normal.w;
 frustumPlanes[3].normal.w = 0.0f;
 frustumPlanes[3].normal = glm::normalize(frustumPlanes[3].normal);
 
 // Ближняя плоскость
 frustumPlanes[4].normal = glm::vec4(m[3][0] + m[2][0], m[3][1] + m[2][1], m[3][2] + m[2][2], m[3][3] + m[2][3]);
 frustumPlanes[4].distance = frustumPlanes[4].normal.w;
 frustumPlanes[4].normal.w = 0.0f;
 frustumPlanes[4].normal = glm::normalize(frustumPlanes[4].normal);
 
 // Дальняя плоскость
 frustumPlanes[5].normal = glm::vec4(m[3][0] - m[2][0], m[3][1] - m[2][1], m[3][2] - m[2][2], m[3][3] - m[2][3]);
 frustumPlanes[5].distance = frustumPlanes[5].normal.w;
 frustumPlanes[5].normal.w = 0.0f;
 frustumPlanes[5].normal = glm::normalize(frustumPlanes[5].normal);
}

bool GeometryEngine::IsObjectInFrustum(const std::shared_ptr<Object>& object)
{
 glm::vec3 objectPos(object->GetPose()[0][3], object->GetPose()[1][3], object->GetPose()[2][3]);
 float radius = 1.0f; // Примерный радиус объекта
 
 for (const auto& plane : frustumPlanes) {
     float distance = glm::dot(glm::vec3(plane.normal), objectPos) + plane.distance;
     if (distance < -radius) {
         return false;
     }
 }
 return true;
}

void GeometryEngine::DrawCube(std::shared_ptr<Cube> icube, GLuint texture)
{
 auto cube = std::dynamic_pointer_cast<CubeGL>(icube);
 if(!cube)
  return;

 glBindTexture(GL_TEXTURE_2D, texture);
 defaultShader->Use();
 defaultShader->SetInt("texture0", 0);


 // Tell OpenGL which VBOs to use
 glBindBuffer(GL_ARRAY_BUFFER, cube->GetArrayBuf());
 glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cube->GetIndexBuf());

 // Offset for position
 size_t offset = 0;

 // Tell OpenGL programmable pipeline how to locate vertex position data
 int vertexLocation = glGetAttribLocation(defaultShader->GetProgramID(), "a_position");
 glEnableVertexAttribArray(vertexLocation);
 glVertexAttribPointer(vertexLocation, 3, GL_FLOAT, GL_FALSE, sizeof(VertexData), (void*)offset);

 // Offset for texture coordinate
 offset += sizeof(glm::vec3);

 // Tell OpenGL programmable pipeline how to locate vertex texture coordinate data
 int texcoordLocation = glGetAttribLocation(defaultShader->GetProgramID(), "a_texcoord");
 glEnableVertexAttribArray(texcoordLocation);
 glVertexAttribPointer(texcoordLocation, 2, GL_FLOAT, GL_FALSE, sizeof(VertexData), (void*)offset);

 // Draw cube geometry using indices from VBO 1
 glDrawElements(GL_TRIANGLE_STRIP, int(std::dynamic_pointer_cast<CubeGL>(cube)->GetIndices().size()), GL_UNSIGNED_SHORT, nullptr);
 
 glBindBuffer(GL_ARRAY_BUFFER, 0);
 glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

 defaultShader->Unuse();
}

void GeometryEngine::DrawObject(std::shared_ptr<Object> object, size_t object_index, const std::map<size_t, TextureCube>& textures, bool is_intersection_exists, size_t intersecion_object_index, size_t intersecion_cube_index)
{
 for(size_t i=0; i<object->GetCubes().size(); i++)
 {
  auto & cube = object->GetCubes()[i];
  GLuint texture;
  if(is_intersection_exists && intersecion_object_index == object_index && intersecion_cube_index == i)
  {
       texture = textures.at(TextureCubeStorageInstance->GetTypeIdByName("selection")).GetTextureId();
  }
  else
  {
       texture = textures.at(cube->GetTypeId()).GetTextureId();
  }
  DrawCube(cube, texture);
 }

}

void GeometryEngine::DrawCubeGeometry(const std::vector<std::shared_ptr<Object>>& objects, const glm::mat4& mvp_matrix, bool is_intersection_exists, size_t intersecion_object_index, size_t intersecion_cube_index)
{
 // Enable depth buffer
 glEnable(GL_DEPTH_TEST);

 // Enable back face culling
 glEnable(GL_CULL_FACE);

 // Очищаем буферы
 if(useGradientSky)
 {
  // Для градиентного неба очищаем только буфер глубины
  glClear(GL_DEPTH_BUFFER_BIT);
 }
 else
 {
  // Для простого неба устанавливаем цвет и очищаем оба буфера
     glClearColor(skyColor.x, skyColor.y, skyColor.z, skyColor.w);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
 }

 // Отрисовываем градиентное небо ПОСЛЕ очистки буфера глубины
 if(useGradientSky)
 {
     std::cout << "Drawing gradient sky with color: (" << skyColor.x << ", " << skyColor.y << ", " << skyColor.z << ", " << skyColor.w << ")" << std::endl;
  DrawSkyGradient();
  std::cout << "Gradient sky drawn." << std::endl;
 }

 // Bind shader pipeline for use
 defaultShader->Use();

 // Set modelview-projection matrix
 defaultShader->SetMat4("mvp_matrix", mvp_matrix);

 //glUniform1f(alphaUniformLocation, alpha);

 //Get locations of fragment shader uniforms to be tied to UI
 //alphaUniformLocation = glGetUniformLocation(program.programId(), "alpha");

  auto textures = TextureCubeStorageInstance->GetTextures();
 
 std::cout << "Objects to render:" << objects.size() << std::endl;
  
 // Оптимизированный рендеринг с batch-обработкой
 PrepareRenderBatches(objects, textures, is_intersection_exists, intersecion_object_index, intersecion_cube_index);
 std::cout << "Render batches prepared:" << renderBatches.size() << std::endl;
 RenderBatches(mvp_matrix);

 auto user = WorldInstance->GetCurrentUser();
 if(user)
 {
  auto active_object_type_name = user->GetActiveObjectTypeName();
  auto active_object = ObjectStorageInstance->TakeObject(active_object_type_name);//GetPrototype(active_object_type_name).GetSample();
  if(active_object)
  {
   // Set modelview-projection matrix for user plane
   glm::mat4 pose = glm::mat4(1.0f);
   active_object->GetCubes()[0]->Init(pose, 0.2f);

   pose[0][3] = 0.8f;
   pose[1][3] = 0.8f;
   pose[2][3] = 0.0f;
   active_object->SetPose(pose);

   glm::mat4 position = glm::mat4(1.0f);
   defaultShader->SetMat4("mvp_matrix", position);

   DrawObject(active_object, 0, textures, false, 0, 0);

 //  std::shared_ptr<QOpenGLTexture> texture = textures[active_cube->GetTypeId()].GetTexture();
 //  DrawCube(active_cube, texture);
  }
 }
 defaultShader->Unuse();

 glDisable(GL_CULL_FACE);
 glDisable(GL_DEPTH_TEST);
}

void GeometryEngine::DrawSkyGradient()
{
 // Используем простую версию, которая более надежна
 DrawSkyGradientSimple();
}

void GeometryEngine::DrawSkyGradientSimple()
{
 // Проверяем, что шейдер неба готов
 if (!skyShader->IsValid()) {
     std::cerr << "Sky shader is not linked!" << std::endl;
     return;
 }
 
 // Временно отключаем тест глубины для неба
 glDisable(GL_DEPTH_TEST);
 
 // Используем шейдер неба
 skyShader->Use();
 
 // Устанавливаем единичную матрицу для неба
 glm::mat4 skyMatrix = glm::mat4(1.0f);
 skyShader->SetMat4("mvp_matrix", skyMatrix);
 
 // Передаем цвет неба в шейдер
 skyShader->SetVec4("skyColor", skyColor);
 
 // Создаем простой прямоугольник для неба (полный экран)
 static const GLfloat skyVertices[] = {
     // Позиции        // Текстурные координаты
     -1.0f, -1.0f, 0.0f,  0.0f, 0.0f,
      1.0f, -1.0f, 0.0f,  1.0f, 0.0f,
      1.0f,  1.0f, 0.0f,  1.0f, 1.0f,
     -1.0f,  1.0f, 0.0f,  0.0f, 1.0f
 };
 
 // Создаем временный VBO для отрисовки
 GLuint tempVBO;
 glGenBuffers(1, &tempVBO);
 glBindBuffer(GL_ARRAY_BUFFER, tempVBO);
 glBufferData(GL_ARRAY_BUFFER, sizeof(skyVertices), skyVertices, GL_STATIC_DRAW);
 
 // Устанавливаем атрибуты
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
 
 // Отрисовываем небо как треугольники
 glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
 
 // Проверяем ошибки OpenGL
 GLenum error = glGetError();
 if (error != GL_NO_ERROR) {
     std::cerr << "OpenGL error after drawing sky: " << error << std::endl;
 }
 
 // Освобождаем ресурсы
 glBindBuffer(GL_ARRAY_BUFFER, 0);
 glDeleteBuffers(1, &tempVBO);
 skyShader->Unuse();
 
 // Включаем тест глубины обратно
 glEnable(GL_DEPTH_TEST);
}

// Методы для управления цветом неба
void GeometryEngine::SetSkyColor(float r, float g, float b, float a)
{
 skyColor = glm::vec4(r, g, b, a);
}

void GeometryEngine::SetSkyColor(const glm::vec4& color)
{
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

}


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

GeometryEngine::GeometryEngine(std::shared_ptr<ObjectStorage> object_storage, std::shared_ptr<World> world, std::shared_ptr<TextureBaseStorage> texture_base_storage, std::shared_ptr<TextureCubeStorage> texture_cube_storage, std::shared_ptr<TextRenderer> text_renderer)
 : ObjectStorageInstance(object_storage)
 , WorldInstance(world)
 , TextureBaseStorageInstance(texture_base_storage)
 , TextureCubeStorageInstance(texture_cube_storage)
 , textRenderer(text_renderer)
 , skyColor(0.5f, 0.7f, 1.0f, 1.0f) // Initialize sky color (blue)
, useGradientSky(false) // Use simple color by default
{
}

GeometryEngine::~GeometryEngine()
{
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

 return true;
}

void GeometryEngine::Paint(int width_size, int height_size, double view_duration)
{
     // Render only scene objects
 DrawCubeGeometry();
 
     // Render crosshair
 RenderCrosshair(width_size, height_size);
 
     // Render simple text
 RenderSimpleText(width_size, height_size);
 
     // Disable performance UI text rendering
     RenderPerformanceText(width_size, height_size, view_duration);
}

void GeometryEngine::DrawCubeGeometry()
{
 auto t_begin = std::chrono::high_resolution_clock::now();
 
 const auto& objects = WorldInstance->GetObjects();
 
 if (objects.empty()) {
     return;
 }
 
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
 
 // Use loaded shader from ShaderManager
 if (!defaultShader || !defaultShader->IsValid()) {
     std::cerr << "Default shader is not valid" << std::endl;
     return;
 }
 
 // Get MVP matrix
 glm::mat4 mvp = camera->GetMvpMatrix();
 
 // Get textures
 auto textures = TextureCubeStorageInstance->GetTextures();
 
 // Используем наш шейдер
 defaultShader->Use();
 defaultShader->SetMat4("mvp_matrix", mvp);
 
 // Render each object as a textured cube
 for (const auto& object : objects) {
     auto objectPos = object->GetPose();
     glm::vec3 position(objectPos[3][0], objectPos[3][1], objectPos[3][2]);
     
     // Render each cube in the object
     for (size_t i = 0; i < object->GetCubes().size(); i++) {
         auto& cube = object->GetCubes()[i];
         size_t textureId = cube->GetTypeId();
         
         // Check if this cube is selected
         bool is_intersection_exists = WorldInstance->GetIsIntersectionExists();
         size_t intersecion_object_index = WorldInstance->GetIntersectionObjectIndex();
         size_t intersecion_cube_index = WorldInstance->GetIntersectionCubeIndex();
         
         // Find object index in the objects vector
         size_t object_index = std::find(objects.begin(), objects.end(), object) - objects.begin();
         
         // If this cube is selected, use selection texture
         if(is_intersection_exists && intersecion_object_index == object_index && intersecion_cube_index == i)
         {
             textureId = TextureCubeStorageInstance->GetTypeIdByName("selection");
         }
         
         // Get texture
         GLuint texture = 0;
         if (textures.find(textureId) != textures.end()) {
             texture = textures.at(textureId).GetTextureId();
         } else {
             continue; // Skip cube without texture
         }
         
         // Create a simple cube with correct texture coordinates for different faces
float cube_shift = 1.0f/6.0f;
float vertices[] = {
    // positions                   // texture coordinates (like in CubeGL)
    // Face 0 (NEAR) - coordinates 0.0 - 1/6
             -0.5f, -0.5f,  0.5f,         0.0f, 0.0f,
              0.5f, -0.5f,  0.5f,         cube_shift*1.0f, 0.0f,
             -0.5f,  0.5f,  0.5f,         0.0f, 1.0f,
              0.5f,  0.5f,  0.5f,         cube_shift*1.0f, 1.0f,
             
             // Face 1 (RIGHT) - coordinates 1/6 - 2/6
              0.5f, -0.5f,  0.5f,         cube_shift*1.0f, 0.0f,
              0.5f, -0.5f, -0.5f,         cube_shift*2.0f, 0.0f,
              0.5f,  0.5f,  0.5f,         cube_shift*1.0f, 1.0f,
              0.5f,  0.5f, -0.5f,         cube_shift*2.0f, 1.0f,
               
             // Face 2 (FAR) - coordinates 2/6 - 3/6
              0.5f, -0.5f, -0.5f,         cube_shift*2.0f, 0.0f,
             -0.5f, -0.5f, -0.5f,         cube_shift*3.0f, 0.0f,
              0.5f,  0.5f, -0.5f,         cube_shift*2.0f, 1.0f,
             -0.5f,  0.5f, -0.5f,         cube_shift*3.0f, 1.0f,
               
             // Face 3 (LEFT) - coordinates 3/6 - 4/6
             -0.5f, -0.5f, -0.5f,         cube_shift*3.0f, 0.0f,
             -0.5f, -0.5f,  0.5f,         cube_shift*4.0f, 0.0f,
             -0.5f,  0.5f, -0.5f,         cube_shift*3.0f, 1.0f,
             -0.5f,  0.5f,  0.5f,         cube_shift*4.0f, 1.0f,
               
             // Face 4 (TOP) - coordinates 4/6 - 5/6
             -0.5f,  0.5f,  0.5f,         cube_shift*4.0f, 0.0f,
              0.5f,  0.5f,  0.5f,         cube_shift*5.0f, 0.0f,
             -0.5f,  0.5f, -0.5f,         cube_shift*4.0f, 1.0f,
              0.5f,  0.5f, -0.5f,         cube_shift*5.0f, 1.0f,
               
             // Face 5 (BOTTOM) - coordinates 5/6 - 1.0
             -0.5f, -0.5f, -0.5f,         cube_shift*5.0f, 0.0f,
              0.5f, -0.5f, -0.5f,         1.0f, 0.0f,
             -0.5f, -0.5f,  0.5f,         cube_shift*5.0f, 1.0f,
              0.5f, -0.5f,  0.5f,         1.0f, 1.0f
         };
         
         // Indices for rendering faces as triangles
unsigned int indices[] = {
    // Face 0 (NEAR)
             0, 1, 2, 2, 1, 3,
             // Face 1 (RIGHT)  
             4, 5, 6, 6, 5, 7,
             // Face 2 (FAR)
             8, 9, 10, 10, 9, 11,
             // Face 3 (LEFT)
             12, 13, 14, 14, 13, 15,
             // Face 4 (TOP)
             16, 17, 18, 18, 17, 19,
             // Face 5 (BOTTOM)
             20, 21, 22, 22, 21, 23
         };
         
         // Создаем VAO и VBO
         GLuint VAO, VBO, EBO;
         glGenVertexArrays(1, &VAO);
         glGenBuffers(1, &VBO);
         glGenBuffers(1, &EBO);
         
         glBindVertexArray(VAO);
         glBindBuffer(GL_ARRAY_BUFFER, VBO);
         glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
         
         glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
         glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
         
         // Настраиваем атрибуты
         glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
         glEnableVertexAttribArray(0);
         glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
         glEnableVertexAttribArray(1);
         
         // Bind texture
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, texture);
         defaultShader->SetInt("texture0", 0);
         
         // Create model matrix for cube positioning
         glm::mat4 model = glm::mat4(1.0f);
         model = glm::translate(model, position);
         
         // Rotate scene 180 degrees around Y axis
         //model = glm::rotate(model, glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f));
         
         // Update MVP matrix for this object
         glm::mat4 objectMVP = camera->GetProjection() * camera->GetViewMatrix() * model;
         defaultShader->SetMat4("mvp_matrix", objectMVP);
         
         // Рисуем куб
         glBindVertexArray(VAO);
         glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
         
         // Disable attributes
         glDisableVertexAttribArray(0);
         glDisableVertexAttribArray(1);
         
         // Очищаем ресурсы
         glDeleteVertexArrays(1, &VAO);
         glDeleteBuffers(1, &VBO);
         glDeleteBuffers(1, &EBO);
     }
 }
 
 // Отключаем шейдер
 defaultShader->Unuse();
 
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
 
 if (cullFaceEnabled) {
     glEnable(GL_CULL_FACE);
 } else {
     glDisable(GL_CULL_FACE);
 }
 
 auto t_end = std::chrono::high_resolution_clock::now();
 DurationDrawSceneMks = std::chrono::duration<double, std::micro>(t_end-t_begin).count();
}

void GeometryEngine::PrepareRenderBatches(const std::vector<std::shared_ptr<Object>>& objects, 
                                         const std::map<size_t, TextureCube>& textures,
                                         bool is_intersection_exists, 
                                         size_t intersecion_object_index, 
                                         size_t intersecion_cube_index)
{
 std::cout << "PrepareRenderBatches: " << objects.size() << " objects, " << textures.size() << " textures" << std::endl;
 
 renderBatches.clear();
 std::unordered_map<size_t, RenderBatch> batchMap;
 
 std::cout << "PrepareRenderBatches: Processing objects..." << std::endl;

   for(size_t j = 0; j < objects.size(); j++)
 {
  auto& object = objects[j];
  auto objectPos = object->GetPose();
  std::cout << "PrepareRenderBatches: Processing object " << j << " with " << object->GetCubes().size() << " cubes at position (" 
            << objectPos[3][0] << ", " << objectPos[3][1] << ", " << objectPos[3][2] << ")" << std::endl;
  
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
        batch.textureID = textures.at(textureId).GetTextureId(); // Use GLuint instead of QOpenGLTexture
    }
    
         // Use identity matrix since cubes already have correct local coordinates
     glm::mat4 modelMatrix = glm::mat4(1.0f);
     batch.modelMatrices.push_back(modelMatrix);
     batch.objects.push_back(object);
     batch.cubeIndices.push_back(i);
  }
 }

 // Convert map to vector
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
 if (batch.modelMatrices.empty()) {
     std::cout << "DrawBatch: Empty batch, skipping" << std::endl;
     return;
 }
 
 std::cout << "DrawBatch: Drawing " << batch.modelMatrices.size() << " objects" << std::endl;
 
 glBindTexture(GL_TEXTURE_2D, batch.textureID);
 defaultShader->Use();
 defaultShader->SetInt("texture0", 0);
 
 // Here we can add instanced rendering for even greater optimization
// For now use simple batch rendering
 for (size_t i = 0; i < batch.modelMatrices.size(); ++i) {
     // Use MVP matrix directly since cubes already have correct coordinates
     defaultShader->SetMat4("mvp_matrix", mvp_matrix);
     
     // Find cube for rendering
     auto& object = batch.objects[i];
     auto& cube = object->GetCubes()[batch.cubeIndices[i]];
     
     DrawCube(cube, batch.textureID);
 }
 
 defaultShader->Unuse();
}

void GeometryEngine::UpdateFrustumCulling(const glm::mat4& view_projection)
{
 // Extract frustum planes from view-projection matrix
 glm::mat4 m = glm::transpose(view_projection);
 
 // Left plane
 frustumPlanes[0].normal = glm::vec4(m[3][0] + m[0][0], m[3][1] + m[0][1], m[3][2] + m[0][2], m[3][3] + m[0][3]);
 frustumPlanes[0].distance = frustumPlanes[0].normal.w;
 frustumPlanes[0].normal.w = 0.0f;
 frustumPlanes[0].normal = glm::normalize(frustumPlanes[0].normal);
 
 // Right plane
 frustumPlanes[1].normal = glm::vec4(m[3][0] - m[0][0], m[3][1] - m[0][1], m[3][2] - m[0][2], m[3][3] - m[0][3]);
 frustumPlanes[1].distance = frustumPlanes[1].normal.w;
 frustumPlanes[1].normal.w = 0.0f;
 frustumPlanes[1].normal = glm::normalize(frustumPlanes[1].normal);
 
 // Bottom plane
 frustumPlanes[2].normal = glm::vec4(m[3][0] + m[1][0], m[3][1] + m[1][1], m[3][2] + m[1][2], m[3][3] + m[1][3]);
 frustumPlanes[2].distance = frustumPlanes[2].normal.w;
 frustumPlanes[2].normal.w = 0.0f;
 frustumPlanes[2].normal = glm::normalize(frustumPlanes[2].normal);
 
 // Top plane
 frustumPlanes[3].normal = glm::vec4(m[3][0] - m[1][0], m[3][1] - m[1][1], m[3][2] - m[1][2], m[3][3] - m[1][3]);
 frustumPlanes[3].distance = frustumPlanes[3].normal.w;
 frustumPlanes[3].normal.w = 0.0f;
 frustumPlanes[3].normal = glm::normalize(frustumPlanes[3].normal);
 
 // Near plane
 frustumPlanes[4].normal = glm::vec4(m[3][0] + m[2][0], m[3][1] + m[2][1], m[3][2] + m[2][2], m[3][3] + m[2][3]);
 frustumPlanes[4].distance = frustumPlanes[4].normal.w;
 frustumPlanes[4].normal.w = 0.0f;
 frustumPlanes[4].normal = glm::normalize(frustumPlanes[4].normal);
 
 // Far plane
 frustumPlanes[5].normal = glm::vec4(m[3][0] - m[2][0], m[3][1] - m[2][1], m[3][2] - m[2][2], m[3][3] - m[2][3]);
 frustumPlanes[5].distance = frustumPlanes[5].normal.w;
 frustumPlanes[5].normal.w = 0.0f;
 frustumPlanes[5].normal = glm::normalize(frustumPlanes[5].normal);
}

bool GeometryEngine::IsObjectInFrustum(const std::shared_ptr<Object>& object)
{
 glm::vec3 objectPos(object->GetPose()[0][3], object->GetPose()[1][3], object->GetPose()[2][3]);
 float radius = 1.0f; // Approximate object radius
 
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
 if(!cube) {
     std::cout << "DrawCube: Failed to cast to CubeGL" << std::endl;
     return;
 }
 
 std::cout << "DrawCube: Drawing cube with texture " << texture << std::endl;

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
 std::cout << "DrawCubeGeometry: Starting render with " << objects.size() << " objects" << std::endl;
 
 // Enable depth buffer
 glEnable(GL_DEPTH_TEST);

 // Enable back face culling
 glEnable(GL_CULL_FACE);

 // Add debug information about OpenGL state
 std::cout << "=== OpenGL State Debug ===" << std::endl;
 GLboolean depthTest;
 glGetBooleanv(GL_DEPTH_TEST, &depthTest);
 std::cout << "GL_DEPTH_TEST: " << (depthTest ? "ENABLED" : "DISABLED") << std::endl;
 
 GLboolean blending;
 glGetBooleanv(GL_BLEND, &blending);
 std::cout << "GL_BLEND: " << (blending ? "ENABLED" : "DISABLED") << std::endl;
 
 GLint depthFunc;
 glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
 std::cout << "GL_DEPTH_FUNC: " << depthFunc << std::endl;
 
 GLboolean cullFace;
 glGetBooleanv(GL_CULL_FACE, &cullFace);
 std::cout << "GL_CULL_FACE: " << (cullFace ? "ENABLED" : "DISABLED") << std::endl;
 
 std::cout << "=== End OpenGL State Debug ===" << std::endl;

 // Clear buffers
 if(useGradientSky)
 {
  // For gradient sky clear only depth buffer
glClear(GL_DEPTH_BUFFER_BIT); // Return depth buffer clearing
 }
 else
 {
  // For simple sky set color and clear both buffers
glClearColor(skyColor.x, skyColor.y, skyColor.z, skyColor.w); // Return sky color
glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // Return buffer clearing
 }

 // Enable sky rendering
// Render gradient sky AFTER clearing depth buffer
 if(useGradientSky)
 {
     std::cout << "Drawing gradient sky with color: (" << skyColor.x << ", " << skyColor.y << ", " << skyColor.z << ", " << skyColor.w << ")" << std::endl;
  DrawSkyGradient();
  std::cout << "Gradient sky drawn." << std::endl;
 }

 // Bind shader pipeline for use
 if (!defaultShader || !defaultShader->IsValid()) {
     std::cerr << "DrawCubeGeometry: Default shader is not valid!" << std::endl;
     return;
 }
 
 std::cout << "DrawCubeGeometry: Using default shader" << std::endl;
 defaultShader->Use();

 // Set modelview-projection matrix
 std::cout << "DrawCubeGeometry: Setting MVP matrix" << std::endl;
 defaultShader->SetMat4("mvp_matrix", mvp_matrix);

 //glUniform1f(alphaUniformLocation, alpha);

     auto textures = TextureCubeStorageInstance->GetTextures();
 
 std::cout << "Objects to render:" << objects.size() << std::endl;
 std::cout << "Textures available:" << textures.size() << std::endl;
  
 // Optimized rendering with batch processing
 std::cout << "DrawCubeGeometry: Preparing render batches..." << std::endl;
 PrepareRenderBatches(objects, textures, is_intersection_exists, intersecion_object_index, intersecion_cube_index);
 std::cout << "Render batches prepared:" << renderBatches.size() << std::endl;
 
 std::cout << "DrawCubeGeometry: Rendering batches..." << std::endl;
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
  }
 }
 defaultShader->Unuse();

 glDisable(GL_CULL_FACE);
 glDisable(GL_DEPTH_TEST);
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

// Methods for sky color management
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
    
    // Get object count
    size_t objectCount = WorldInstance->GetObjects().size();
    
    // Form performance information strings
    std::vector<std::string> performanceLines = {
        "Performance:",
        "FPS: " + std::to_string(fps).substr(0, 6),
        "Objects: " + std::to_string(objectCount),
        "Scene: " + std::to_string(DurationDrawSceneMks/1000.0).substr(0, 6) + " ms",
        "Movement: " + std::to_string(WorldInstance->GetDurationDoMovementMks()/1000.0).substr(0, 6) + " ms",
        "View: " + std::to_string(view_duration/1000.0).substr(0, 6) + " ms"
    };
    
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

void GeometryEngine::RenderTestCube()
{
    std::cout << "GeometryEngine: Rendering minimal test triangle..." << std::endl;
    
    // Сохраняем состояние OpenGL
    GLboolean depthTestEnabled;
    glGetBooleanv(GL_DEPTH_TEST, &depthTestEnabled);
    GLboolean blendEnabled;
    glGetBooleanv(GL_BLEND, &blendEnabled);
    GLboolean cullFaceEnabled;
    glGetBooleanv(GL_CULL_FACE, &cullFaceEnabled);
    
    // Create simple shader for minimal test
    const char* vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aColor;
        out vec3 ourColor;
        void main()
        {
            gl_Position = vec4(aPos, 1.0);
            ourColor = aColor;
        }
    )";
    
    const char* fragmentShaderSource = R"(
        #version 330 core
        in vec3 ourColor;
        out vec4 FragColor;
        void main()
        {
            FragColor = vec4(ourColor, 1.0);
        }
    )";
    
    // Компилируем шейдеры
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);
    
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);
    
    // Создаем программу
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    
    // Проверяем ошибки компиляции
    GLint success;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLchar infoLog[512];
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cerr << "Vertex shader compilation failed: " << infoLog << std::endl;
    }
    
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLchar infoLog[512];
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cerr << "Fragment shader compilation failed: " << infoLog << std::endl;
    }
    
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        GLchar infoLog[512];
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cerr << "Shader program linking failed: " << infoLog << std::endl;
    }
    
    // Create triangle data (in normalized screen coordinates)
    float vertices[] = {
        // позиции        // цвета
        -0.8f, -0.8f, 0.0f,  1.0f, 0.0f, 0.0f,  // red (larger and to the left)
         0.8f, -0.8f, 0.0f,  0.0f, 1.0f, 0.0f,  // green (larger and to the right)
         0.0f,  0.8f, 0.0f,  0.0f, 0.0f, 1.0f   // blue (higher)
    };
    
    // Создаем VAO и VBO
    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    // Настраиваем атрибуты
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    // Отключаем тест глубины для 2D рендеринга
    glDisable(GL_DEPTH_TEST);
    
    // Используем наш шейдер
    glUseProgram(shaderProgram);
    
    // Рисуем треугольник
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    
    // Проверяем OpenGL ошибки
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        std::cerr << "RenderTestTriangle: OpenGL error after drawing: " << error << " (0x" << std::hex << error << std::dec << ")" << std::endl;
    }
    
    // Очищаем ресурсы
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
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
    
    if (cullFaceEnabled) {
        glEnable(GL_CULL_FACE);
    } else {
        glDisable(GL_CULL_FACE);
    }
    
    std::cout << "GeometryEngine: Minimal test triangle rendered successfully!" << std::endl;
}

void GeometryEngine::Render3DCubeWithPerspective()
{
    std::cout << "GeometryEngine: Rendering 3D cube with perspective..." << std::endl;
    
    // Сохраняем состояние OpenGL
    GLboolean depthTestEnabled;
    glGetBooleanv(GL_DEPTH_TEST, &depthTestEnabled);
    GLboolean blendEnabled;
    glGetBooleanv(GL_BLEND, &blendEnabled);
    GLboolean cullFaceEnabled;
    glGetBooleanv(GL_CULL_FACE, &cullFaceEnabled);
    
    // Get camera
    auto camera = WorldInstance->GetCurrentUserCamera();
    if (!camera) {
        std::cout << "Render3DCubeWithPerspective: Camera is null!" << std::endl;
        return;
    }
    
    // Create shader for 3D rendering
    const char* vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aColor;
        out vec3 ourColor;
        uniform mat4 MVP;
        void main()
        {
            gl_Position = MVP * vec4(aPos, 1.0);
            ourColor = aColor;
        }
    )";
    
    const char* fragmentShaderSource = R"(
        #version 330 core
        in vec3 ourColor;
        out vec4 FragColor;
        void main()
        {
            FragColor = vec4(ourColor, 1.0);
        }
    )";
    
    // Компилируем шейдеры
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);
    
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);
    
    // Создаем программу
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    
    // Проверяем ошибки компиляции
    GLint success;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLchar infoLog[512];
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cerr << "Vertex shader compilation failed: " << infoLog << std::endl;
    }
    
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLchar infoLog[512];
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cerr << "Fragment shader compilation failed: " << infoLog << std::endl;
    }
    
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        GLchar infoLog[512];
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cerr << "Shader program linking failed: " << infoLog << std::endl;
    }
    
    // Create cube data (in world coordinates)
    float vertices[] = {
        // позиции        // цвета
        -0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,  // red
         0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 0.0f,  // green
         0.5f,  0.5f, -0.5f,  0.0f, 0.0f, 1.0f,  // blue
        -0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 0.0f,  // yellow

        -0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 1.0f,  // magenta
         0.5f, -0.5f,  0.5f,  0.0f, 1.0f, 1.0f,  // cyan
         0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 1.0f,  // white
        -0.5f,  0.5f,  0.5f,  0.5f, 0.5f, 0.5f   // gray
    };
    
    // Indices for drawing cube faces
    unsigned int indices[] = {
        0, 1, 2, 2, 3, 0,   // front face
        1, 5, 6, 6, 2, 1,   // right face
        5, 4, 7, 7, 6, 5,   // back face
        4, 0, 3, 3, 7, 4,   // left face
        3, 2, 6, 6, 7, 3,   // top face
        4, 5, 1, 1, 0, 4    // bottom face
    };
    
    // Create VAO, VBO and EBO
    GLuint VAO, VBO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    
    // Настраиваем атрибуты
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    // Включаем тест глубины для 3D рендеринга
    glEnable(GL_DEPTH_TEST);
    
    // Используем наш шейдер
    glUseProgram(shaderProgram);
    
    // Create MVP matrix
    glm::mat4 model = glm::mat4(1.0f);
    // Place cube in static position in front of camera
model = glm::translate(model, glm::vec3(0.0f, 0.0f, -2.0f)); // Fixed position in front of camera
model = glm::rotate(model, glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f)); // Rotate 45 degrees around Y axis
    
    glm::mat4 view = camera->GetViewMatrix();
    glm::mat4 projection = camera->GetProjection();
    glm::mat4 mvp = projection * view * model;
    
    // Set MVP matrix
    GLint mvpLocation = glGetUniformLocation(shaderProgram, "MVP");
    if (mvpLocation != -1) {
        glUniformMatrix4fv(mvpLocation, 1, GL_FALSE, glm::value_ptr(mvp));
    }
    
    // Debug information
    auto cameraPos = camera->GetPosition();
    auto cameraFront = camera->GetFront();
    std::cout << "Render3DCubeWithPerspective: Camera Position: (" << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z << ")" << std::endl;
    std::cout << "Render3DCubeWithPerspective: Camera Front: (" << cameraFront.x << ", " << cameraFront.y << ", " << cameraFront.z << ")" << std::endl;
    std::cout << "Render3DCubeWithPerspective: Test cube position: (0, 0, -2) - STATIC" << std::endl;
    std::cout << "Render3DCubeWithPerspective: Test cube rotation: 45° around Y axis" << std::endl;
    
    // Рисуем куб
    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
    
    // Проверяем OpenGL ошибки
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        std::cerr << "Render3DCubeWithPerspective: OpenGL error after drawing: " << error << " (0x" << std::hex << error << std::dec << ")" << std::endl;
    }
    
    // Очищаем ресурсы
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteProgram(shaderProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
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
    
    if (cullFaceEnabled) {
        glEnable(GL_CULL_FACE);
    } else {
        glDisable(GL_CULL_FACE);
    }
    
    std::cout << "GeometryEngine: 3D cube with perspective rendered successfully!" << std::endl;
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
uiShader->SetVec3("color", glm::vec3(1.0f, 1.0f, 0.0f)); // Yellow color
    
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
     // Use existing TextRenderer if available
     if (textRenderer) {
         // Обновляем размеры окна в TextRenderer
         textRenderer->SetWindowSize(width_size, height_size);
         
         // Display user information
         std::string currentUser = WorldInstance->GetCurrentUserName();
         textRenderer->RenderText("User: " + currentUser, 20, 140, 0.8f, glm::vec3(0.0f, 1.0f, 1.0f)); // Cyan color
         
         // Display key legend at bottom of screen
         textRenderer->RenderText("WASD - Movement", 20, 120, 0.8f, glm::vec3(1.0f, 1.0f, 1.0f));
         textRenderer->RenderText("Q/E - Up/Down", 20, 100, 0.8f, glm::vec3(1.0f, 1.0f, 1.0f));
         textRenderer->RenderText("Space - Jump", 20, 80, 0.8f, glm::vec3(1.0f, 1.0f, 1.0f));
         textRenderer->RenderText("Shift - Crouch", 20, 60, 0.8f, glm::vec3(1.0f, 1.0f, 1.0f));
         textRenderer->RenderText("0-9 - Block selection", 20, 40, 0.8f, glm::vec3(1.0f, 1.0f, 1.0f));
         textRenderer->RenderText("Delete - Remove block", 20, 20, 0.8f, glm::vec3(1.0f, 1.0f, 1.0f));
         textRenderer->RenderText("F1-F8 - Sky colors", 20, 5, 0.8f, glm::vec3(1.0f, 1.0f, 1.0f));
         
         // Display mouse information
         textRenderer->RenderText("Right Mouse - Camera control", 20, -15, 0.8f, glm::vec3(1.0f, 1.0f, 0.0f)); // Yellow color
textRenderer->RenderText("Left Mouse - Add/Remove blocks", 20, -35, 0.8f, glm::vec3(1.0f, 1.0f, 0.0f)); // Yellow color
         
         return;
     }
     
     // Fallback: if TextRenderer is unavailable, output error message
     std::cerr << "TextRenderer is not available" << std::endl;
 }
 
 


 
 }

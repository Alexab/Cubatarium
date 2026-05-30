
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <chrono>
#include <GL/glew.h>
#include "GeometryEngine.h"
#include "GridMath.h"
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
    DestroyCubeBuffers();
    DestroyPreviewBuffers();
    DestroyOutlineBuffers();
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

 if (!InitFaceQuadBuffers()) {
     std::cerr << "Failed to initialize face quad buffers" << std::endl;
     return false;
 }
 blockBatchesValid_ = false;
 
 // Initialize preview buffers
 InitPreviewBuffers();

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
 
 // Instanced shader with mat4 per-instance
 instancedShader = shaderManager->CreateShader("instanced", "shaders/vshader_instanced.glsl", "shaders/fshader.glsl");
 if (!instancedShader || !instancedShader->IsValid()) {
     std::cerr << "Failed to create instanced shader from files, trying inline sources" << std::endl;
     const char* instancedVS = R"(
 #version 330 core
 layout (location = 0) in vec3 aPos;
 layout (location = 1) in vec2 aTexCoord;
 layout (location = 2) in mat4 instanceMVP;
 layout (location = 6) in vec4 instanceAtlasUV;
 layout (location = 7) in vec2 instanceQuadSize;
 out vec2 TexCoord;
 void main()
 {
     gl_Position = instanceMVP * vec4(aPos, 1.0);
     vec2 quadSize = max(instanceQuadSize, vec2(1.0));
     vec2 tiled = fract(aTexCoord * quadSize);
     TexCoord = mix(instanceAtlasUV.xy, instanceAtlasUV.zw, tiled);
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

 outlineShader = shaderManager->CreateShader("outline", "shaders/vshader.glsl", "shaders/fshader_2d.glsl");
 if (!outlineShader || !outlineShader->IsValid()) {
     std::cerr << "Failed to create outline shader" << std::endl;
     return false;
 }
 
 return true;
}

void GeometryEngine::Paint(int width_size, int height_size, double view_duration)
{
     // Render only scene objects
 DrawCubeGeometry();
 
     // Render crosshair
if (showCrosshair) {
    RenderCrosshair(width_size, height_size);
}
 
     // Render simple text
if (showHud) {
    RenderSimpleText(width_size, height_size);
    RenderActiveObjectPreview(width_size, height_size);
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
  if (faceVAO == 0) {
      if (!InitFaceQuadBuffers()) {
          std::cerr << "DrawCubeGeometry: face quad buffers not initialized" << std::endl;
          return;
      }
  }

  auto textures = TextureCubeStorageInstance->GetTextures();
  const auto& blockInstances = WorldInstance->GetBlockRenderInstances();
  const size_t instanceCount = blockInstances.size();
  const uint64_t meshRevision = WorldInstance->GetMeshRevision();
  if (!blockBatchesValid_ || instanceCount != cachedInstanceCount_
      || meshRevision != cachedMeshRevision_) {
   PrepareRenderBatchesFromBlocks(blockInstances, textures);
   cachedInstanceCount_ = instanceCount;
   cachedMeshRevision_ = meshRevision;
   blockBatchesValid_ = true;
  }
 
  // Render all batches instanced
  glm::mat4 dummy_mvp = camera->GetMvpMatrix();
  RenderBatches(dummy_mvp);

  RenderSelectionOutline();
 
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

void GeometryEngine::PrepareRenderBatchesFromBlocks(const std::vector<BlockInstance>& instances,
                                                    const std::map<size_t, TextureCube>& textures)
{
 renderBatches.clear();
 std::unordered_map<size_t, RenderBatch> batchMap;

 for (const auto& instance : instances) {
  const size_t textureId = static_cast<size_t>(instance.id);
  auto& batch = batchMap[textureId];
  if (batch.textureID == 0) {
   const auto texIt = textures.find(textureId);
   if (texIt == textures.end()) {
    continue;
   }
   batch.textureID = texIt->second.GetTextureId();
  }
  batch.modelMatrices.push_back(instance.model);
  batch.atlasUVs.push_back(instance.atlasUV);
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
 instancedShader->Use();
 instancedShader->SetInt("texture0", 0);
 
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

 const bool isBlockBatch = batch.objects.empty() && !batch.modelMatrices.empty()
     && batch.atlasUVs.size() == batch.modelMatrices.size()
     && batch.quadSizes.size() == batch.modelMatrices.size();
 const GLsizei indexCount = isBlockBatch ? 6 : 36;
 GLuint vao = isBlockBatch ? faceVAO : cubeVAO;
 if (isBlockBatch && faceVAO == 0) {
  if (!InitFaceQuadBuffers()) {
   return;
  }
  vao = faceVAO;
 }

 if (isBlockBatch) {
  struct BlockDrawInstance {
   glm::mat4 mvp;
   glm::vec4 atlasUV;
   glm::vec2 quadSize;
  };
  std::vector<BlockDrawInstance> blockInstances;
  blockInstances.reserve(batch.modelMatrices.size());
  const glm::mat4 vp = camera->GetProjection() * camera->GetViewMatrix();
  for (size_t i = 0; i < batch.modelMatrices.size(); ++i) {
   BlockDrawInstance inst;
   inst.mvp = vp * batch.modelMatrices[i];
   inst.atlasUV = batch.atlasUVs[i];
   inst.quadSize = batch.quadSizes[i];
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
 const GLsizei instanceCount = isBlockBatch
     ? static_cast<GLsizei>(batch.modelMatrices.size())
     : static_cast<GLsizei>(instanceMVPs.size());
 if (instanceCount > 0) {
  glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0, instanceCount);
 }
 glBindVertexArray(0);
 glBindBuffer(GL_ARRAY_BUFFER, 0);

 instancedShader->Unuse();
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
         textRenderer->RenderText("Space - Jump, 2xSpace - Flight", 20, 80, 0.8f, glm::vec3(1.0f, 1.0f, 1.0f));
         textRenderer->RenderText("Shift - Crouch", 20, 60, 0.8f, glm::vec3(1.0f, 1.0f, 1.0f));
         textRenderer->RenderText("0-9 - Block selection", 20, 40, 0.8f, glm::vec3(1.0f, 1.0f, 1.0f));
         textRenderer->RenderText("Delete - Remove block", 20, 20, 0.8f, glm::vec3(1.0f, 1.0f, 1.0f));
         textRenderer->RenderText("F1-F8 - Sky colors", 20, 5, 0.8f, glm::vec3(1.0f, 1.0f, 1.0f));

         const double now = std::chrono::duration<double>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
         if (!transientMessage_.empty() && now < transientMessageUntil_) {
             textRenderer->RenderText(transientMessage_, 20.0f, static_cast<float>(height_size) - 60.0f,
                                      1.0f, glm::vec3(1.0f, 0.85f, 0.2f));
         }
         
         // Display mouse information
         textRenderer->RenderText("Right Mouse - Camera control", 20, -15, 0.8f, glm::vec3(1.0f, 1.0f, 0.0f)); // Yellow color
textRenderer->RenderText("Left Mouse - Add/Remove blocks", 20, -35, 0.8f, glm::vec3(1.0f, 1.0f, 0.0f)); // Yellow color
         
         return;
     }
     
     // Fallback: if TextRenderer is unavailable, output error message
     std::cerr << "TextRenderer is not available" << std::endl;
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
        auto user = WorldInstance->GetCurrentUser();
        if (user) {
            const std::string &activeName = user->GetActiveObjectTypeName();
            if (!activeName.empty()) {
                const auto &texMap = TextureCubeStorageInstance->GetTextures();
                for (const auto &kv : texMap) {
                    const TextureCube &tc = kv.second;
                    if (tc.GetName() == activeName) {
                        texId = tc.GetTexture();
                        break;
                    }
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
        -0.5f, -0.5f, 0.0f,  0.0f, 0.0f,
         0.5f, -0.5f, 0.0f,  1.0f, 0.0f,
         0.5f,  0.5f, 0.0f,  1.0f, 1.0f,
        -0.5f,  0.5f, 0.0f,  0.0f, 1.0f,
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

    constexpr std::size_t kStride = sizeof(glm::mat4) + sizeof(glm::vec4) + sizeof(glm::vec2);
    glBindBuffer(GL_ARRAY_BUFFER, instanceBlockVBO);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    std::size_t vec4Size = sizeof(glm::vec4);
    for (int i = 0; i < 4; ++i) {
        glVertexAttribPointer(2 + i, 4, GL_FLOAT, GL_FALSE, kStride, (void*)(i * vec4Size));
        glEnableVertexAttribArray(2 + i);
        glVertexAttribDivisor(2 + i, 1);
    }
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, kStride, (void*)sizeof(glm::mat4));
    glEnableVertexAttribArray(6);
    glVertexAttribDivisor(6, 1);
    glVertexAttribPointer(7, 2, GL_FLOAT, GL_FALSE, kStride,
                          (void*)(sizeof(glm::mat4) + sizeof(glm::vec4)));
    glEnableVertexAttribArray(7);
    glVertexAttribDivisor(7, 1);

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
    outlineShader->SetVec3("color", glm::vec3(0.0f, 0.0f, 0.0f));

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


#ifndef SHADERMANAGER_H
#define SHADERMANAGER_H

// GLEW will be included in .cpp file after GLFW initialization
// Forward declaration for OpenGL types
typedef unsigned int GLuint;
typedef unsigned int GLenum;
typedef int GLint;

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace cutum
{

class UShaderProgram
{
public:
  UShaderProgram();
  ~UShaderProgram();

  // Create and compile shaders
  bool CreateFromFiles(const std::string &vertexPath,
                       const std::string &fragmentPath);
  bool CreateFromStrings(const std::string &vertexSource,
                         const std::string &fragmentSource);

  // Use program
  void Use();
  void Unuse();

  // Check validity
  bool IsValid() const { return programID != 0; }
  GLuint GetProgramID() const { return programID; }

  // Set uniform variables
  void SetBool(const std::string &name, bool value);
  void SetInt(const std::string &name, int value);
  void SetFloat(const std::string &name, float value);
  void SetVec2(const std::string &name, const glm::vec2 &value);
  void SetVec3(const std::string &name, const glm::vec3 &value);
  void SetVec4(const std::string &name, const glm::vec4 &value);
  void SetMat3(const std::string &name, const glm::mat3 &value);
  void SetMat4(const std::string &name, const glm::mat4 &value);

  // Get uniform variable locations
  GLint GetUniformLocation(const std::string &name);

private:
  GLuint programID;
  std::unordered_map<std::string, GLint> uniformLocations;

  // Helper methods
  bool CompileShader(GLuint &shaderID, GLenum shaderType,
                     const std::string &source);
  bool LinkProgram();
  void CheckCompileErrors(GLuint shader, const std::string &type);
  std::string ReadFile(const std::string &filepath);
};

class UShaderManager
{
public:
  UShaderManager();
  ~UShaderManager();

  // Initialization
  bool Initialize();
  void Shutdown();

  // Create and manage shaders
  std::shared_ptr<UShaderProgram> CreateShader(const std::string &name,
                                               const std::string &vertexPath,
                                               const std::string &fragmentPath);

  std::shared_ptr<UShaderProgram>
  CreateShaderFromStrings(const std::string &name,
                          const std::string &vertexSource,
                          const std::string &fragmentSource);

  // Get shaders
  std::shared_ptr<UShaderProgram> GetShader(const std::string &name);
  bool HasShader(const std::string &name) const;

  // Delete shaders
  void RemoveShader(const std::string &name);
  void ClearAllShaders();

  // Preset shaders
  std::shared_ptr<UShaderProgram> GetDefaultShader();
  std::shared_ptr<UShaderProgram> GetSkyShader();
  std::shared_ptr<UShaderProgram> GetTextureShader();

private:
  std::unordered_map<std::string, std::shared_ptr<UShaderProgram>> shaders;

  // Create preset shaders
  void CreateDefaultShaders();

  // Default shader source code
  std::string GetDefaultVertexShader();
  std::string GetDefaultFragmentShader();
  std::string GetSkyVertexShader();
  std::string GetSkyFragmentShader();
  std::string GetTextureVertexShader();
  std::string GetTextureFragmentShader();
};

} // namespace cutum

#endif // SHADERMANAGER_H

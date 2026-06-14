#include "Render/Engine/ShaderManager.h"
#include "App/Platform/IPlatformPaths.h"
#include "Render/GlIncludes.h"
#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <sstream>

namespace cutum
{

UShaderProgram::UShaderProgram() : programID(0) {}

UShaderProgram::~UShaderProgram()
{
  if (programID != 0)
  {
    glDeleteProgram(programID);
    programID = 0;
  }
}

bool UShaderProgram::CreateFromFiles(const std::string &vertexPath,
                                     const std::string &fragmentPath)
{
  std::string vertexSource = ReadFile(vertexPath);
  std::string fragmentSource = ReadFile(fragmentPath);

  if (vertexSource.empty() || fragmentSource.empty())
  {
    return false;
  }

  return CreateFromStrings(vertexSource, fragmentSource);
}

bool UShaderProgram::CreateFromStrings(const std::string &vertexSource,
                                       const std::string &fragmentSource)
{
  GLuint vertexShader, fragmentShader;

  // Vertex shader compilation
  if (!CompileShader(vertexShader, GL_VERTEX_SHADER, vertexSource))
  {
    return false;
  }

  // Fragment shader compilation
  if (!CompileShader(fragmentShader, GL_FRAGMENT_SHADER, fragmentSource))
  {
    glDeleteShader(vertexShader);
    return false;
  }

  // Program creation
  programID = glCreateProgram();
  glAttachShader(programID, vertexShader);
  glAttachShader(programID, fragmentShader);

  // Program linking
  if (!LinkProgram())
  {
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return false;
  }

  // Delete shaders after linking
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  return true;
}

void UShaderProgram::Use() { glUseProgram(programID); }

void UShaderProgram::Unuse() { glUseProgram(0); }

void UShaderProgram::SetBool(const std::string &name, bool value)
{
  GLint location = GetUniformLocation(name);
  if (location != -1)
  {
    glUniform1i(location, static_cast<int>(value));
  }
}

void UShaderProgram::SetInt(const std::string &name, int value)
{
  GLint location = GetUniformLocation(name);
  if (location != -1)
  {
    glUniform1i(location, value);
  }
}

void UShaderProgram::SetFloat(const std::string &name, float value)
{
  GLint location = GetUniformLocation(name);
  if (location != -1)
  {
    glUniform1f(location, value);
  }
}

void UShaderProgram::SetVec2(const std::string &name, const glm::vec2 &value)
{
  GLint location = GetUniformLocation(name);
  if (location != -1)
  {
    glUniform2fv(location, 1, glm::value_ptr(value));
  }
}

void UShaderProgram::SetVec3(const std::string &name, const glm::vec3 &value)
{
  GLint location = GetUniformLocation(name);
  if (location != -1)
  {
    glUniform3fv(location, 1, glm::value_ptr(value));
  }
}

void UShaderProgram::SetVec4(const std::string &name, const glm::vec4 &value)
{
  GLint location = GetUniformLocation(name);
  if (location != -1)
  {
    glUniform4fv(location, 1, glm::value_ptr(value));
  }
}

void UShaderProgram::SetMat3(const std::string &name, const glm::mat3 &value)
{
  GLint location = GetUniformLocation(name);
  if (location != -1)
  {
    glUniformMatrix3fv(location, 1, GL_FALSE, glm::value_ptr(value));
  }
}

void UShaderProgram::SetMat4(const std::string &name, const glm::mat4 &value)
{
  GLint location = GetUniformLocation(name);
  if (location != -1)
  {
    glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
  }
}

GLint UShaderProgram::GetUniformLocation(const std::string &name)
{
  auto it = uniformLocations.find(name);
  if (it != uniformLocations.end())
  {
    return it->second;
  }

  GLint location = glGetUniformLocation(programID, name.c_str());
  uniformLocations[name] = location;
  return location;
}

bool UShaderProgram::CompileShader(GLuint &shaderID, GLenum shaderType,
                                   const std::string &source)
{
  shaderID = glCreateShader(shaderType);
  const char *sourcePtr = source.c_str();
  glShaderSource(shaderID, 1, &sourcePtr, nullptr);
  glCompileShader(shaderID);

  CheckCompileErrors(shaderID,
                     shaderType == GL_VERTEX_SHADER ? "VERTEX" : "FRAGMENT");

  return shaderID != 0;
}

bool UShaderProgram::LinkProgram()
{
  glLinkProgram(programID);
  CheckCompileErrors(programID, "PROGRAM");
  return programID != 0;
}

void UShaderProgram::CheckCompileErrors(GLuint shader, const std::string &type)
{
  GLint success;
  GLchar infoLog[1024];

  if (type != "PROGRAM")
  {
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
      glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
      std::cerr << "Shader compile error [" << type << "]: " << infoLog
                << std::endl;
    }
  }
  else
  {
    glGetProgramiv(shader, GL_LINK_STATUS, &success);
    if (!success)
    {
      glGetProgramInfoLog(shader, 1024, nullptr, infoLog);
      std::cerr << "Shader program link error: " << infoLog << std::endl;
    }
  }
}

namespace
{

std::string ResolveShaderPath(const std::string &filepath)
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  const auto slash = filepath.find_last_of('/');
  const std::string fileName =
      slash == std::string::npos ? filepath : filepath.substr(slash + 1);
  const std::string glesRel = "shaders/gles/" + fileName;
  if (auto *paths = IPlatformPaths::TryGet())
  {
    if (paths->AssetExists(glesRel))
    {
      return (paths->AssetRoot() / glesRel).string();
    }
  }
  return glesRel;
#else
  return filepath;
#endif
}

} // namespace

std::string UShaderProgram::ReadFile(const std::string &filepath)
{
  const std::string resolved = ResolveShaderPath(filepath);
  if (auto *paths = IPlatformPaths::TryGet())
  {
    std::string text;
    if (paths->ReadAssetText(resolved, text))
    {
      return text;
    }
  }
  std::ifstream file(resolved);
  if (!file.is_open())
  {
    std::cerr << "Shader file open error: " << resolved << std::endl;
    return "";
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

// UShaderManager implementation
UShaderManager::UShaderManager() {}

UShaderManager::~UShaderManager() { Shutdown(); }

bool UShaderManager::Initialize()
{
  CreateDefaultShaders();
  return true;
}

void UShaderManager::Shutdown() { ClearAllShaders(); }

std::shared_ptr<UShaderProgram>
UShaderManager::CreateShader(const std::string &name,
                             const std::string &vertexPath,
                             const std::string &fragmentPath)
{
  auto shader = std::make_shared<UShaderProgram>();
  if (shader->CreateFromFiles(vertexPath, fragmentPath))
  {
    shaders[name] = shader;
    return shader;
  }
  return nullptr;
}

std::shared_ptr<UShaderProgram>
UShaderManager::CreateShaderFromStrings(const std::string &name,
                                        const std::string &vertexSource,
                                        const std::string &fragmentSource)
{
  auto shader = std::make_shared<UShaderProgram>();
  if (shader->CreateFromStrings(vertexSource, fragmentSource))
  {
    shaders[name] = shader;
    return shader;
  }
  return nullptr;
}

std::shared_ptr<UShaderProgram>
UShaderManager::GetShader(const std::string &name)
{
  auto it = shaders.find(name);
  if (it != shaders.end())
  {
    return it->second;
  }
  return nullptr;
}

bool UShaderManager::HasShader(const std::string &name) const
{
  return shaders.find(name) != shaders.end();
}

void UShaderManager::RemoveShader(const std::string &name)
{
  shaders.erase(name);
}

void UShaderManager::ClearAllShaders() { shaders.clear(); }

std::shared_ptr<UShaderProgram> UShaderManager::GetDefaultShader()
{
  return GetShader("default");
}

std::shared_ptr<UShaderProgram> UShaderManager::GetSkyShader()
{
  return GetShader("sky");
}

std::shared_ptr<UShaderProgram> UShaderManager::GetTextureShader()
{
  return GetShader("texture");
}

void UShaderManager::CreateDefaultShaders()
{
  // Create default shader
  CreateShaderFromStrings("default", GetDefaultVertexShader(),
                          GetDefaultFragmentShader());

  // Create sky shader
  CreateShaderFromStrings("sky", GetSkyVertexShader(), GetSkyFragmentShader());

  // Create texture shader
  CreateShaderFromStrings("texture", GetTextureVertexShader(),
                          GetTextureFragmentShader());
}

std::string UShaderManager::GetDefaultVertexShader()
{
  return R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    TexCoord = aTexCoord;
}
)";
}

std::string UShaderManager::GetDefaultFragmentShader()
{
  return R"(
#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D texture1;
uniform float alpha;

void main()
{
    vec4 texColor = texture(texture1, TexCoord);
    if(texColor.a < 0.1)
        discard;
    FragColor = vec4(texColor.rgb, texColor.a * alpha);
}
)";
}

std::string UShaderManager::GetSkyVertexShader()
{
  return R"(
#version 330 core
layout (location = 0) in vec3 aPos;

out vec3 TexCoord;

uniform mat4 projection;
uniform mat4 view;

void main()
{
    TexCoord = aPos;
    vec4 pos = projection * view * vec4(aPos, 1.0);
    gl_Position = pos.xyww;
}
)";
}

std::string UShaderManager::GetSkyFragmentShader()
{
  return R"(
#version 330 core
out vec4 FragColor;

in vec3 TexCoord;

uniform vec4 skyColor;
uniform bool useGradient;

void main()
{
    if (useGradient) {
        // Simple gradient from top to bottom
        float gradient = 1.0 - TexCoord.y;
        vec3 gradientColor = mix(skyColor.rgb * 0.5, skyColor.rgb, gradient);
        FragColor = vec4(gradientColor, 1.0);
    } else {
        FragColor = skyColor;
    }
}
)";
}

std::string UShaderManager::GetTextureVertexShader()
{
  return R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    TexCoord = aTexCoord;
}
)";
}

std::string UShaderManager::GetTextureFragmentShader()
{
  return R"(
#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D texture1;

void main()
{
    FragColor = texture(texture1, TexCoord);
}
)";
}

} // namespace cutum

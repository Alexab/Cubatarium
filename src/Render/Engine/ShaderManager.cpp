#include "Render/Engine/ShaderManager.h"
#include "App/Platform/IUPlatformPaths.h"
#include "App/Platform/Log.h"
#include "Render/GlIncludes.h"
#include "Render/Lighting/SectionLightBindingTable.h"
#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <sstream>

namespace cutum
{
namespace
{
// GLSL ES does not allow uniform initializers. Keep the file assets' render
// defaults explicit and identical on both backends, before publication.
bool InitializeFileUniformDefaults(GLuint program, const std::string &path)
{
  const auto slash = path.find_last_of("/\\");
  const auto filename = path.substr(slash == std::string::npos ? 0 : slash + 1);
  const bool greedy = filename == "fshader_greedy.glsl";
  const bool creature = filename == "fshader_creature.glsl" ||
                        filename == "fshader_creature_skinned.glsl";
  if (!greedy && !creature)
    return true;
  if (glGetError() != GL_NO_ERROR)
    return false;
  GLint previous = 0;
  glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
  glUseProgram(program);
  const auto scalar = [&](const char *name, float value)
  { glUniform1f(glGetUniformLocation(program, name), value); };
  if (greedy)
    scalar("uFluidAlpha", 1.f);
  else
  {
    glUniform4f(glGetUniformLocation(program, "uTint"), 1, 1, 1, 1);
    glUniform2f(glGetUniformLocation(program, "uCreatureLight"), 1, 0);
    scalar("uCreatureLitEnabled", 0);
    scalar("uEnvMinAmbient", .12f);
    scalar("uEnvDayFactor", 1);
    scalar("uEnvNightFactor", 0);
    scalar("uEnvSkyLightScale", 1);
    scalar("uCreatureFogFactor", 0);
    glUniform3f(glGetUniformLocation(program, "uFogColor"), .7f, .8f, 1);
    scalar("uFogEnabled", 0);
  }
  const bool initialized = glGetError() == GL_NO_ERROR;
  glUseProgram(static_cast<GLuint>(previous));
  return initialized && glGetError() == GL_NO_ERROR;
}
} // namespace

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

  return CreateProgramCandidate(vertexSource, fragmentSource, fragmentPath);
}

bool UShaderProgram::CreateFromStrings(const std::string &vertexSource,
                                       const std::string &fragmentSource)
{
  return CreateProgramCandidate(vertexSource, fragmentSource, {});
}

bool UShaderProgram::CreateProgramCandidate(const std::string &vertexSource,
                                            const std::string &fragmentSource,
                                            const std::string &fragmentPath)
{
  struct Candidate
  {
    GLuint vertex{0}, fragment{0}, program{0};
    ~Candidate()
    {
      if (program)
        glDeleteProgram(program);
      if (vertex)
        glDeleteShader(vertex);
      if (fragment)
        glDeleteShader(fragment);
    }
  } candidate;

  // Vertex shader compilation
  if (!CompileShader(candidate.vertex, GL_VERTEX_SHADER, vertexSource))
  {
    return false;
  }

  // Fragment shader compilation
  if (!CompileShader(candidate.fragment, GL_FRAGMENT_SHADER, fragmentSource))
  {
    return false;
  }

  // Program creation
  candidate.program = glCreateProgram();
  if (!candidate.program)
    return false;
  glAttachShader(candidate.program, candidate.vertex);
  glAttachShader(candidate.program, candidate.fragment);

  // Program linking
  if (!LinkProgram(candidate.program))
  {
    return false;
  }
  if (!InitializeFileUniformDefaults(candidate.program, fragmentPath))
    return false;

  // Publish only a complete linked program. Failed replacements keep the
  // predecessor and its cached uniform locations, including queued draws.
  const GLuint previous = programID;
  programID = candidate.program;
  candidate.program = 0;
  uniformLocations.clear();
  if (previous)
    glDeleteProgram(previous);
  return true;
}

void UShaderProgram::Use() { glUseProgram(programID); }

void UShaderProgram::Unuse() { glUseProgram(0); }

void UShaderProgram::SetBool(const std::string &Name, bool value)
{
  GLint location = GetUniformLocation(Name);
  if (location != -1)
  {
    glUniform1i(location, static_cast<int>(value));
  }
}

void UShaderProgram::SetInt(const std::string &Name, int value)
{
  GLint location = GetUniformLocation(Name);
  if (location != -1)
  {
    glUniform1i(location, value);
  }
}

void UShaderProgram::SetFloat(const std::string &Name, float value)
{
  GLint location = GetUniformLocation(Name);
  if (location != -1)
  {
    glUniform1f(location, value);
  }
}

void UShaderProgram::SetVec2(const std::string &Name, const glm::vec2 &value)
{
  GLint location = GetUniformLocation(Name);
  if (location != -1)
  {
    glUniform2fv(location, 1, glm::value_ptr(value));
  }
}

void UShaderProgram::SetVec3(const std::string &Name, const glm::vec3 &value)
{
  GLint location = GetUniformLocation(Name);
  if (location != -1)
  {
    glUniform3fv(location, 1, glm::value_ptr(value));
  }
}

void UShaderProgram::SetIVec3(const std::string &Name,
                              const glm::ivec3 &value)
{
  GLint location = GetUniformLocation(Name);
  if (location != -1)
  {
    glUniform3iv(location, 1, glm::value_ptr(value));
  }
}

void UShaderProgram::SetVec4(const std::string &Name, const glm::vec4 &value)
{
  GLint location = GetUniformLocation(Name);
  if (location != -1)
  {
    glUniform4fv(location, 1, glm::value_ptr(value));
  }
}

void UShaderProgram::SetMat3(const std::string &Name, const glm::mat3 &value)
{
  GLint location = GetUniformLocation(Name);
  if (location != -1)
  {
    glUniformMatrix3fv(location, 1, GL_FALSE, glm::value_ptr(value));
  }
}

void UShaderProgram::SetMat4(const std::string &Name, const glm::mat4 &value)
{
  GLint location = GetUniformLocation(Name);
  if (location != -1)
  {
    glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
  }
}

GLint UShaderProgram::GetUniformLocation(const std::string &Name)
{
  auto it = uniformLocations.find(Name);
  if (it != uniformLocations.end())
  {
    return it->second;
  }

  GLint location = glGetUniformLocation(programID, Name.c_str());
  uniformLocations[Name] = location;
  return location;
}

bool UShaderProgram::CompileShader(GLuint &shaderID, GLenum shaderType,
                                   const std::string &source)
{
  const auto expanded = SectionLightBindingTable::ExpandShaderInterface(source);
  shaderID = glCreateShader(shaderType);
  if (!shaderID)
    return false;
  const char *sourcePtr = expanded.c_str();
  glShaderSource(shaderID, 1, &sourcePtr, nullptr);
  glCompileShader(shaderID);

  if (!CheckCompileErrors(
          shaderID, shaderType == GL_VERTEX_SHADER ? "VERTEX" : "FRAGMENT"))
  {
    glDeleteShader(shaderID);
    shaderID = 0;
    return false;
  }
  return true;
}

bool UShaderProgram::LinkProgram(GLuint candidate)
{
  glLinkProgram(candidate);
  return CheckCompileErrors(candidate, "PROGRAM");
}

namespace
{

void LogShaderDiagnostic(const char *type, const char *infoLog)
{
#if defined(__ANDROID__)
  CubatariumLogError("Shader", std::string(type) + ": " + infoLog);
#else
  std::cerr << type << ": " << infoLog << std::endl;
#endif
}

} // namespace

bool UShaderProgram::CheckCompileErrors(GLuint shader, const std::string &type)
{
  GLint success = GL_FALSE;
  GLchar infoLog[1024]{};

  if (type != "PROGRAM")
  {
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
      glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
      LogShaderDiagnostic(("Shader compile error [" + type + "]").c_str(), infoLog);
    }
  }
  else
  {
    glGetProgramiv(shader, GL_LINK_STATUS, &success);
    if (!success)
    {
      glGetProgramInfoLog(shader, 1024, nullptr, infoLog);
      LogShaderDiagnostic("Shader program link error", infoLog);
    }
  }
  return success == GL_TRUE;
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
  if (auto *paths = IUPlatformPaths::TryGet())
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
  if (auto *paths = IUPlatformPaths::TryGet())
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
    const std::string message = "Shader file open error: " + resolved;
#if defined(__ANDROID__)
    CubatariumLogError("Shader", message);
#else
    std::cerr << message << std::endl;
#endif
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
UShaderManager::CreateShader(const std::string &Name,
                             const std::string &vertexPath,
                             const std::string &fragmentPath)
{
  auto shader = std::make_shared<UShaderProgram>();
  if (shader->CreateFromFiles(vertexPath, fragmentPath))
  {
    shaders[Name] = shader;
    return shader;
  }
  return nullptr;
}

std::shared_ptr<UShaderProgram>
UShaderManager::CreateShaderFromStrings(const std::string &Name,
                                        const std::string &vertexSource,
                                        const std::string &fragmentSource)
{
  auto shader = std::make_shared<UShaderProgram>();
  if (shader->CreateFromStrings(vertexSource, fragmentSource))
  {
    shaders[Name] = shader;
    return shader;
  }
  return nullptr;
}

std::shared_ptr<UShaderProgram>
UShaderManager::GetShader(const std::string &Name)
{
  auto it = shaders.find(Name);
  if (it != shaders.end())
  {
    return it->second;
  }
  return nullptr;
}

bool UShaderManager::HasShader(const std::string &Name) const
{
  return shaders.find(Name) != shaders.end();
}

void UShaderManager::RemoveShader(const std::string &Name)
{
  shaders.erase(Name);
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
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  return;
#else
  // Create default shader
  CreateShaderFromStrings("default", GetDefaultVertexShader(),
                          GetDefaultFragmentShader());

  // Create sky shader
  CreateShaderFromStrings("sky", GetSkyVertexShader(), GetSkyFragmentShader());

  // Create texture shader
  CreateShaderFromStrings("texture", GetTextureVertexShader(),
                          GetTextureFragmentShader());
#endif
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

#ifndef SHADERMANAGER_H
#define SHADERMANAGER_H

// GLEW будет включен в .cpp файле после инициализации GLFW
// Forward declaration для OpenGL типов
typedef unsigned int GLuint;
typedef unsigned int GLenum;
typedef int GLint;

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <unordered_map>
#include <memory>

namespace cutum {

class ShaderProgram {
public:
    ShaderProgram();
    ~ShaderProgram();

    // Создание и компиляция шейдеров
    bool CreateFromFiles(const std::string& vertexPath, const std::string& fragmentPath);
    bool CreateFromStrings(const std::string& vertexSource, const std::string& fragmentSource);
    
    // Использование программы
    void Use();
    void Unuse();
    
    // Проверка валидности
    bool IsValid() const { return programID != 0; }
    GLuint GetProgramID() const { return programID; }
    
    // Установка uniform переменных
    void SetBool(const std::string& name, bool value);
    void SetInt(const std::string& name, int value);
    void SetFloat(const std::string& name, float value);
    void SetVec2(const std::string& name, const glm::vec2& value);
    void SetVec3(const std::string& name, const glm::vec3& value);
    void SetVec4(const std::string& name, const glm::vec4& value);
    void SetMat3(const std::string& name, const glm::mat3& value);
    void SetMat4(const std::string& name, const glm::mat4& value);
    
    // Получение location uniform переменных
    GLint GetUniformLocation(const std::string& name);

private:
    GLuint programID;
    std::unordered_map<std::string, GLint> uniformLocations;
    
    // Вспомогательные методы
    bool CompileShader(GLuint& shaderID, GLenum shaderType, const std::string& source);
    bool LinkProgram();
    void CheckCompileErrors(GLuint shader, const std::string& type);
    std::string ReadFile(const std::string& filepath);
};

class ShaderManager {
public:
    ShaderManager();
    ~ShaderManager();

    // Инициализация
    bool Initialize();
    void Shutdown();

    // Создание и управление шейдерами
    std::shared_ptr<ShaderProgram> CreateShader(const std::string& name, 
                                               const std::string& vertexPath, 
                                               const std::string& fragmentPath);
    
    std::shared_ptr<ShaderProgram> CreateShaderFromStrings(const std::string& name,
                                                          const std::string& vertexSource,
                                                          const std::string& fragmentSource);

    // Получение шейдеров
    std::shared_ptr<ShaderProgram> GetShader(const std::string& name);
    bool HasShader(const std::string& name) const;

    // Удаление шейдеров
    void RemoveShader(const std::string& name);
    void ClearAllShaders();

    // Предустановленные шейдеры
    std::shared_ptr<ShaderProgram> GetDefaultShader();
    std::shared_ptr<ShaderProgram> GetSkyShader();
    std::shared_ptr<ShaderProgram> GetTextureShader();

private:
    std::unordered_map<std::string, std::shared_ptr<ShaderProgram>> shaders;
    
    // Создание предустановленных шейдеров
    void CreateDefaultShaders();
    
    // Исходный код шейдеров по умолчанию
    std::string GetDefaultVertexShader();
    std::string GetDefaultFragmentShader();
    std::string GetSkyVertexShader();
    std::string GetSkyFragmentShader();
    std::string GetTextureVertexShader();
    std::string GetTextureFragmentShader();
};

} // namespace cutum

#endif // SHADERMANAGER_H

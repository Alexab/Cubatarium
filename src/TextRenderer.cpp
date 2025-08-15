#include "TextRenderer.h"
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace cutum {

TextRenderer::TextRenderer()
    : textShader(0)
    , VAO(0)
    , VBO(0)
    , windowWidth(1280)
    , windowHeight(720)
{
}

TextRenderer::~TextRenderer() {
    Shutdown();
}

bool TextRenderer::Initialize(const std::string& fontPath, int fontSize) {
    // Создание шейдера
    if (!CreateShader()) {
        std::cerr << "Failed to create text shader" << std::endl;
        return false;
    }

    // Настройка OpenGL для отображения текста
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Создание VAO и VBO для отображения символов
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Загрузка символов (пока используем простую реализацию без FreeType)
    LoadCharacters(fontPath, fontSize);

    return true;
}

void TextRenderer::Shutdown() {
    if (VAO) {
        glDeleteVertexArrays(1, &VAO);
        VAO = 0;
    }
    if (VBO) {
        glDeleteBuffers(1, &VBO);
        VBO = 0;
    }
    if (textShader) {
        glDeleteProgram(textShader);
        textShader = 0;
    }
    
    // Удаление текстур символов
    for (auto& [c, ch] : characters) {
        glDeleteTextures(1, &ch.textureID);
    }
    characters.clear();
}

bool TextRenderer::CreateShader() {
    const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec4 vertex;
out vec2 TexCoords;

uniform mat4 projection;

void main()
{
    gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);
    TexCoords = vertex.zw;
}
)";

    const char* fragmentShaderSource = R"(
#version 330 core
out vec4 color;

in vec2 TexCoords;

uniform vec3 textColor;

void main()
{    
    // Простая версия - просто используем цвет текста без текстуры
    color = vec4(textColor, 1.0);
}
)";

    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    // Проверка компиляции vertex shader
    GLint success;
    GLchar infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cerr << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
        return false;
    }

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    // Проверка компиляции fragment shader
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cerr << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
        return false;
    }

    textShader = glCreateProgram();
    glAttachShader(textShader, vertexShader);
    glAttachShader(textShader, fragmentShader);
    glLinkProgram(textShader);

    // Проверка линковки программы
    glGetProgramiv(textShader, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(textShader, 512, NULL, infoLog);
        std::cerr << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
        return false;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Кэширование uniform locations
    textColorLocation = glGetUniformLocation(textShader, "textColor");
    projectionLocation = glGetUniformLocation(textShader, "projection");

    return textShader != 0;
}

bool TextRenderer::LoadCharacters(const std::string& fontPath, int fontSize) {
    // Простая реализация без FreeType - создаем базовые символы
    // В реальном проекте здесь должна быть загрузка через FreeType
    
    // Создаем простые текстуры для символов (белые квадраты)
    // Расширяем диапазон для поддержки кириллицы и других символов
    for (unsigned char c = 0; c < 255; c++) {
        Character character;
        
        // Создаем простую текстуру 8x8 пикселей - ВСЕ пиксели белые
        unsigned char data[8 * 8];
        for (int i = 0; i < 8 * 8; i++) {
            data[i] = 255; // Белый цвет для всех пикселей
        }
        
        glGenTextures(1, &character.textureID);
        glBindTexture(GL_TEXTURE_2D, character.textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 8, 8, 0, GL_RED, GL_UNSIGNED_BYTE, data);
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        // Разные размеры для разных символов
        if (c == ' ') {
            // Пробел - маленький размер
            character.size = glm::ivec2(4, 4);
            character.bearing = glm::ivec2(0, 0);
            character.advance = 4;
        } else if (c >= 'A' && c <= 'Z') {
            // Заглавные буквы - больший размер
            character.size = glm::ivec2(12, 12);
            character.bearing = glm::ivec2(0, 0);
            character.advance = 12;
        } else if (c >= 'a' && c <= 'z') {
            // Строчные буквы - средний размер
            character.size = glm::ivec2(10, 10);
            character.bearing = glm::ivec2(0, 0);
            character.advance = 10;
        } else if (c >= '0' && c <= '9') {
            // Цифры - средний размер
            character.size = glm::ivec2(10, 10);
            character.bearing = glm::ivec2(0, 0);
            character.advance = 10;
        } else if (c >= 128) {
            // Кириллица и другие символы - средний размер
            character.size = glm::ivec2(10, 10);
            character.bearing = glm::ivec2(0, 0);
            character.advance = 10;
        } else {
            // Остальные символы - стандартный размер
            character.size = glm::ivec2(8, 8);
            character.bearing = glm::ivec2(0, 0);
            character.advance = 8;
        }
        
        characters[c] = character;
    }
    
    return true;
}

void TextRenderer::RenderText(const std::string& text, float x, float y, float scale, const glm::vec3& color) {
    if (!textShader) {
        std::cerr << "TextRenderer: textShader is null!" << std::endl;
        return;
    }
    
    // Проверяем OpenGL ошибки перед рендерингом
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        std::cerr << "TextRenderer: OpenGL error before rendering: " << error << " (0x" << std::hex << error << std::dec << ")" << std::endl;
        // Очищаем ошибки
        while (glGetError() != GL_NO_ERROR);
    }
    
    // Убеждаемся, что blending включен для текста
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glUseProgram(textShader);
    
    // Проверяем uniform locations
    if (textColorLocation == -1) {
        std::cerr << "TextRenderer: textColorLocation is invalid!" << std::endl;
        return;
    }
    if (projectionLocation == -1) {
        std::cerr << "TextRenderer: projectionLocation is invalid!" << std::endl;
        return;
    }
    
    glUniform3f(textColorLocation, color.x, color.y, color.z);
    glBindVertexArray(VAO);

    // Ортографическая проекция для 2D текста
    glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(windowWidth), 0.0f, static_cast<float>(windowHeight));
    glUniformMatrix4fv(projectionLocation, 1, GL_FALSE, glm::value_ptr(projection));

    std::string::const_iterator c;
    for (c = text.begin(); c != text.end(); c++) {
        Character ch = characters[*c];

        float xpos = x + ch.bearing.x * scale;
        float ypos = y - (ch.size.y - ch.bearing.y) * scale;

        float w = ch.size.x * scale;
        float h = ch.size.y * scale;

        // Для отладки - рисуем простые цветные прямоугольники
        // Обновляем VBO для каждого символа
        float vertices[6][4] = {
            { xpos,     ypos + h,   0.0f, 0.0f },
            { xpos,     ypos,       0.0f, 1.0f },
            { xpos + w, ypos,       1.0f, 1.0f },

            { xpos,     ypos + h,   0.0f, 0.0f },
            { xpos + w, ypos,       1.0f, 1.0f },
            { xpos + w, ypos + h,   1.0f, 0.0f }
        };

        // Не используем текстуры вообще
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        x += (ch.advance >> 6) * scale; // Сдвигаем курсор для следующего символа
    }
    glBindVertexArray(0);
    
    // Проверяем OpenGL ошибки после рендеринга
    error = glGetError();
    if (error != GL_NO_ERROR) {
        std::cerr << "TextRenderer: OpenGL error after rendering: " << error << " (0x" << std::hex << error << std::dec << ")" << std::endl;
    }
}

void TextRenderer::RenderTextCentered(const std::string& text, float y, float scale, const glm::vec3& color) {
    glm::vec2 textSize = GetTextSize(text, scale);
    float x = (windowWidth - textSize.x) / 2.0f;
    RenderText(text, x, y, scale, color);
}

glm::vec2 TextRenderer::GetTextSize(const std::string& text, float scale) {
    float width = 0.0f;
    float height = 0.0f;
    
    std::string::const_iterator c;
    for (c = text.begin(); c != text.end(); c++) {
        Character ch = characters[*c];
        width += (ch.advance >> 6) * scale;
        height = std::max(height, static_cast<float>(ch.size.y) * scale);
    }
    
    return glm::vec2(width, height);
}

void TextRenderer::SetWindowSize(int width, int height) {
    windowWidth = width;
    windowHeight = height;
}

} // namespace cutum

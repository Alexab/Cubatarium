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
    , ft(nullptr)
    , face(nullptr)
    , ftInitialized(false)
{
}

TextRenderer::~TextRenderer() {
    Shutdown();
}

bool TextRenderer::Initialize(const std::string& fontPath, int fontSize) {
    // Инициализация FreeType
    if (!InitializeFreeType()) {
        std::cerr << "Failed to initialize FreeType" << std::endl;
        return false;
    }

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

    // Загрузка символов с помощью FreeType
    if (!LoadCharacters(fontPath, fontSize)) {
        std::cerr << "Failed to load characters" << std::endl;
        return false;
    }

    return true;
}

GLuint TextRenderer::CreateSimpleTextTexture()
{
    // Создаем простую текстуру 16x16 пикселей для символа
    // Это будет простой белый квадрат с черной рамкой
    unsigned char textureData[16 * 16] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // Верхняя строка - черная
        0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Вторая строка - белая с черными краями
        0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Третья строка - белая с черными краями
        0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Четвертая строка - белая с черными краями
        0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Пятая строка - белая с черными краями
        0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Шестая строка - белая с черными краями
        0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Седьмая строка - белая с черными краями
        0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Восьмая строка - белая с черными краями
        0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Девятая строка - белая с черными краями
        0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Десятая строка - белая с черными краями
        0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Одиннадцатая строка - белая с черными краями
        0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Двенадцатая строка - белая с черными краями
        0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Тринадцатая строка - белая с черными краями
        0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Четырнадцатая строка - белая с черными краями
        0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Пятнадцатая строка - белая с черными краями
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0   // Нижняя строка - черная
    };
    
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 16, 16, 0, GL_RED, GL_UNSIGNED_BYTE, textureData);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    return textureID;
}

GLuint TextRenderer::CreateCharacterTexture(char character)
{
    // Создаем текстуру 16x16 пикселей для символа
    unsigned char textureData[16 * 16];
    
    // Заполняем текстуру в зависимости от символа
    for (int i = 0; i < 16 * 16; i++) {
        textureData[i] = 0; // По умолчанию черный
    }
    
    // Простая реализация для некоторых символов
    switch (character) {
        case 'H':
            // Буква H
            for (int y = 2; y < 14; y++) {
                textureData[y * 16 + 2] = 255; // Левая вертикальная линия
                textureData[y * 16 + 13] = 255; // Правая вертикальная линия
            }
            for (int x = 4; x < 12; x++) {
                textureData[8 * 16 + x] = 255; // Горизонтальная линия
            }
            break;
        case 'e':
            // Буква e
            for (int x = 4; x < 12; x++) {
                textureData[4 * 16 + x] = 255; // Верхняя линия
                textureData[8 * 16 + x] = 255; // Средняя линия
                textureData[12 * 16 + x] = 255; // Нижняя линия
            }
            for (int y = 6; y < 12; y++) {
                textureData[y * 16 + 4] = 255; // Левая вертикальная линия
            }
            break;
        case 'l':
            // Буква l
            for (int y = 2; y < 14; y++) {
                textureData[y * 16 + 4] = 255; // Вертикальная линия
            }
            break;
        case 'o':
            // Буква o
            for (int x = 4; x < 12; x++) {
                textureData[4 * 16 + x] = 255; // Верхняя линия
                textureData[12 * 16 + x] = 255; // Нижняя линия
            }
            for (int y = 6; y < 12; y++) {
                textureData[y * 16 + 4] = 255; // Левая вертикальная линия
                textureData[y * 16 + 11] = 255; // Правая вертикальная линия
            }
            break;
        case 'W':
            // Буква W
            for (int y = 2; y < 14; y++) {
                textureData[y * 16 + 2] = 255; // Левая вертикальная линия
                textureData[y * 16 + 13] = 255; // Правая вертикальная линия
            }
            for (int i = 0; i < 6; i++) {
                textureData[(10 - i) * 16 + (4 + i)] = 255; // Диагональная линия
                textureData[(10 - i) * 16 + (11 - i)] = 255; // Диагональная линия
            }
            break;
        case 'r':
            // Буква r
            for (int y = 6; y < 12; y++) {
                textureData[y * 16 + 4] = 255; // Вертикальная линия
            }
            textureData[6 * 16 + 6] = 255;
            textureData[6 * 16 + 8] = 255;
            break;
        case 'd':
            // Буква d
            for (int y = 2; y < 14; y++) {
                textureData[y * 16 + 11] = 255; // Правая вертикальная линия
            }
            for (int x = 4; x < 11; x++) {
                textureData[4 * 16 + x] = 255; // Верхняя линия
                textureData[12 * 16 + x] = 255; // Нижняя линия
            }
            for (int y = 6; y < 12; y++) {
                textureData[y * 16 + 4] = 255; // Левая вертикальная линия
            }
            break;
        case '!':
            // Восклицательный знак
            for (int y = 2; y < 10; y++) {
                textureData[y * 16 + 6] = 255; // Вертикальная линия
            }
            textureData[12 * 16 + 6] = 255; // Точка
            break;
        case 'C':
            // Буква C
            for (int x = 4; x < 12; x++) {
                textureData[4 * 16 + x] = 255; // Верхняя линия
                textureData[12 * 16 + x] = 255; // Нижняя линия
            }
            for (int y = 6; y < 12; y++) {
                textureData[y * 16 + 4] = 255; // Левая вертикальная линия
            }
            break;
        case 'u':
            // Буква u
            for (int y = 6; y < 12; y++) {
                textureData[y * 16 + 4] = 255; // Левая вертикальная линия
                textureData[y * 16 + 11] = 255; // Правая вертикальная линия
            }
            for (int x = 4; x < 11; x++) {
                textureData[12 * 16 + x] = 255; // Нижняя линия
            }
            break;
        case 'b':
            // Буква b
            for (int y = 2; y < 14; y++) {
                textureData[y * 16 + 4] = 255; // Левая вертикальная линия
            }
            for (int x = 4; x < 11; x++) {
                textureData[4 * 16 + x] = 255; // Верхняя линия
                textureData[8 * 16 + x] = 255; // Средняя линия
                textureData[12 * 16 + x] = 255; // Нижняя линия
            }
            for (int y = 6; y < 8; y++) {
                textureData[y * 16 + 11] = 255; // Правая вертикальная линия
            }
            for (int y = 10; y < 12; y++) {
                textureData[y * 16 + 11] = 255; // Правая вертикальная линия
            }
            break;
        case 'a':
            // Буква a
            for (int x = 4; x < 11; x++) {
                textureData[4 * 16 + x] = 255; // Верхняя линия
                textureData[8 * 16 + x] = 255; // Средняя линия
                textureData[12 * 16 + x] = 255; // Нижняя линия
            }
            for (int y = 6; y < 12; y++) {
                textureData[y * 16 + 11] = 255; // Правая вертикальная линия
            }
            textureData[6 * 16 + 4] = 255; // Левая точка
            break;
        case 't':
            // Буква t
            for (int x = 4; x < 12; x++) {
                textureData[4 * 16 + x] = 255; // Верхняя линия
            }
            for (int y = 6; y < 12; y++) {
                textureData[y * 16 + 6] = 255; // Вертикальная линия
            }
            break;
        case 'i':
            // Буква i
            textureData[4 * 16 + 6] = 255; // Точка
            for (int y = 8; y < 14; y++) {
                textureData[y * 16 + 6] = 255; // Вертикальная линия
            }
            break;
        case 'm':
            // Буква m
            for (int y = 6; y < 12; y++) {
                textureData[y * 16 + 4] = 255; // Левая вертикальная линия
                textureData[y * 16 + 8] = 255; // Средняя вертикальная линия
                textureData[y * 16 + 12] = 255; // Правая вертикальная линия
            }
            for (int x = 4; x < 8; x++) {
                textureData[6 * 16 + x] = 255; // Верхняя линия
            }
            for (int x = 8; x < 12; x++) {
                textureData[6 * 16 + x] = 255; // Верхняя линия
            }
            break;
        case ' ':
            // Пробел - пустая текстура
            break;
        default:
            // Для неизвестных символов создаем простой квадрат
            for (int y = 2; y < 14; y++) {
                for (int x = 2; x < 14; x++) {
                    textureData[y * 16 + x] = 255;
                }
            }
            break;
    }
    
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 16, 16, 0, GL_RED, GL_UNSIGNED_BYTE, textureData);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    return textureID;
}

bool TextRenderer::InitializeFreeType() {
    if (ftInitialized) {
        return true;
    }
    
    // Инициализация FreeType
    if (FT_Init_FreeType(&ft)) {
        std::cerr << "ERROR::FREETYPE: Could not init FreeType Library" << std::endl;
        return false;
    }
    
    ftInitialized = true;
    return true;
}

void TextRenderer::CleanupFreeType() {
    if (face) {
        FT_Done_Face(face);
        face = nullptr;
    }
    if (ftInitialized && ft) {
        FT_Done_FreeType(ft);
        ft = nullptr;
        ftInitialized = false;
    }
}

void TextRenderer::RenderSimpleTextString(const std::string& text, int x, int y, float scale, const glm::vec3& color)
{
    if (!textShader) {
        std::cerr << "Text shader is not valid" << std::endl;
        return;
    }
    
    // Сохраняем состояние OpenGL
    GLboolean depthTestEnabled;
    glGetBooleanv(GL_DEPTH_TEST, &depthTestEnabled);
    GLboolean blendEnabled;
    glGetBooleanv(GL_BLEND, &blendEnabled);
    
    // Отключаем тест глубины для 2D рендеринга
    glDisable(GL_DEPTH_TEST);
    
    // Включаем blending для прозрачности
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Используем text шейдер
    glUseProgram(textShader);
    
    // Устанавливаем размер экрана
    GLint screenSizeLocation = glGetUniformLocation(textShader, "screenSize");
    if (screenSizeLocation != -1) {
        glUniform2f(screenSizeLocation, windowWidth, windowHeight);
    }
    
    // Устанавливаем цвет текста
    GLint textColorLocation = glGetUniformLocation(textShader, "textColor");
    if (textColorLocation != -1) {
        glUniform3f(textColorLocation, color.x, color.y, color.z);
    }
    
    // Размер символа в пикселях
    int charWidth = 12 * scale;
    int charHeight = 16 * scale;
    
    // Создаем VAO и VBO для символов
    GLuint charVAO, charVBO;
    glGenVertexArrays(1, &charVAO);
    glGenBuffers(1, &charVBO);
    
    glBindVertexArray(charVAO);
    glBindBuffer(GL_ARRAY_BUFFER, charVBO);
    
    // Настраиваем атрибуты
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    // Привязываем текстуру
    glActiveTexture(GL_TEXTURE0);
    GLint textTextureLocation = glGetUniformLocation(textShader, "textTexture");
    if (textTextureLocation != -1) {
        glUniform1i(textTextureLocation, 0);
    }
    
    // Отрисовываем каждый символ как простой прямоугольник
    int currentX = x;
    for (char c : text) {
        // Создаем текстуру для конкретного символа
        GLuint charTexture = CreateCharacterTexture(c);
        glBindTexture(GL_TEXTURE_2D, charTexture);
        
        // Создаем данные для прямоугольника символа
        float charRect[] = {
            // позиции (x, y)        // текстурные координаты (u, v)
            currentX, y,             0.0f, 0.0f,  // Левая верхняя точка
            currentX + charWidth, y, 1.0f, 0.0f,  // Правая верхняя точка
            currentX, y - charHeight, 0.0f, 1.0f, // Левая нижняя точка
            currentX + charWidth, y - charHeight, 1.0f, 1.0f  // Правая нижняя точка
        };
        
        // Обновляем VBO
        glBufferData(GL_ARRAY_BUFFER, sizeof(charRect), charRect, GL_STATIC_DRAW);
        
        // Рисуем символ
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        
        // Удаляем текстуру символа
        glDeleteTextures(1, &charTexture);
        
        // Сдвигаем позицию для следующего символа (пробелы меньше)
        if (c == ' ') {
            currentX += charWidth / 2;
        } else {
            currentX += charWidth;
        }
    }
    
    // Очищаем ресурсы
    glDeleteVertexArrays(1, &charVAO);
    glDeleteBuffers(1, &charVBO);
    
    // Отключаем шейдер
    glUseProgram(0);
    
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

void TextRenderer::RenderTextWithCharacters(const std::string& text, int x, int y, float scale, const glm::vec3& color)
{
    // Этот метод использует ту же логику, что и RenderSimpleTextString
    RenderSimpleTextString(text, x, y, scale, color);
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
    
    // Очистка FreeType
    CleanupFreeType();
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

uniform sampler2D text;
uniform vec3 textColor;

void main()
{    
    vec4 sampled = vec4(1.0, 1.0, 1.0, texture(text, TexCoords).r);
    color = vec4(textColor, 1.0) * sampled;
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
    if (!ftInitialized) {
        std::cerr << "FreeType not initialized" << std::endl;
        return false;
    }
    
    // Загрузка шрифта
    if (FT_New_Face(ft, fontPath.c_str(), 0, &face)) {
        std::cerr << "ERROR::FREETYPE: Failed to load font: " << fontPath << std::endl;
        return false;
    }
    
    // Установка размера шрифта
    FT_Set_Pixel_Sizes(face, 0, fontSize);
    
    // Отключение выравнивания байтов
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    
    // Загрузка первых 128 символов ASCII
    for (unsigned char c = 0; c < 128; c++) {
        // Загрузка глифа символа
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
            std::cerr << "ERROR::FREETYPE: Failed to load Glyph: " << c << std::endl;
            continue;
        }
        
        // Генерация текстуры
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        
        // Загрузка данных глифа в текстуру
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RED,
            face->glyph->bitmap.width,
            face->glyph->bitmap.rows,
            0,
            GL_RED,
            GL_UNSIGNED_BYTE,
            face->glyph->bitmap.buffer
        );
        
        // Настройка параметров текстуры
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        // Сохранение информации о символе
        Character character = {
            texture,
            glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
            glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
            static_cast<GLuint>(face->glyph->advance.x)
        };
        
        characters.insert(std::pair<char, Character>(c, character));
    }
    
    // Восстановление выравнивания байтов
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    
    std::cout << "Loaded " << characters.size() << " characters from font: " << fontPath << std::endl;
    return true;
}

void TextRenderer::RenderText(const std::string& text, float x, float y, float scale, const glm::vec3& color) {
    if (!textShader) {
        std::cerr << "TextRenderer: textShader is null!" << std::endl;
        return;
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
        // Проверяем, есть ли символ в нашей карте
        if (characters.find(*c) == characters.end()) {
            std::cerr << "Character not found: " << *c << std::endl;
            continue;
        }
        
        Character ch = characters[*c];

        float xpos = x + ch.bearing.x * scale;
        float ypos = y - (ch.size.y - ch.bearing.y) * scale;

        float w = ch.size.x * scale;
        float h = ch.size.y * scale;

        // Обновляем VBO для каждого символа
        float vertices[6][4] = {
            { xpos,     ypos + h,   0.0f, 0.0f },
            { xpos,     ypos,       0.0f, 1.0f },
            { xpos + w, ypos,       1.0f, 1.0f },

            { xpos,     ypos + h,   0.0f, 0.0f },
            { xpos + w, ypos,       1.0f, 1.0f },
            { xpos + w, ypos + h,   1.0f, 0.0f }
        };

        // Привязываем текстуру символа
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ch.textureID);
        
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Сдвигаем курсор для следующего символа (advance в 1/64 пикселя)
        x += (ch.advance >> 6) * scale;
    }
    glBindVertexArray(0);
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

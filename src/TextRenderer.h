#ifndef TEXTRENDERER_H
#define TEXTRENDERER_H

#include <string>
#include <glm/glm.hpp>
#include <GL/glew.h>
#include <map>
#include <memory>
#include <ft2build.h>
#include FT_FREETYPE_H

namespace cutum {

struct Character {
    GLuint textureID;   // ID текстуры символа
    glm::ivec2 size;    // Размер символа (ширина, высота)
    glm::ivec2 bearing; // Смещение от базовой линии (left, top)
    GLuint advance;     // Расстояние до следующего символа (в 1/64 пикселя)
};

class TextRenderer {
public:
    TextRenderer();
    ~TextRenderer();

    // Инициализация системы отображения текста
    bool Initialize(const std::string& fontPath, int fontSize);
    void Shutdown();

    // Отрисовка текста
    void RenderText(const std::string& text, float x, float y, float scale, const glm::vec3& color = glm::vec3(1.0f));
    
    // Отрисовка текста с центрированием
    void RenderTextCentered(const std::string& text, float y, float scale, const glm::vec3& color = glm::vec3(1.0f));
    
    // Получение размеров текста
    glm::vec2 GetTextSize(const std::string& text, float scale);
    
    // Установка размеров окна
    void SetWindowSize(int width, int height);
    
    // Новые методы для отображения текста с битовыми картами
    void RenderSimpleTextString(const std::string& text, int x, int y, float scale, const glm::vec3& color);
    void RenderTextWithCharacters(const std::string& text, int x, int y, float scale, const glm::vec3& color);
    
    // Методы для создания текстур символов
    GLuint CreateSimpleTextTexture();
    GLuint CreateCharacterTexture(char character);

private:
    GLuint textShader;
    GLuint VAO, VBO;
    std::map<GLchar, Character> characters;
    int windowWidth, windowHeight;
    
    // FreeType
    FT_Library ft;
    FT_Face face;
    bool ftInitialized;
    
    // Кэшированные uniform locations для производительности
    GLint textColorLocation;
    GLint projectionLocation;

    bool CreateShader();
    bool LoadCharacters(const std::string& fontPath, int fontSize);
    bool InitializeFreeType();
    void CleanupFreeType();
};

} // namespace cutum

#endif // TEXTRENDERER_H

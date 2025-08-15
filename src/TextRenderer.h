#ifndef TEXTRENDERER_H
#define TEXTRENDERER_H

#include <string>
#include <glm/glm.hpp>
#include <GL/glew.h>
#include <map>
#include <memory>

namespace cutum {

struct Character {
    GLuint textureID;   // ID текстуры символа
    glm::ivec2 size;    // Размер символа
    glm::ivec2 bearing; // Смещение от базовой линии
    GLuint advance;     // Расстояние до следующего символа
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
    
    // Кэшированные uniform locations для производительности
    GLint textColorLocation;
    GLint projectionLocation;

    bool CreateShader();
    bool LoadCharacters(const std::string& fontPath, int fontSize);
};

} // namespace cutum

#endif // TEXTRENDERER_H

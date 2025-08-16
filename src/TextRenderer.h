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
    GLuint textureID;   // Symbol texture ID
    glm::ivec2 size;    // Symbol size (width, height)
    glm::ivec2 bearing; // Offset from baseline (left, top)
    GLuint advance;     // Distance to next symbol (in 1/64 pixels)
};

class TextRenderer {
public:
    TextRenderer();
    ~TextRenderer();

    // Initialize text rendering system
    bool Initialize(const std::string& fontPath, int fontSize);
    void Shutdown();

    // Render text
    void RenderText(const std::string& text, float x, float y, float scale, const glm::vec3& color = glm::vec3(1.0f));
    
    // Render centered text
    void RenderTextCentered(const std::string& text, float y, float scale, const glm::vec3& color = glm::vec3(1.0f));
    
    // Get text size
    glm::vec2 GetTextSize(const std::string& text, float scale);
    
    // Set window size
    void SetWindowSize(int width, int height);
    
    // New methods for bitmap text rendering
    void RenderSimpleTextString(const std::string& text, int x, int y, float scale, const glm::vec3& color);
    void RenderTextWithCharacters(const std::string& text, int x, int y, float scale, const glm::vec3& color);
    
    // Methods for creating character textures
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
    
    // Cached uniform locations for performance
    GLint textColorLocation;
    GLint projectionLocation;

    bool CreateShader();
    bool LoadCharacters(const std::string& fontPath, int fontSize);
    bool InitializeFreeType();
    void CleanupFreeType();
};

} // namespace cutum

#endif // TEXTRENDERER_H

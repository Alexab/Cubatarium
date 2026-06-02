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

bool TextRenderer::Initialize(int fontSize) {
    // Find available font automatically
    std::string fontPath = FindAvailableFont();
    if (fontPath.empty()) {
        std::cerr << "No available fonts found" << std::endl;
        return false;
    }
    
    return Initialize(fontPath, fontSize);
}

bool TextRenderer::Initialize(const std::string& fontPath, int fontSize) {
    // Initialize FreeType
    if (!InitializeFreeType()) {
        std::cerr << "Failed to initialize FreeType" << std::endl;
        return false;
    }

    // Create shader
    if (!CreateShader()) {
        std::cerr << "Failed to create text shader" << std::endl;
        return false;
    }

    // Configure OpenGL for text rendering
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Create VAO and VBO for character rendering
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Load characters using FreeType
    if (!LoadCharacters(fontPath, fontSize)) {
        std::cerr << "Failed to load characters from: " << fontPath << std::endl;
        return false;
    }

    std::cout << "TextRenderer initialized successfully with font: " << fontPath << std::endl;
    return true;
}

bool TextRenderer::InitializeFreeType() {
    if (ftInitialized) {
        return true;
    }
    
    // FreeType initialization
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
    
    // Delete character textures
    for (auto& [c, ch] : characters) {
        glDeleteTextures(1, &ch.textureID);
    }
    characters.clear();
    
    // Cleanup FreeType
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

    // Vertex shader compilation check
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

    // Fragment shader compilation check
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

    // Program linking check
    glGetProgramiv(textShader, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(textShader, 512, NULL, infoLog);
        std::cerr << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
        return false;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Cache uniform locations
    textColorLocation = glGetUniformLocation(textShader, "textColor");
    projectionLocation = glGetUniformLocation(textShader, "projection");

    return textShader != 0;
}

bool TextRenderer::LoadCharacters(const std::string& fontPath, int fontSize) {
    if (!ftInitialized) {
        std::cerr << "FreeType not initialized" << std::endl;
        return false;
    }
    
    // Load font
    if (FT_New_Face(ft, fontPath.c_str(), 0, &face)) {
        std::cerr << "ERROR::FREETYPE: Failed to load font: " << fontPath << std::endl;
        return false;
    }
    
    // Set font size
    FT_Set_Pixel_Sizes(face, 0, fontSize);
    
    // Disable byte alignment
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    
    // Load first 128 ASCII characters
    for (unsigned char c = 0; c < 128; c++) {
        // Load character glyph
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
            std::cerr << "ERROR::FREETYPE: Failed to load Glyph: " << c << std::endl;
            continue;
        }
        
        // Generate texture
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        
        // Load glyph data into texture
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
        
        // Configure texture parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        // Save character information
        Character character = {
            texture,
            glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
            glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
            static_cast<GLuint>(face->glyph->advance.x)
        };
        
        characters.insert(std::pair<char, Character>(c, character));
    }
    
    // Restore byte alignment
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    
    std::cout << "Loaded " << characters.size() << " characters from font: " << fontPath << std::endl;
    return true;
}

void TextRenderer::RenderText(const std::string& text, float x, float y, float scale, const glm::vec3& color) {
    if (!textShader) {
        std::cerr << "TextRenderer: textShader is null!" << std::endl;
        return;
    }
    
    // Ensure blending is enabled for text
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glUseProgram(textShader);
    
    // Check uniform locations
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

    // Orthographic projection for 2D text
    glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(windowWidth), 0.0f, static_cast<float>(windowHeight));
    glUniformMatrix4fv(projectionLocation, 1, GL_FALSE, glm::value_ptr(projection));

    std::string::const_iterator c;
    for (c = text.begin(); c != text.end(); c++) {
        // Check if character exists in our map
        if (characters.find(*c) == characters.end()) {
            std::cerr << "Character not found: " << *c << std::endl;
            continue;
        }
        
        Character ch = characters[*c];

        float xpos = x + ch.bearing.x * scale;
        float ypos = y - (ch.size.y - ch.bearing.y) * scale;

        float w = ch.size.x * scale;
        float h = ch.size.y * scale;

        // Update VBO for each character
        float vertices[6][4] = {
            { xpos,     ypos + h,   0.0f, 0.0f },
            { xpos,     ypos,       0.0f, 1.0f },
            { xpos + w, ypos,       1.0f, 1.0f },

            { xpos,     ypos + h,   0.0f, 0.0f },
            { xpos + w, ypos,       1.0f, 1.0f },
            { xpos + w, ypos + h,   1.0f, 0.0f }
        };

        // Bind character texture
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ch.textureID);
        
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Move cursor for next character (advance in 1/64 pixels)
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

std::string TextRenderer::FindAvailableFont() {
    // First try to find fonts in local fonts directory
    std::vector<std::string> localFonts = ScanFontsDirectory();
    for (const auto& font : localFonts) {
        if (std::filesystem::exists(font)) {
            return font;
        }
    }
    
    // Fallback to system font
    return GetSystemFontPath();
}

std::vector<std::string> TextRenderer::ScanFontsDirectory() {
    std::vector<std::string> fonts;
    std::string fontsDir = "fonts";
    
    if (!std::filesystem::exists(fontsDir)) {
        return fonts;
    }
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(fontsDir)) {
            if (entry.is_regular_file()) {
                std::string extension = entry.path().extension().string();
                // Check for common font extensions
                if (extension == ".ttf" || extension == ".TTF" || 
                    extension == ".otf" || extension == ".OTF") {
                    fonts.push_back(entry.path().string());
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error scanning fonts directory: " << e.what() << std::endl;
    }
    
    return fonts;
}

std::string TextRenderer::GetSystemFontPath() {
#ifdef _WIN32
    return "C:/Windows/Fonts/arial.ttf";
#elif defined(__linux__)
    return "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
#elif defined(__APPLE__)
    return "/System/Library/Fonts/Arial.ttf";
#else
    return "fonts/arial.ttf"; // Default fallback
#endif
}

} // namespace cutum

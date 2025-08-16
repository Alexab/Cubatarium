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
        std::cerr << "Failed to load characters" << std::endl;
        return false;
    }

    return true;
}

GLuint TextRenderer::CreateSimpleTextTexture()
{
    // Create a simple 16x16 pixel texture for the character
// This will be a simple white square with a black border
    unsigned char textureData[16 * 16] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // Top row - black
        0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Second row - white with black edges
        0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Third row - white with black edges
0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Fourth row - white with black edges
0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Fifth row - white with black edges
0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Sixth row - white with black edges
0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Seventh row - white with black edges
0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Eighth row - white with black edges
0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Ninth row - white with black edges
0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Tenth row - white with black edges
0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Eleventh row - white with black edges
0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Twelfth row - white with black edges
0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Thirteenth row - white with black edges
0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Fourteenth row - white with black edges
0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0,  // Fifteenth row - white with black edges
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0   // Bottom row - black
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
    // Create 16x16 pixel texture for the character
    unsigned char textureData[16 * 16];
    
    // Fill texture based on character
for (int i = 0; i < 16 * 16; i++) {
    textureData[i] = 0; // Default black
}
    
    // Simple implementation for some characters
    switch (character) {
        case 'H':
    // Letter H
    for (int y = 2; y < 14; y++) {
        textureData[y * 16 + 2] = 255; // Left vertical line
        textureData[y * 16 + 13] = 255; // Right vertical line
    }
    for (int x = 4; x < 12; x++) {
        textureData[8 * 16 + x] = 255; // Horizontal line
    }
    break;
        case 'e':
    // Letter e
    for (int x = 4; x < 12; x++) {
        textureData[4 * 16 + x] = 255; // Top line
        textureData[8 * 16 + x] = 255; // Middle line
        textureData[12 * 16 + x] = 255; // Bottom line
    }
    for (int y = 6; y < 12; y++) {
        textureData[y * 16 + 4] = 255; // Left vertical line
    }
    break;
        case 'l':
    // Letter l
    for (int y = 2; y < 14; y++) {
        textureData[y * 16 + 4] = 255; // Vertical line
    }
    break;
case 'o':
    // Letter o
    for (int x = 4; x < 12; x++) {
        textureData[4 * 16 + x] = 255; // Top line
        textureData[12 * 16 + x] = 255; // Bottom line
    }
    for (int y = 6; y < 12; y++) {
        textureData[y * 16 + 4] = 255; // Left vertical line
        textureData[y * 16 + 11] = 255; // Right vertical line
    }
    break;
        case 'W':
    // Letter W
    for (int y = 2; y < 14; y++) {
        textureData[y * 16 + 2] = 255; // Left vertical line
        textureData[y * 16 + 13] = 255; // Right vertical line
    }
    for (int i = 0; i < 6; i++) {
        textureData[(10 - i) * 16 + (4 + i)] = 255; // Diagonal line
        textureData[(10 - i) * 16 + (11 - i)] = 255; // Diagonal line
    }
    break;
        case 'r':
    // Letter r
    for (int y = 6; y < 12; y++) {
        textureData[y * 16 + 4] = 255; // Vertical line
    }
    textureData[6 * 16 + 6] = 255;
    textureData[6 * 16 + 8] = 255;
    break;
case 'd':
    // Letter d
    for (int y = 2; y < 14; y++) {
        textureData[y * 16 + 11] = 255; // Right vertical line
    }
    for (int x = 4; x < 11; x++) {
        textureData[4 * 16 + x] = 255; // Top line
        textureData[12 * 16 + x] = 255; // Bottom line
    }
    for (int y = 6; y < 12; y++) {
        textureData[y * 16 + 4] = 255; // Left vertical line
    }
    break;
        case '!':
    // Exclamation mark
    for (int y = 2; y < 10; y++) {
        textureData[y * 16 + 6] = 255; // Vertical line
    }
    textureData[12 * 16 + 6] = 255; // Dot
    break;
case 'C':
    // Letter C
    for (int x = 4; x < 12; x++) {
        textureData[4 * 16 + x] = 255; // Top line
        textureData[12 * 16 + x] = 255; // Bottom line
    }
    for (int y = 6; y < 12; y++) {
        textureData[y * 16 + 4] = 255; // Left vertical line
    }
    break;
        case 'u':
    // Letter u
    for (int y = 6; y < 12; y++) {
        textureData[y * 16 + 4] = 255; // Left vertical line
        textureData[y * 16 + 11] = 255; // Right vertical line
    }
    for (int x = 4; x < 11; x++) {
        textureData[12 * 16 + x] = 255; // Bottom line
    }
    break;
case 'b':
    // Letter b
    for (int y = 2; y < 14; y++) {
        textureData[y * 16 + 4] = 255; // Left vertical line
    }
    for (int x = 4; x < 11; x++) {
        textureData[4 * 16 + x] = 255; // Top line
        textureData[8 * 16 + x] = 255; // Middle line
        textureData[12 * 16 + x] = 255; // Bottom line
    }
    for (int y = 6; y < 8; y++) {
        textureData[y * 16 + 11] = 255; // Right vertical line
    }
    for (int y = 10; y < 12; y++) {
        textureData[y * 16 + 11] = 255; // Right vertical line
    }
    break;
        case 'a':
    // Letter a
    for (int x = 4; x < 11; x++) {
        textureData[4 * 16 + x] = 255; // Top line
        textureData[8 * 16 + x] = 255; // Middle line
        textureData[12 * 16 + x] = 255; // Bottom line
    }
    for (int y = 6; y < 12; y++) {
        textureData[y * 16 + 11] = 255; // Right vertical line
    }
    textureData[6 * 16 + 4] = 255; // Left dot
    break;
case 't':
    // Letter t
    for (int x = 4; x < 12; x++) {
        textureData[4 * 16 + x] = 255; // Top line
    }
    for (int y = 6; y < 12; y++) {
        textureData[y * 16 + 6] = 255; // Vertical line
    }
    break;
        case 'i':
    // Letter i
    textureData[4 * 16 + 6] = 255; // Dot
    for (int y = 8; y < 14; y++) {
        textureData[y * 16 + 6] = 255; // Vertical line
    }
    break;
case 'm':
    // Letter m
    for (int y = 6; y < 12; y++) {
        textureData[y * 16 + 4] = 255; // Left vertical line
        textureData[y * 16 + 8] = 255; // Middle vertical line
        textureData[y * 16 + 12] = 255; // Right vertical line
    }
    for (int x = 4; x < 8; x++) {
        textureData[6 * 16 + x] = 255; // Top line
    }
    for (int x = 8; x < 12; x++) {
        textureData[6 * 16 + x] = 255; // Top line
    }
    break;
        case ' ':
    // Space - empty texture
    break;
default:
    // For unknown characters create a simple square
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

void TextRenderer::RenderSimpleTextString(const std::string& text, int x, int y, float scale, const glm::vec3& color)
{
    if (!textShader) {
        std::cerr << "Text shader is not valid" << std::endl;
        return;
    }
    
    // Save OpenGL state
    GLboolean depthTestEnabled;
    glGetBooleanv(GL_DEPTH_TEST, &depthTestEnabled);
    GLboolean blendEnabled;
    glGetBooleanv(GL_BLEND, &blendEnabled);
    
    // Disable depth test for 2D rendering
    glDisable(GL_DEPTH_TEST);
    
    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Use text shader
    glUseProgram(textShader);
    
    // Set screen size
    GLint screenSizeLocation = glGetUniformLocation(textShader, "screenSize");
    if (screenSizeLocation != -1) {
        glUniform2f(screenSizeLocation, windowWidth, windowHeight);
    }
    
    // Set text color
    GLint textColorLocation = glGetUniformLocation(textShader, "textColor");
    if (textColorLocation != -1) {
        glUniform3f(textColorLocation, color.x, color.y, color.z);
    }
    
    // Character size in pixels
    int charWidth = 12 * scale;
    int charHeight = 16 * scale;
    
    // Create VAO and VBO for characters
    GLuint charVAO, charVBO;
    glGenVertexArrays(1, &charVAO);
    glGenBuffers(1, &charVBO);
    
    glBindVertexArray(charVAO);
    glBindBuffer(GL_ARRAY_BUFFER, charVBO);
    
    // Configure attributes
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    // Bind texture
    glActiveTexture(GL_TEXTURE0);
    GLint textTextureLocation = glGetUniformLocation(textShader, "textTexture");
    if (textTextureLocation != -1) {
        glUniform1i(textTextureLocation, 0);
    }
    
    // Render each character as a simple rectangle
    int currentX = x;
    for (char c : text) {
        // Create texture for specific character
        GLuint charTexture = CreateCharacterTexture(c);
        glBindTexture(GL_TEXTURE_2D, charTexture);
        
        // Create data for character rectangle
float charRect[] = {
    // positions (x, y)      // texture coordinates (u, v)
    currentX, y,             0.0f, 0.0f,  // Top left point
    currentX + charWidth, y, 1.0f, 0.0f,  // Top right point
    currentX, y - charHeight, 0.0f, 1.0f, // Bottom left point
    currentX + charWidth, y - charHeight, 1.0f, 1.0f  // Bottom right point
};
        
        // Update VBO
        glBufferData(GL_ARRAY_BUFFER, sizeof(charRect), charRect, GL_STATIC_DRAW);
        
        // Draw character
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        
        // Delete character texture
        glDeleteTextures(1, &charTexture);
        
        // Move position for next character (spaces are smaller)
        if (c == ' ') {
            currentX += charWidth / 2;
        } else {
            currentX += charWidth;
        }
    }
    
    // Clean up resources
    glDeleteVertexArrays(1, &charVAO);
    glDeleteBuffers(1, &charVBO);
    
    // Disable shader
    glUseProgram(0);
    
    // Restore OpenGL state
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
    // This method uses the same logic as RenderSimpleTextString
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

} // namespace cutum

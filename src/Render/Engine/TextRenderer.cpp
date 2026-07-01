#include "Render/Engine/TextRenderer.h"

#include "App/Platform/GameAssets.h"
#include "App/Platform/IUPlatformPaths.h"
#include "App/Platform/Log.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

namespace cutum
{

namespace
{

#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
constexpr GLint kGlyphInternalFormat = GL_R8;
#else
constexpr GLint kGlyphInternalFormat = GL_RED;
#endif

void ConfigureGlyphTextureParameters()
{
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
#endif
}

} // namespace

UTextRenderer::UTextRenderer()
    : TextShader(0), VAO(0), VBO(0), WindowWidth(1280), WindowHeight(720),
      ft(nullptr), face(nullptr), ftInitialized(false), textColorLocation(-1),
      projectionLocation(-1), textSamplerLocation(-1)
{
}

UTextRenderer::~UTextRenderer() { Shutdown(); }

bool UTextRenderer::Initialize(int fontSize)
{
  std::string fontPath = FindAvailableFont();
  if (fontPath.empty())
  {
    CubatariumLogError("Text", "No available fonts found");
    return false;
  }

  CubatariumLogInfo("Text", "Using font: " + fontPath);
  return Initialize(fontPath, fontSize);
}

bool UTextRenderer::Initialize(const std::string &fontPath, int fontSize)
{
  if (!InitializeFreeType())
  {
    CubatariumLogError("Text", "Failed to initialize FreeType");
    return false;
  }

  if (!CreateShader())
  {
    CubatariumLogError("Text", "Failed to create text shader");
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
  if (!LoadCharacters(fontPath, fontSize))
  {
    CubatariumLogError("Text", "Failed to load characters from: " + fontPath);
    return false;
  }

  std::cout << "TextRenderer initialized successfully with font: " << fontPath
            << std::endl;
  return true;
}

bool UTextRenderer::InitializeFreeType()
{
  if (ftInitialized)
  {
    return true;
  }

  // FreeType initialization
  if (FT_Init_FreeType(&ft))
  {
    std::cerr << "ERROR::FREETYPE: Could not init FreeType Library"
              << std::endl;
    return false;
  }

  ftInitialized = true;
  return true;
}

void UTextRenderer::CleanupFreeType()
{
  if (face)
  {
    FT_Done_Face(face);
    face = nullptr;
  }
  if (ftInitialized && ft)
  {
    FT_Done_FreeType(ft);
    ft = nullptr;
    ftInitialized = false;
  }
}

void UTextRenderer::Shutdown()
{
  if (VAO)
  {
    glDeleteVertexArrays(1, &VAO);
    VAO = 0;
  }
  if (VBO)
  {
    glDeleteBuffers(1, &VBO);
    VBO = 0;
  }
  if (TextShader)
  {
    glDeleteProgram(TextShader);
    TextShader = 0;
  }

  // Delete character textures
  for (auto &[c, ch] : characters)
  {
    if (ch.textureID != 0)
    {
      glDeleteTextures(1, &ch.textureID);
    }
  }
  characters.clear();

  // Cleanup FreeType
  CleanupFreeType();
}

bool UTextRenderer::CreateShader()
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  const char *vertexShaderSource = R"(#version 300 es
layout(location = 0) in vec4 vertex;
out vec2 TexCoords;
uniform mat4 projection;
void main()
{
    gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);
    TexCoords = vertex.zw;
}
)";

  const char *fragmentShaderSource = R"(#version 300 es
precision mediump float;
in vec2 TexCoords;
out vec4 color;
uniform sampler2D text;
uniform vec3 textColor;
void main()
{
    float alpha = texture(text, TexCoords).r;
    color = vec4(textColor, alpha);
}
)";
#else
  const char *vertexShaderSource = R"(#version 330 core
layout (location = 0) in vec4 vertex;
out vec2 TexCoords;

uniform mat4 projection;

void main()
{
    gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);
    TexCoords = vertex.zw;
}
)";

  const char *fragmentShaderSource = R"(#version 330 core
out vec4 color;

in vec2 TexCoords;

uniform sampler2D text;
uniform vec3 textColor;

void main()
{    
    float alpha = texture(text, TexCoords).r;
    color = vec4(textColor, alpha);
}
)";
#endif

  GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
  glCompileShader(vertexShader);

  // Vertex shader compilation check
  GLint success;
  GLchar infoLog[512];
  glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
  if (!success)
  {
    glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
    CubatariumLogError("Text", std::string("Vertex shader: ") + infoLog);
    return false;
  }

  GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
  glCompileShader(fragmentShader);

  // Fragment shader compilation check
  glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
  if (!success)
  {
    glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
    CubatariumLogError("Text", std::string("Fragment shader: ") + infoLog);
    return false;
  }

  TextShader = glCreateProgram();
  glAttachShader(TextShader, vertexShader);
  glAttachShader(TextShader, fragmentShader);
  glLinkProgram(TextShader);

  // Program linking check
  glGetProgramiv(TextShader, GL_LINK_STATUS, &success);
  if (!success)
  {
    glGetProgramInfoLog(TextShader, 512, NULL, infoLog);
    CubatariumLogError("Text", std::string("Program link: ") + infoLog);
    return false;
  }

  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  // Cache uniform locations
  textColorLocation = glGetUniformLocation(TextShader, "textColor");
  projectionLocation = glGetUniformLocation(TextShader, "projection");
  textSamplerLocation = glGetUniformLocation(TextShader, "text");

  return TextShader != 0;
}

bool UTextRenderer::LoadCharacters(const std::string &fontPath, int fontSize)
{
  if (!ftInitialized)
  {
    std::cerr << "FreeType not initialized" << std::endl;
    return false;
  }

  // Load font
  if (FT_New_Face(ft, fontPath.c_str(), 0, &face))
  {
    std::cerr << "ERROR::FREETYPE: Failed to load font: " << fontPath
              << std::endl;
    return false;
  }

  // Set font size
  FT_Set_Pixel_Sizes(face, 0, fontSize);

  // Disable byte alignment
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  int texturedGlyphs = 0;
  bool loggedGlError = false;

  // Load first 128 ASCII characters
  for (unsigned char c = 0; c < 128; c++)
  {
    // Load character glyph
    if (FT_Load_Char(face, c, FT_LOAD_RENDER))
    {
      CubatariumLogError("Text",
                         std::string("Failed to load glyph: ") +
                             std::to_string(static_cast<unsigned int>(c)));
      continue;
    }

    const auto bitmapWidth = static_cast<int>(face->glyph->bitmap.width);
    const auto bitmapRows = static_cast<int>(face->glyph->bitmap.rows);

    GLuint texture = 0;
    if (bitmapWidth > 0 && bitmapRows > 0)
    {
      glGenTextures(1, &texture);
      glBindTexture(GL_TEXTURE_2D, texture);

      glTexImage2D(GL_TEXTURE_2D, 0, kGlyphInternalFormat, bitmapWidth,
                   bitmapRows, 0, GL_RED, GL_UNSIGNED_BYTE,
                   face->glyph->bitmap.buffer);
      ConfigureGlyphTextureParameters();

      if (!loggedGlError)
      {
        const GLenum glError = glGetError();
        if (glError != GL_NO_ERROR)
        {
          CubatariumLogError(
              "Text", "glTexImage2D failed with GL error " +
                          std::to_string(static_cast<unsigned int>(glError)));
          loggedGlError = true;
        }
      }
      ++texturedGlyphs;
    }

    Character character = {texture,
                           glm::ivec2(bitmapWidth, bitmapRows),
                           glm::ivec2(face->glyph->bitmap_left,
                                      face->glyph->bitmap_top),
                           static_cast<GLuint>(face->glyph->advance.x)};

    characters.insert(std::pair<char, Character>(c, character));
  }

  // Restore byte alignment
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

  CubatariumLogInfo("Text",
                    "Loaded " + std::to_string(characters.size()) +
                        " glyphs (" + std::to_string(texturedGlyphs) +
                        " textured) from font: " + fontPath);

  if (texturedGlyphs == 0)
  {
    CubatariumLogError("Text", "No textured glyphs loaded from font");
    return false;
  }

  return true;
}

void UTextRenderer::RenderText(const std::string &text, float x, float y,
                               float scale, const glm::vec3 &color)
{
  if (!TextShader)
  {
    std::cerr << "TextRenderer: TextShader is null!" << std::endl;
    return;
  }

  // Ensure blending is enabled for text
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glUseProgram(TextShader);

  // Check uniform locations
  if (textColorLocation == -1)
  {
    std::cerr << "TextRenderer: textColorLocation is invalid!" << std::endl;
    return;
  }
  if (projectionLocation == -1)
  {
    std::cerr << "TextRenderer: projectionLocation is invalid!" << std::endl;
    return;
  }
  if (textSamplerLocation == -1)
  {
    CubatariumLogError("Text", "text sampler uniform location is invalid");
    return;
  }

  glUniform1i(textSamplerLocation, 0);
  glUniform3f(textColorLocation, color.x, color.y, color.z);
  glBindVertexArray(VAO);

  // Orthographic projection for 2D text
  glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(WindowWidth), 0.0f,
                                    static_cast<float>(WindowHeight));
  glUniformMatrix4fv(projectionLocation, 1, GL_FALSE,
                     glm::value_ptr(projection));

  std::string::const_iterator c;
  for (c = text.begin(); c != text.end(); c++)
  {
    // Check if character exists in our map
    if (characters.find(*c) == characters.end())
    {
      std::cerr << "Character not found: " << *c << std::endl;
      continue;
    }

    Character ch = characters[*c];

    if (ch.textureID == 0)
    {
      x += (ch.advance >> 6) * scale;
      continue;
    }

    float xpos = x + ch.bearing.x * scale;
    float ypos = y - (ch.size.y - ch.bearing.y) * scale;

    float w = ch.size.x * scale;
    float h = ch.size.y * scale;

    // Update VBO for each character
    float vertices[6][4] = {
        {xpos, ypos + h, 0.0f, 0.0f},    {xpos, ypos, 0.0f, 1.0f},
        {xpos + w, ypos, 1.0f, 1.0f},

        {xpos, ypos + h, 0.0f, 0.0f},    {xpos + w, ypos, 1.0f, 1.0f},
        {xpos + w, ypos + h, 1.0f, 0.0f}};

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

void UTextRenderer::RenderTextCentered(const std::string &text, float y,
                                       float scale, const glm::vec3 &color)
{
  glm::vec2 textSize = GetTextSize(text, scale);
  float x = (WindowWidth - textSize.x) / 2.0f;
  RenderText(text, x, y, scale, color);
}

glm::vec2 UTextRenderer::GetTextSize(const std::string &text, float scale)
{
  float width = 0.0f;
  float height = 0.0f;

  std::string::const_iterator c;
  for (c = text.begin(); c != text.end(); c++)
  {
    const auto it = characters.find(*c);
    if (it == characters.end())
    {
      continue;
    }
    const Character &ch = it->second;
    width += (ch.advance >> 6) * scale;
    height = std::max(height, static_cast<float>(ch.size.y) * scale);
  }

  return glm::vec2(width, height);
}

void UTextRenderer::SetWindowSize(int width, int height)
{
  WindowWidth = width;
  WindowHeight = height;
}

std::string UTextRenderer::FindAvailableFont()
{
  if (auto *paths = IUPlatformPaths::TryGet())
  {
    const auto preferred =
        paths->AssetRoot() / "fonts" / kBundledUiFontFileName;
    if (std::filesystem::exists(preferred))
    {
      return preferred.string();
    }

    const auto fontsDir = paths->AssetRoot() / "fonts";
    if (std::filesystem::exists(fontsDir))
    {
      try
      {
        for (const auto &entry : std::filesystem::directory_iterator(fontsDir))
        {
          if (!entry.is_regular_file())
          {
            continue;
          }
          const auto ext = entry.path().extension().string();
          if (ext == ".ttf" || ext == ".TTF" || ext == ".otf" || ext == ".OTF")
          {
            return entry.path().string();
          }
        }
      }
      catch (const std::exception &e)
      {
        CubatariumLogError("Text",
                           std::string("Font scan failed: ") + e.what());
      }
    }
  }

  std::vector<std::string> localFonts = ScanFontsDirectory();
  for (const auto &font : localFonts)
  {
    if (std::filesystem::exists(font))
    {
      return font;
    }
  }

  return GetSystemFontPath();
}

std::vector<std::string> UTextRenderer::ScanFontsDirectory()
{
  std::vector<std::string> fonts;
  std::string fontsDir = "fonts";

  if (!std::filesystem::exists(fontsDir))
  {
    return fonts;
  }

  try
  {
    for (const auto &entry : std::filesystem::directory_iterator(fontsDir))
    {
      if (entry.is_regular_file())
      {
        std::string extension = entry.path().extension().string();
        // Check for common font extensions
        if (extension == ".ttf" || extension == ".TTF" || extension == ".otf" ||
            extension == ".OTF")
        {
          fonts.push_back(entry.path().string());
        }
      }
    }
  }
  catch (const std::exception &e)
  {
    std::cerr << "Error scanning fonts directory: " << e.what() << std::endl;
  }

  return fonts;
}

std::string UTextRenderer::GetSystemFontPath()
{
#if defined(__linux__) && !defined(__ANDROID__)
  const char *linuxOpen = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
  if (std::filesystem::exists(linuxOpen))
  {
    return linuxOpen;
  }
#endif
  return kBundledUiFontRelPath;
}

} // namespace cutum

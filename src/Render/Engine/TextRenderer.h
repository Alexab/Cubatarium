#ifndef TEXTRENDERER_H
#define TEXTRENDERER_H

#include "Render/GlIncludes.h"
#include <filesystem>
#include <ft2build.h>
#include <glm/glm.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include FT_FREETYPE_H

namespace cutum
{

struct Character
{
  GLuint textureID;   // Symbol texture ID
  glm::ivec2 size;    // Symbol size (width, height)
  glm::ivec2 bearing; // Offset from baseline (left, top)
  GLuint advance;     // Distance to next symbol (in 1/64 pixels)
};

class UTextRenderer
{
public:
  UTextRenderer();
  ~UTextRenderer();

  // Initialize text rendering system
  bool Initialize(int fontSize = 16);
  bool Initialize(const std::string &fontPath, int fontSize);
  void Shutdown();

  // Render text
  void RenderText(const std::string &text, float x, float y, float scale,
                  const glm::vec3 &color = glm::vec3(1.0f));

  // Render centered text
  void RenderTextCentered(const std::string &text, float y, float scale,
                          const glm::vec3 &color = glm::vec3(1.0f));

  // Get text size
  glm::vec2 GetTextSize(const std::string &text, float scale);

  // Set window size
  void SetWindowSize(int width, int height);

private:
  GLuint TextShader;
  GLuint VAO, VBO;
  std::map<GLchar, Character> characters;
  int WindowWidth, WindowHeight;

  // FreeType
  FT_Library ft;
  FT_Face face;
  bool ftInitialized;

  // Cached uniform locations for performance
  GLint textColorLocation;
  GLint projectionLocation;

  bool CreateShader();
  bool LoadCharacters(const std::string &fontPath, int fontSize);
  bool InitializeFreeType();
  void CleanupFreeType();

  // Font management methods
  std::string FindAvailableFont();
  std::vector<std::string> ScanFontsDirectory();
  std::string GetSystemFontPath();
};

} // namespace cutum

#endif // TEXTRENDERER_H

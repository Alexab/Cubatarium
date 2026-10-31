#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>

namespace
{

namespace fs = std::filesystem;

int gFailures = 0;

void Require(bool condition, const std::string &message)
{
  if (!condition)
  {
    std::cerr << "FAIL " << message << '\n';
    ++gFailures;
  }
}

std::string ReadShader(const fs::path &path)
{
  std::ifstream file(path);
  if (!file.is_open())
  {
    ++gFailures;
    std::cerr << "FAIL cannot open " << path.string() << '\n';
    return {};
  }
  return std::string((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
}

std::string Normalize(const std::string &text)
{
  std::regex whitespace("\\s+");
  return std::regex_replace(text, whitespace, " ");
}

std::string LastAssignmentOf(const std::string &source,
                             const std::string &name)
{
  const std::regex re(name + "\\s*=[^;]+;");
  std::sregex_iterator it(source.begin(), source.end(), re);
  std::sregex_iterator end;
  std::string last;
  while (it != end)
  {
    last = it->str();
    ++it;
  }
  return Normalize(last);
}

void CheckSkinnedVertexShader()
{
  const std::string variants[] = {"shaders/vshader_creature_skinned.glsl",
                                  "shaders/gles/vshader_creature_skinned.glsl"};
  for (const std::string &rel : variants)
  {
    const std::string src = ReadShader(fs::path(CUBATARIUM_SOURCE_DIR) / rel);
    Require(src.find("layout(std140) uniform BonePalette") != std::string::npos,
            rel + ": expected std140 uniform block 'BonePalette' (the world "
                  "draw pass uploads the palette only through this UBO)");
    const std::regex bones("mat4\\s+uBones\\[(\\d+)\\]");
    std::smatch match;
    const bool hasBones =
        std::regex_search(src, match, bones) && match.size() > 1;
    Require(hasBones && match[1].str() == "96",
            rel + ": expected 'mat4 uBones[96]' inside the block (palette cap "
                  "is 96, hermitcrab needs 67)");
    Require(src.find("uniform mat4 uBones[") == std::string::npos,
            rel + ": plain 'uniform mat4 uBones[...]' array is not uploaded "
                  "by any caller and silently renders zero matrices");
  }
}

void CheckFragmentShaderTint(const std::string &shaderName)
{
  const std::string desktop =
      ReadShader(fs::path(CUBATARIUM_SOURCE_DIR) / "shaders" / shaderName);
  const std::string gles = ReadShader(fs::path(CUBATARIUM_SOURCE_DIR) /
                                      "shaders" / "gles" / shaderName);
  const std::string desktopOut = LastAssignmentOf(desktop, "FragColor");
  const std::string glesOut = LastAssignmentOf(gles, "FragColor");
  Require(!desktopOut.empty() && desktopOut == glesOut,
          shaderName + ": desktop and GLES final FragColor must match ("
          "desktop='" + desktopOut + "' gles='" + glesOut + "')");
  Require(gles.find("uniform vec4 uTint") != std::string::npos,
          shaderName + " (GLES): missing uTint declaration");
}

} // namespace

int main()
{
  CheckSkinnedVertexShader();
  CheckFragmentShaderTint("fshader_creature.glsl");
  CheckFragmentShaderTint("fshader_creature_skinned.glsl");
  if (gFailures != 0)
  {
    std::cerr << gFailures << " shader parity failure(s)\n";
    return 1;
  }
  std::cout << "creature shader parity OK\n";
  return 0;
}

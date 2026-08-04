#ifndef FP_VIEWMODEL_RENDERER_H
#define FP_VIEWMODEL_RENDERER_H

#include <memory>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>

typedef unsigned int GLuint;

namespace cutum
{

class UItemDefinitionStorage;
class UShaderManager;
class UGuiRenderer;
struct GuiTheme;
struct InventoryEntryRef;

/// Minecraft/Luanti-style first-person box arms + held tool (FBO → UI blit).
/// Not a full skinned FP mesh (see TD-ITEM-004).
class UFpViewmodelRenderer
{
public:
  UFpViewmodelRenderer(std::shared_ptr<UItemDefinitionStorage> items,
                       std::shared_ptr<UShaderManager> shaderManager);
  ~UFpViewmodelRenderer();

  bool Initialize();
  void Shutdown();

  /// Renders 3D arms (+ tool if held) and blits into the lower-right corner.
  void DrawOverlay(UGuiRenderer &renderer, const GuiTheme &theme,
                   const InventoryEntryRef *active, int framebuffer_w,
                   int framebuffer_h);

private:
  struct Part
  {
    float ox{0.f};
    float oy{0.f};
    float oz{0.f};
    float sx{1.f};
    float sy{1.f};
    float sz{1.f};
    unsigned char r{180};
    unsigned char g{140};
    unsigned char b{110};
  };

  bool InitCubeMesh();
  bool EnsureFbo(int size);
  GLuint SolidTex(unsigned char r, unsigned char g, unsigned char b);
  void DrawParts(const std::vector<Part> &parts, const glm::mat4 &mvpBase);
  std::vector<Part> ArmParts() const;
  std::vector<Part> ToolParts(const std::string &itemId) const;
  GLuint RenderFrame(const InventoryEntryRef *active, int size);

  std::shared_ptr<UItemDefinitionStorage> Items;
  std::shared_ptr<UShaderManager> ShaderManager;
  std::shared_ptr<class UShaderProgram> Shader;

  GLuint CubeVao{0};
  GLuint CubeVbo{0};
  GLuint CubeEbo{0};
  GLuint Fbo{0};
  GLuint ColorTex{0};
  GLuint DepthRbo{0};
  int FboSize{0};
  GLuint SkinTex{0};
  GLuint SleeveTex{0};
  GLuint MetalTex{0};
  GLuint WoodTex{0};
};

} // namespace cutum

#endif

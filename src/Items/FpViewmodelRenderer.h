#ifndef FP_VIEWMODEL_RENDERER_H
#define FP_VIEWMODEL_RENDERER_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

typedef unsigned int GLuint;

namespace cutum
{

class UItemDefinitionStorage;
class UBlockDefinitionStorage;
class UTextureCubeStorage;
class UShaderManager;
struct InventoryEntryRef;

enum class FpSwingKind : uint8_t
{
  Dig = 0,
  Place = 1,
  Melee = 2
};

struct FpViewmodelDrawParams
{
  int FramebufferW{0};
  int FramebufferH{0};
  const InventoryEntryRef *Active{nullptr};
  const InventoryEntryRef *Offhand{nullptr};
};

/// Luanti-style clear-Z FP viewmodel + Minecraft dual arms (box mesh).
/// Not body-in-FP; skinned arms remain TD-ITEM-004.
class UFpViewmodelRenderer
{
public:
  UFpViewmodelRenderer(std::shared_ptr<UItemDefinitionStorage> items,
                       std::shared_ptr<UBlockDefinitionStorage> blocks,
                       std::shared_ptr<UTextureCubeStorage> textures,
                       std::shared_ptr<UShaderManager> shaderManager);
  ~UFpViewmodelRenderer();

  bool Initialize();
  void Shutdown();

  void Update(float dt, float cameraYawDeg, float cameraPitchDeg,
              float moveSpeedHint);
  void NotifySwing(FpSwingKind kind);

  /// AFTER Geometry::Paint, BEFORE HUD. Default framebuffer.
  void DrawWorldOverlay(const FpViewmodelDrawParams &params);

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

  struct FpMotion
  {
    float BobPhase{0.f};
    float DigAnim{-1.f};
    int DigButton{-1};
    float WieldOffsetX{0.f};
    float WieldOffsetY{0.f};
    float LastYaw{0.f};
    float LastPitch{0.f};
    bool HaveLastAngles{false};
  };

  bool InitCubeMesh();
  GLuint SolidTex(unsigned char r, unsigned char g, unsigned char b);
  void DrawParts(const std::vector<Part> &parts, const glm::mat4 &mvpBase);
  std::vector<Part> ArmPartsRight() const;
  std::vector<Part> ArmPartsLeft() const;
  std::vector<Part> ToolParts(const std::string &itemId,
                               const glm::vec3 &socket) const;
  void DrawHeld(const InventoryEntryRef *entry, const glm::vec3 &socket,
                const glm::mat4 &mvpBase);
  void DrawBlockCube(const std::string &typeName, const glm::vec3 &socket,
                     const glm::mat4 &mvpBase);
  GLuint ResolveBlockAtlas(const std::string &typeName) const;
  glm::mat4 BuildRootMatrix(bool mirrorX) const;

  std::shared_ptr<UItemDefinitionStorage> Items;
  std::shared_ptr<UBlockDefinitionStorage> Blocks;
  std::shared_ptr<UTextureCubeStorage> Textures;
  std::shared_ptr<UShaderManager> ShaderManager;
  std::shared_ptr<class UShaderProgram> Shader;

  GLuint CubeVao{0};
  GLuint CubeVbo{0};
  GLuint CubeEbo{0};
  GLuint SkinTex{0};
  GLuint SleeveTex{0};
  GLuint MetalTex{0};
  GLuint WoodTex{0};

  FpMotion Motion;
  float BobX{0.f};
  float BobY{0.f};
};

} // namespace cutum

#endif

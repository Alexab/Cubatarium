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
class UCreatureTextureStorage;
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
  std::string SpeciesId;
  std::string SkinId;
};

/// Luanti-style clear-Z FP viewmodel + Minecraft dual arms (box mesh).
/// Not body-in-FP; skinned glTF arms remain TD-ITEM-004. Box arms sample
/// player_skin_atlas UV for the right-arm region when a diffuse is available.
class UFpViewmodelRenderer
{
public:
  UFpViewmodelRenderer(std::shared_ptr<UItemDefinitionStorage> items,
                       std::shared_ptr<UBlockDefinitionStorage> blocks,
                       std::shared_ptr<UTextureCubeStorage> textures,
                       std::shared_ptr<UCreatureTextureStorage> creatureTextures,
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
    bool useSkinAtlas{false};
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
  bool EnsureArmSkinMesh(int texW, int texH);
  GLuint SolidTex(unsigned char r, unsigned char g, unsigned char b);
  void DrawParts(const std::vector<Part> &parts, const glm::mat4 &mvpBase,
                 GLuint skinAtlas);
  std::vector<Part> ArmPartsRight() const;
  std::vector<Part> ArmPartsLeft() const;
  std::vector<Part> ToolParts(const std::string &itemId,
                               const glm::vec3 &socket) const;
  void DrawHeld(const InventoryEntryRef *entry, const glm::vec3 &socket,
                const glm::mat4 &mvpBase);
  void DrawBlockCube(const std::string &typeName, const glm::vec3 &socket,
                     const glm::mat4 &mvpBase);
  GLuint ResolveBlockAtlas(const std::string &typeName) const;
  GLuint ResolvePlayerSkin(const std::string &speciesId,
                           const std::string &skinId) const;
  glm::mat4 BuildRootMatrix(bool mirrorX) const;
  /// TD-ITEM-004: draw skinned arm mesh if loaded; otherwise false → box arms.
  bool TryDrawSkinnedArms(const glm::mat4 &mvpBase, bool leftHand);

  std::shared_ptr<UItemDefinitionStorage> Items;
  std::shared_ptr<UBlockDefinitionStorage> Blocks;
  std::shared_ptr<UTextureCubeStorage> Textures;
  std::shared_ptr<UCreatureTextureStorage> CreatureTextures;
  std::shared_ptr<UShaderManager> ShaderManager;
  std::shared_ptr<class UShaderProgram> Shader;

  GLuint CubeVao{0};
  GLuint CubeVbo{0};
  GLuint CubeEbo{0};
  GLuint ArmSkinVao{0};
  GLuint ArmSkinVbo{0};
  GLuint ArmSkinEbo{0};
  int ArmSkinTexW{0};
  int ArmSkinTexH{0};
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

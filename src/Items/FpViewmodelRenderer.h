#ifndef FP_VIEWMODEL_RENDERER_H
#define FP_VIEWMODEL_RENDERER_H

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "Items/ItemVisualPresetLibrary.h"

typedef unsigned int GLuint;

namespace cutum
{

class UItemDefinitionStorage;
class UBlockDefinitionStorage;
class UTextureCubeStorage;
class UCreatureTextureStorage;
class UShaderManager;
struct ResolvedCreatureAppearance;
struct CreatureDefinition;
struct CreatureBoneSkeletonMeshAsset;
struct InventoryEntryRef;

struct FpArmDrawAudit
{
  bool Left{false};
  std::string Source;
  GLuint Texture{0};
  glm::mat4 Mvp{1.f};
  uint64_t MeshHash{0};
  GLuint VertexBuffer{0};
  size_t VertexFloatCount{0};
  float SourceScale{1.f};
  glm::mat4 RootMvp{1.f};
};

struct FpViewmodelDrawAudit
{
  GLuint SkinTexture{0};
  std::vector<FpArmDrawAudit> Arms;
};

struct FpViewmodelDrawParams
{
  int FramebufferW{0};
  int FramebufferH{0};
  const InventoryEntryRef *Active{nullptr};
  const InventoryEntryRef *Offhand{nullptr};
  std::string SpeciesId;
  std::shared_ptr<const ResolvedCreatureAppearance> Appearance;
  const CreatureDefinition *Definition{nullptr};
  GLuint TargetFramebuffer{0};
  FpViewmodelDrawAudit *Audit{nullptr};
};

/// Clear-Z FP presentation with authored B arm meshes/UV when available.
/// Independent FP transforms/presets keep the world rig unchanged. The legacy
/// atlas boxes remain a fallback for definitions without usable arm geometry.
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
  void NotifyUseVisual(const std::string &presetId, bool holdAtEnd);
  void ClearHeldVisual();
  void SetActiveItemHint(const std::string &itemId);

  /// AFTER Geometry::Paint, BEFORE HUD. Default framebuffer.
  void DrawWorldOverlay(const FpViewmodelDrawParams &params);

private:
  FpViewmodelDrawAudit *ActiveAudit{nullptr};
  bool DrawingLeftArm{false};
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

  struct FpAnimPlayback
  {
    std::string PresetId;
    float T{-1.f};
    float Duration{0.28f};
    bool HoldAtEnd{false};
    bool Holding{false};
    ItemVisualPreset Snapshot{};
    bool HaveSnapshot{false};
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
    FpAnimPlayback Anim;
    std::string ActiveItemHint;
  };

  void BeginPresetAnim(const std::string &presetId, bool holdAtEnd);
  float SampleEasing(float t, ItemVisualEasing easing) const;
  void ApplyPresetToOffsets(float eased, float &ox, float &oy, float &oz,
                            glm::mat4 &swing) const;

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
  bool TryDrawGltfHeld(const std::string &itemId, const glm::vec3 &socket,
                       const glm::mat4 &mvpBase);
  void DrawBlockCube(const std::string &typeName, const glm::vec3 &socket,
                     const glm::mat4 &mvpBase);
  GLuint ResolveBlockAtlas(const std::string &typeName) const;
  GLuint ResolvePlayerSkin(const std::string &speciesId,
                             const ResolvedCreatureAppearance *appearance) const;
  glm::mat4 BuildRootMatrix(bool mirrorX) const;
  /// Mesh FP arms (box UV atlas path). Full GPU skinning remains optional.
  bool TryDrawLegacyAtlasArms(const glm::mat4 &mvpBase, bool leftHand,
                          GLuint skinAtlas);
  void PrepareArmSource(const CreatureDefinition *definition, const ResolvedCreatureAppearance *appearance);
  bool TryDrawAuthoredArm(const glm::mat4 &mvpBase,bool leftHand,GLuint skinAtlas);

  std::shared_ptr<const CreatureBoneSkeletonMeshAsset> ArmSource;
  std::string ArmSpecies,ArmFile,ArmGeometry,ArmDirectory;
  uint64_t ArmRevision{0};
  bool ArmRequestSeen{false};
  std::vector<std::pair<size_t,glm::mat4>> ArmSegments[2];
  size_t ArmIndices[2]{static_cast<size_t>(-1),static_cast<size_t>(-1)};
  glm::mat4 ArmFits[2]{glm::mat4(1.f),glm::mat4(1.f)};
  float ArmScales[2]{1.f,1.f};

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

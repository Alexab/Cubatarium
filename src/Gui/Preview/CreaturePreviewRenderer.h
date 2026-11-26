#ifndef CREATURE_PREVIEW_RENDERER_H
#define CREATURE_PREVIEW_RENDERER_H

#include "Creatures/Core/HumanAppearance.h"

#include "Creatures/Locomotion/LocomotionTypes.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Gui/Preview/PreviewRenderResources.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

typedef unsigned int GLuint;

namespace cutum
{

class UCreatureDefinitionStorage;
class VisualAssetCensus;
class USkinDefinitionStorage;
class UCreatureTextureStorage;
class UShaderManager;
class ItemSurfaceRenderer;
class IUCreatureVisual;
struct ItemDrawRequest;
struct ItemVisualInputs;
class CreatureDrawQueue;
struct CreatureGltfMeshAsset;
struct PreviewRendererStorage;

class UCreaturePreviewRenderer
{
public:
  UCreaturePreviewRenderer(std::shared_ptr<UCreatureDefinitionStorage> species,
                           std::shared_ptr<USkinDefinitionStorage> skins,
                           std::shared_ptr<UCreatureTextureStorage> textures,
                           std::shared_ptr<UShaderManager> shaderManager);
  ~UCreaturePreviewRenderer();
  static std::shared_ptr<UCreaturePreviewRenderer> Create(
      std::shared_ptr<UCreatureDefinitionStorage> species,
      std::shared_ptr<USkinDefinitionStorage> skins,
      std::shared_ptr<UCreatureTextureStorage> textures,
      std::shared_ptr<UShaderManager> shaderManager);
  void InspectVisualAssets(VisualAssetCensus &census) const;
  PreviewRendererStorage InspectOwnedStorage(const UCreatureDefinitionStorage *species,
      const USkinDefinitionStorage *skins, const UCreatureTextureStorage *textures,
      const UShaderManager *shaders) const noexcept;

  bool Initialize();
  void Shutdown();
  void Invalidate();
  void SetAnimationPreviewState(LocomotionState state)
  {
    AnimationPreviewState = state;
  }
  void SetItemInspection(int mode, float ambient)
  {
    ItemInspectionMode = mode;
    ItemInspectionAmbient = ambient;
  }
  void SetTransparentBackground(bool enabled)
  {
    TransparentBackground = enabled;
  }
  void SetAnimationPreviewSpeedRatio(float ratio)
  {
    AnimationPreviewSpeedRatio =
        ratio >= 0.f ? (ratio > 3.f ? 3.f : ratio) : 0.f;
  }
  /// Optional QA observer of the actual anchor supplied to equipment drawers.
  void SetAttachmentObserver(
      std::function<void(const std::string &, const glm::mat4 &)> observer)
  {
    AttachmentObserver = std::move(observer);
  }
  void SetAttachmentBoundsObserver(
      std::function<void(const std::string &, const glm::vec3 &,
                         const glm::vec3 &)>
          observer)
  {
    AttachmentBoundsObserver = std::move(observer);
  }
  /// Explicit research capture: shared world framing and observation of the
  /// exact native submissions; unset in ordinary icons/dock rendering.
  void SetQaEnvelopeProbe(
      std::optional<std::pair<glm::vec3, glm::vec3>> bounds,
      std::function<void(CreatureDrawQueue &, const glm::mat4 &)> observer = {},
      std::optional<glm::mat4> viewProjection = {},
      std::optional<glm::vec3> cameraPosition = {})
  {
    QaFramingBounds = std::move(bounds);
    QaSubmissionObserver = std::move(observer);
    QaViewProjection = std::move(viewProjection);
    QaCameraPosition = std::move(cameraPosition);
  }

  void
  SetItemSurfaceObserver(std::function<void(const ItemDrawRequest &)> observer)
  {
    ItemSurfaceObserver = std::move(observer);
  }
  /// Read-only final pose landmarks for native contact QA and studio overlays.
  void SetSkeletonPoseObserver(
      std::function<void(const IUCreatureVisual &)> observer)
  {
    SkeletonPoseObserver = std::move(observer);
  }

  UCreatureDefinitionStorage *GetSpecies() const { return Species.get(); }
  USkinDefinitionStorage *GetSkins() const { return Skins.get(); }
  UCreatureTextureStorage *GetTextures() const { return Textures.get(); }

  /// Renders into the internal FBO; returns the color attachment texture.
  GLuint Render(const std::string &speciesId, const std::string &skinId,
                int size, float yawDeg, float pitchDeg);

  /// Allocates a standalone texture (for icon cache entries).
  /// Optional main/offhand item ids attach to rightItem/leftItem bones.
  GLuint RenderToUniqueTexture(
      const std::string &speciesId, const std::string &skinId, int size,
      float yawDeg, float pitchDeg, float animTimeSec = 0.f,
      bool animateWalk = false,
      const std::array<WornArmorPreviewSlot, kWornArmorSlotCount> *armor =
          nullptr,
      const std::string *mainItemId = nullptr,
      const std::string *offhandItemId = nullptr,
      const HumanAppearance *human = nullptr,
      const ItemVisualInputs *mainInputs = nullptr,
      const ItemVisualInputs *offInputs = nullptr);

  GLuint CreateSolidColorTexture(int size, float r, float g, float b, float a);

  /// Flat catalog icon when iconMode requests it; otherwise 0.
  GLuint TryGetDirectSpeciesIcon(const std::string &speciesId) const;

private:
  struct GltfBoundsEntry
  {
    std::shared_ptr<const CreatureGltfMeshAsset> asset;
    glm::vec3 low{0.f}, high{0.f};
  };
  std::unordered_map<std::string, GltfBoundsEntry> GltfBoundsCache;
  uint64_t BoundsDefinitionsRevision{0}, BoundsBoneRevision{0},
      BoundsGltfRevision{0};
  std::unordered_map<std::string, std::pair<glm::vec3, glm::vec3>>
      ProceduralBoundsCache;
  std::optional<LocomotionState> AnimationPreviewState;
  bool TransparentBackground{false};
  int ItemInspectionMode{0};
  float ItemInspectionAmbient{.65f};
  float AnimationPreviewSpeedRatio{1.f};
  std::function<void(const std::string &, const glm::mat4 &)>
      AttachmentObserver;
  std::function<void(const std::string &, const glm::vec3 &, const glm::vec3 &)>
      AttachmentBoundsObserver;
  std::optional<std::pair<glm::vec3, glm::vec3>> QaFramingBounds;
  std::function<void(CreatureDrawQueue &, const glm::mat4 &)>
      QaSubmissionObserver;
  std::optional<glm::mat4> QaViewProjection;
  std::optional<glm::vec3> QaCameraPosition;
  bool InitCubeMesh();
  bool EnsureFboSize(int size);
  glm::mat4 OrbitView(float yawDeg, float pitchDeg, float distance) const;
  bool DrawSpeciesParts(const std::string &speciesId, const std::string &skinId,
                        int viewportSize, float yawDeg, float pitchDeg,
                        float animTimeSec = 0.f, bool animateWalk = false,
                        const std::array<WornArmorPreviewSlot,
                                         kWornArmorSlotCount> *armor = nullptr,
                        const std::string *mainItemId = nullptr,
                        const std::string *offhandItemId = nullptr,
                        const HumanAppearance *human = nullptr,
                        const ItemVisualInputs *mainInputs = nullptr,
                        const ItemVisualInputs *offInputs = nullptr);

  std::shared_ptr<UCreatureDefinitionStorage> Species;
  std::shared_ptr<USkinDefinitionStorage> Skins;
  std::shared_ptr<UCreatureTextureStorage> Textures;
  std::shared_ptr<UShaderManager> ShaderManager;
  std::shared_ptr<class UShaderProgram> Shader;
  std::shared_ptr<class UShaderProgram> SkinnedShader;
  std::unique_ptr<ItemSurfaceRenderer> ItemRenderer;
  uint64_t ItemRevision{0};
  std::function<void(const ItemDrawRequest &)> ItemSurfaceObserver;
  std::function<void(const IUCreatureVisual &)> SkeletonPoseObserver;

  PreviewCubeMesh Cube, HeadCube, BodyCube, RigidHeadCube;
  PreviewRenderTarget Target;
  int FboSize{0};
  bool FactoryOwner{false};
};

} // namespace cutum

#endif

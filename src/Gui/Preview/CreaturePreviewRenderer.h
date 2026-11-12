#ifndef CREATURE_PREVIEW_RENDERER_H
#define CREATURE_PREVIEW_RENDERER_H

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <optional>
#include <unordered_map>
#include <cstdint>
#include "Creatures/Locomotion/LocomotionTypes.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

typedef unsigned int GLuint;

namespace cutum
{

class UCreatureDefinitionStorage;
class USkinDefinitionStorage;
class UCreatureTextureStorage;
class UShaderManager;
class CreatureDrawQueue;
struct WornArmorPreviewSlot;
struct CreatureGltfMeshAsset;

class UCreaturePreviewRenderer
{
public:
  UCreaturePreviewRenderer(
      std::shared_ptr<UCreatureDefinitionStorage> species,
      std::shared_ptr<USkinDefinitionStorage> skins,
      std::shared_ptr<UCreatureTextureStorage> textures,
      std::shared_ptr<UShaderManager> shaderManager);
  ~UCreaturePreviewRenderer();

  bool Initialize();
  void Shutdown();
  void Invalidate();
  void SetAnimationPreviewState(LocomotionState state)
  {
    AnimationPreviewState = state;
  }
  void SetTransparentBackground(bool enabled) { TransparentBackground = enabled; }
  void SetAnimationPreviewSpeedRatio(float ratio)
  {
    AnimationPreviewSpeedRatio = ratio >= 0.f ? (ratio > 3.f ? 3.f : ratio) : 0.f;
  }
  /// Optional QA observer of the actual anchor supplied to equipment drawers.
  void SetAttachmentObserver(std::function<void(const std::string &, const glm::mat4 &)> observer)
  {
    AttachmentObserver = std::move(observer);
  }
  void SetAttachmentBoundsObserver(std::function<void(const std::string &, const glm::vec3 &, const glm::vec3 &)> observer)
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
      const std::array<WornArmorPreviewSlot, kWornArmorSlotCount> *armor = nullptr,
      const std::string *mainItemId = nullptr,
      const std::string *offhandItemId = nullptr);

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
  uint64_t BoundsDefinitionsRevision{0},BoundsBoneRevision{0},BoundsGltfRevision{0};
  std::unordered_map<std::string, std::pair<glm::vec3, glm::vec3>> ProceduralBoundsCache;
  std::optional<LocomotionState> AnimationPreviewState;
  bool TransparentBackground{false};
  float AnimationPreviewSpeedRatio{1.f};
  std::function<void(const std::string &, const glm::mat4 &)> AttachmentObserver;
  std::function<void(const std::string &, const glm::vec3 &, const glm::vec3 &)> AttachmentBoundsObserver;
  std::optional<std::pair<glm::vec3, glm::vec3>> QaFramingBounds;
  std::function<void(CreatureDrawQueue &, const glm::mat4 &)> QaSubmissionObserver;
  std::optional<glm::mat4> QaViewProjection;
  std::optional<glm::vec3> QaCameraPosition;
  bool InitCubeMesh();
  bool EnsureFboSize(int size);
  glm::mat4 OrbitView(float yawDeg, float pitchDeg, float distance) const;
  bool DrawSpeciesParts(const std::string &speciesId, const std::string &skinId,
                        int viewportSize, float yawDeg, float pitchDeg,
                        float animTimeSec = 0.f, bool animateWalk = false,
                        const std::array<WornArmorPreviewSlot, kWornArmorSlotCount> *armor =
                            nullptr,
                        const std::string *mainItemId = nullptr,
                        const std::string *offhandItemId = nullptr);

  std::shared_ptr<UCreatureDefinitionStorage> Species;
  std::shared_ptr<USkinDefinitionStorage> Skins;
  std::shared_ptr<UCreatureTextureStorage> Textures;
  std::shared_ptr<UShaderManager> ShaderManager;
  std::shared_ptr<class UShaderProgram> Shader;
  std::shared_ptr<class UShaderProgram> SkinnedShader;

  GLuint CubeVao{0};
  GLuint CubeVbo{0};
  GLuint CubeEbo{0};
  GLuint HeadCubeVao{0};
  GLuint HeadCubeVbo{0};
  GLuint HeadCubeEbo{0};
  GLuint BodyCubeVao{0};
  GLuint BodyCubeVbo{0};
  GLuint BodyCubeEbo{0};
  GLuint RigidHeadCubeVao{0};
  GLuint RigidHeadCubeVbo{0};
  GLuint RigidHeadCubeEbo{0};
  GLuint Fbo{0};
  GLuint ColorTex{0};
  GLuint DepthRbo{0};
  int FboSize{0};
};

} // namespace cutum

#endif

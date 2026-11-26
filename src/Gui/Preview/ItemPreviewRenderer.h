#ifndef ITEM_PREVIEW_RENDERER_H
#define ITEM_PREVIEW_RENDERER_H

#include "Gui/Preview/PreviewRenderResources.h"
#include "Items/ItemVisualInputs.h"
#include "Render/Textures/GlTextureOwner.h"
#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>

typedef unsigned int GLuint;

namespace cutum
{

class UItemDefinitionStorage;
class VisualAssetCensus;
class UShaderManager;
struct ItemDrawRequest;
struct PreviewRendererStorage;

/// Lightweight 3D preview renderer for item tools / armor props.
/// Supports (in order): static glTF via CreatureGltfLoader, parts[] JSON cubes,
/// then procedural FallbackParts.
class UItemPreviewRenderer
{
public:
  UItemPreviewRenderer(std::shared_ptr<UItemDefinitionStorage> items,
                       std::shared_ptr<UShaderManager> shaderManager);
  ~UItemPreviewRenderer();
  static std::shared_ptr<UItemPreviewRenderer> Create(std::shared_ptr<UItemDefinitionStorage> items,
      std::shared_ptr<UShaderManager> shaders);
  void InspectVisualAssets(VisualAssetCensus &census) const;
  void InspectRenderImages(class RenderImageCensus &out) const;
  PreviewRendererStorage InspectOwnedStorage(const UItemDefinitionStorage *items,
      const UShaderManager *shaders) const noexcept;

  bool Initialize();
  void Shutdown();
  void Invalidate();
  void SetInspectionDrawObserver(
      std::function<void(const ItemDrawRequest &)> observer)
  {
    InspectionDrawObserver = std::move(observer);
  }
  void SetInspectionFramingBounds(
      std::optional<std::pair<glm::vec3, glm::vec3>> bounds)
  {
    InspectionFramingBounds = std::move(bounds);
  }

  /// Renders to an owned texture (caller must keep/delete it).
  GLuint RenderToUniqueTexture(const std::string &itemId, int size,
                               float yawDeg, float pitchDeg,
                               const ItemVisualInputs &inputs = {});
  /// Resolve a 4x encoded-sRGB source into an owned icon texture in linear
  /// colour space. The source remains owned by its caller.
  GLuint ResolveIconTexture(GLuint source, int size);
  std::shared_ptr<const struct ItemRenderAsset>
  GetInspectionAsset(const std::filesystem::path &path);
  GLuint RenderInspectionTexture(const std::string &itemId,
                                 const std::filesystem::path &path, int size,
                                 float yaw, float pitch, int mode,
                                 float ambient,
                                 const ItemVisualInputs &inputs = {});

private:
  struct Part
  {
    std::string textureStem;
    float ox{0.f};
    float oy{0.f};
    float oz{0.f};
    float sx{1.f};
    float sy{1.f};
    float sz{1.f};
  };

  bool InitCubeMesh();
  bool EnsureFboSize(int size);
  void ReleaseFramebuffer();
  GLuint GetOrCreateColorTexture(const std::string &itemId);

  bool TryLoadPartsFromModelJson(const std::string &itemId,
                                 std::string &outModelRelPath,
                                 std::vector<Part> &outParts) const;

  /// Returns true if a static glTF mesh was drawn into the bound FBO.
  bool TryDrawGltfModel(const std::string &itemId, const std::string &modelRel,
                        const glm::mat4 &projection, const glm::mat4 &view,
                        const ItemVisualInputs &inputs);

  std::vector<Part> FallbackParts(const std::string &itemId) const;
  bool DrawGltfPath(const std::string &itemId,
                    const std::filesystem::path &path,
                    const glm::mat4 &projection, const glm::mat4 &view,
                    const ItemVisualInputs &inputs);

  glm::mat4 OrbitView(float yawDeg, float pitchDeg, float distance) const;

  std::shared_ptr<UItemDefinitionStorage> Items;
  std::shared_ptr<UShaderManager> ShaderManager;

  std::shared_ptr<class UShaderProgram> Shader;
  std::shared_ptr<class UShaderProgram> IconResolveShader;
  bool RenderingIconSamples{false};
  GLuint IconResolveFbo{0};
  std::unique_ptr<class ItemSurfaceRenderer> SurfaceRenderer;
  std::shared_ptr<const struct ItemRenderAsset> InspectionAsset;
  std::filesystem::path InspectionPath;
  int InspectionMode{0};
  std::function<void(const ItemDrawRequest &)> InspectionDrawObserver;
  std::optional<std::pair<glm::vec3, glm::vec3>> InspectionFramingBounds;
  float InspectionAmbient{.65f};
  std::unordered_map<std::string, std::shared_ptr<const ItemRenderAsset>>
      InspectionAssets;

  PreviewCubeMesh Cube;
  GLuint ScratchMeshVao{0};
  GLuint ScratchMeshVbo{0};
  GLuint ScratchMeshEbo{0};
  size_t ScratchVertexBytes{}, ScratchIndexBytes{};

  PreviewRenderTarget Target;
  int FboSize{0};

  std::array<UGlTextureOwner, 6> ColorTextures;
  bool FactoryOwner{false};
};

} // namespace cutum

#endif

#ifndef CREATURE_PREVIEW_RENDERER_H
#define CREATURE_PREVIEW_RENDERER_H

#include <array>
#include <memory>
#include <string>

#include <glm/mat4x4.hpp>

typedef unsigned int GLuint;

namespace cutum
{

class UCreatureDefinitionStorage;
class USkinDefinitionStorage;
class UCreatureTextureStorage;
class UShaderManager;
struct WornArmorPreviewSlot;

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

  UCreatureDefinitionStorage *GetSpecies() const { return Species.get(); }
  USkinDefinitionStorage *GetSkins() const { return Skins.get(); }
  UCreatureTextureStorage *GetTextures() const { return Textures.get(); }

  /// Renders into the internal FBO; returns the color attachment texture.
  GLuint Render(const std::string &speciesId, const std::string &skinId,
                int size, float yawDeg, float pitchDeg);

  /// Allocates a standalone texture (for icon cache entries).
  GLuint RenderToUniqueTexture(
      const std::string &speciesId, const std::string &skinId, int size,
      float yawDeg, float pitchDeg, float animTimeSec = 0.f,
      bool animateWalk = false,
      const std::array<WornArmorPreviewSlot, 6> *armor = nullptr);

  GLuint CreateSolidColorTexture(int size, float r, float g, float b, float a);

  /// Flat catalog icon when iconMode requests it; otherwise 0.
  GLuint TryGetDirectSpeciesIcon(const std::string &speciesId) const;

private:
  bool InitCubeMesh();
  bool EnsureFboSize(int size);
  glm::mat4 OrbitView(float yawDeg, float pitchDeg, float distance) const;
  bool DrawSpeciesParts(const std::string &speciesId, const std::string &skinId,
                        int viewportSize, float yawDeg, float pitchDeg,
                        float animTimeSec = 0.f, bool animateWalk = false,
                        const std::array<WornArmorPreviewSlot, 6> *armor =
                            nullptr);

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

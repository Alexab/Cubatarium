#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Visual/CreatureVisualBackendSelect.h"

#include <iostream>
#include <string>

namespace
{

void Expect(bool condition, const char *message, int &failures)
{
  if (!condition)
  {
    std::cerr << "creature_visual_backend_select_test: " << message << '\n';
    ++failures;
  }
}

cutum::CreatureDefinition MakeDef(const std::string &backend)
{
  cutum::CreatureDefinition def;
  def.Id = "test_species";
  def.visual.backend = backend;
  return def;
}

} // namespace

int main()
{
  using namespace cutum;
  int failures = 0;

  {
    const CreatureDefinition def;
    Expect(!CreatureUsesBillboardSprite(def),
           "billboard defaults to off", failures);
    Expect(ResolveCreatureVisualBackend(def) == CreatureVisualBackend::RigidVoxels,
           "empty backend string resolves to rigid_voxels", failures);
  }

  {
    CreatureDefinition def = MakeDef("gltf_skeleton");
    def.visual.gltf.modelPath = "model.gltf";
    Expect(ResolveCreatureVisualBackend(def) == CreatureVisualBackend::GltfSkeleton,
           "gltf backend with a model resolves to gltf", failures);
  }

  {
    CreatureDefinition def = MakeDef("gltf_skeleton");
    def.visual.fallbackBackend = "rigid_voxels";
    Expect(ResolveCreatureVisualBackend(def) == CreatureVisualBackend::RigidVoxels,
           "gltf backend without a model falls back when set", failures);
  }

  {
    CreatureDefinition def = MakeDef("gltf_skeleton");
    Expect(ResolveCreatureVisualBackend(def) == CreatureVisualBackend::GltfSkeleton,
           "gltf backend without a model and no fallback stays gltf", failures);
  }

  {
    // Single-step fallback: the fallback backend itself is not asset-checked.
    CreatureDefinition def = MakeDef("bone_skeleton");
    def.visual.fallbackBackend = "gltf_skeleton";
    Expect(ResolveCreatureVisualBackend(def) == CreatureVisualBackend::GltfSkeleton,
           "fallback is taken unconditionally, without asset re-check", failures);
  }

  {
    CreatureDefinition def = MakeDef("bedrock_geo");
    def.visual.boneSkeleton.geometryId = "geometry.test";
    Expect(ResolveCreatureVisualBackend(def) == CreatureVisualBackend::BoneSkeleton,
           "legacy bedrock_geo alias resolves to bone_skeleton", failures);
  }

  {
    CreatureDefinition def = MakeDef("definitely_not_a_backend");
    Expect(ResolveCreatureVisualBackend(def) == CreatureVisualBackend::RigidVoxels,
           "unknown backend string resolves to rigid_voxels", failures);
  }

  {
    // fire_spirit-style config: billboard wins over the declared backend, so
    // its gltf section never loads (audit V14).
    CreatureDefinition def = MakeDef("gltf_skeleton");
    def.visual.gltf.modelPath = "model.gltf";
    def.visual.sprite.billboard = true;
    Expect(CreatureUsesBillboardSprite(def),
           "billboard flag wins over the declared gltf backend", failures);
    Expect(ResolveCreatureVisualBackend(def) == CreatureVisualBackend::GltfSkeleton,
           "backend resolution ignores the billboard flag", failures);
  }

  if (failures != 0)
  {
    return 1;
  }
  std::cout << "creature_visual_backend_select_test: OK\n";
  return 0;
}

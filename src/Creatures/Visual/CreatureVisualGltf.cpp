#include "Creatures/Visual/CreatureVisualGltf.h"
#include "App/Settings/RenderSettings.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Render/Engine/GeometryEngine.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <iostream>
#include <unordered_set>

namespace cutum
{

void UCreatureVisualGltf::UpdatePose(const UCreature &creature,
                                     const CreatureLocomotionFacts & /*facts*/,
                                     const CreaturePoseParams & /*pose*/,
                                     const CreatureDefinition & /*animDef*/,
                                     float /*dt*/)
{
  BodyOrigin = creature.GetFeetPosition();
  SizeBlocks = creature.GetBounds().currentSizeBlocks;
}

void UCreatureVisualGltf::SubmitDraw(UGeometryEngine &engine,
                                     const glm::mat4 &viewProj)
{
  static std::unordered_set<std::string> logged;
  if (logged.insert(Appearance.visualBackend).second)
  {
    std::cerr << "UCreatureVisualGltf::SubmitDraw: not implemented ("
              << Appearance.visualBackend << ")" << std::endl;
  }

  const RenderSettings &settings = engine.GetRenderSettings();
  if (!settings.CreatureDebugBounds)
  {
    return;
  }

  const glm::vec3 center = BoundsCollisionCenter(BodyOrigin, SizeBlocks);
  glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
  model = glm::scale(model, SizeBlocks);
  engine.DrawBoxWireframe(viewProj * model, glm::vec4(1.0f, 0.5f, 0.2f, 1.0f));
}

} // namespace cutum

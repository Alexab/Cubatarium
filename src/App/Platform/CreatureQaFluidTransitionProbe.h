#pragma once
#include "App/Platform/CreatureQaEnvelopeCapture.h"

namespace cutum
{
inline void
ExportCreatureQaFluidTransitionProbe(UCreaturePreviewRenderer &preview,
                                     const std::vector<std::string> &ids,
                                     const std::filesystem::path &directory)
{
  const char *enabled = std::getenv("CUBA_FLIGHT_QA_FLUID_TRANSITIONS");
  if (!enabled || std::string(enabled) != "1")
    return;
  using namespace creature_envelope_qa;
  Json report = {{"schema", "creature-native-fluid-transitions.v1"},
                 {"scope", "Synthetic retained actual visual with explicit "
                           "land/free-fluid/bottom facts; CPU vertices and "
                           "palettes, not world travel or GPU fade acceptance"},
                 {"resources", Json::array()}};
  RenderSettings settings;
  settings.CreatureTexturedParts = true;
  CreatureDrawContext context;
  context.Textures = preview.GetTextures();
  context.Settings = &settings;
  struct Condition
  {
    const char *name;
    LocomotionState state;
    bool fluid;
    bool supported;
  };
  const Condition landIdle{"land_idle", LocomotionState::Idle, false, true};
  const Condition landWalk{"land_walk", LocomotionState::Walk, false, true};
  const Condition fluidIdle{"fluid_idle", LocomotionState::Idle, true, false};
  const Condition swim{"free_swim", LocomotionState::Swim, true, false};
  const Condition tread{"bottom_tread", LocomotionState::Tread, true, true};
  const std::vector<std::pair<Condition, Condition>> transitions = {
      {landIdle, fluidIdle}, {fluidIdle, landIdle}, {landWalk, swim},
      {swim, landWalk},      {fluidIdle, swim},     {swim, fluidIdle},
      {swim, tread},         {tread, swim},         {landWalk, tread},
      {tread, landWalk}};
  for (const auto &id : ids)
  {
    const auto *def = preview.GetSpecies()->Get(id);
    if (!def || def->visual.backend != "gltf_skeleton" ||
        def->habitat != CreatureHabitat::Amphibious)
      throw std::runtime_error(
          "Fluid transition probe requires amphibious glTF species");
    const auto asset = CreatureGltfCache::Instance().Load(
        id, def->visual.gltf.modelPath, def->visual.sourceDirectory);
    if (!asset)
      throw std::runtime_error("Fluid transition source missing");
    const auto factsFor = [&](const Condition &condition)
    {
      CreatureLocomotionFacts facts;
      facts.state = condition.state;
      facts.archetype = condition.fluid ? LocomotionArchetype::Aquatic
                                        : def->locomotionArchetype;
      facts.inFluid = condition.fluid;
      facts.onGround = condition.supported;
      facts.onFluidBottom = condition.fluid && condition.supported;
      facts.horizontalSpeed = condition.state == LocomotionState::Idle
                                  ? 0.f
                                  : def->locomotion.walkSpeed;
      return facts;
    };
    const auto clipFor = [&](const Condition &condition)
    {
      const auto clip = ResolveAnimationClipId(condition.state,
                                               def->visual.Animation.stateMap)
                            .value_or("idle");
      if (!asset->animationIndexByName.count(clip))
        throw std::runtime_error("Fluid condition has no clip");
      return clip;
    };
    Json cases = Json::array();
    for (const auto &transition : transitions)
      for (int phaseIndex = 0; phaseIndex < 16; ++phaseIndex)
      {
        const auto &from = transition.first;
        const auto &to = transition.second;
        const auto fromClip = clipFor(from), toClip = clipFor(to);
        UCreature creature(1, id, glm::vec3(0.f),
                           glm::vec3(0.f, def->eyeHeight, 0.f));
        creature.GetBoundsMutable().profile = def->bounds;
        creature.GetBoundsMutable().currentSizeBlocks =
            def->bounds.restSizeBlocks;
        auto visual = CreateCreatureVisual(*def);
        visual->SetAppearance(ResolveCreatureAppearance(
            *preview.GetSpecies(), *preview.GetSkins(), id, ""));
        const auto settingsIt = def->visual.Animation.clips.find(fromClip);
        const float speed = settingsIt == def->visual.Animation.clips.end()
                                ? 1.f
                                : settingsIt->second.speed;
        const float duration = GltfAnimationDuration(
            asset->animations[asset->animationIndexByName.at(fromClip)]);
        const float phase = static_cast<float>(phaseIndex) / 16.f;
        auto facts = factsFor(from);
        visual->UpdatePose(creature, facts, CreaturePoseParams{}, *def,
                           phase * duration / std::max(.001f, speed));
        facts = factsFor(to);
        visual->UpdatePose(creature, facts, CreaturePoseParams{}, *def, 0.f);
        Json frames = Json::array();
        for (int step = 0; step <= 20; ++step)
        {
          glm::vec3 renderLow, renderHigh;
          if (!visual->GetConservativeWorldBounds(creature, *def, renderLow,
                                                  renderHigh))
            throw std::runtime_error(
                "Fluid transition lacks conservative bounds");
          if (step)
            visual->UpdatePose(creature, facts, CreaturePoseParams{}, *def,
                               .015f);
          CreatureDrawQueue queue;
          context.Queue = &queue;
          visual->SubmitDraw(context, glm::mat4(1.f));
          UWorldBounds sink(glm::mat4(1.f));
          queue.Flush(sink);
          if (!sink.bounds.any)
            throw std::runtime_error(
                "Fluid transition has no actual submissions");
          const bool covered =
              glm::all(glm::greaterThanEqual(sink.bounds.low,
                                             renderLow - glm::vec3(.0001f))) &&
              glm::all(glm::lessThanEqual(sink.bounds.high,
                                          renderHigh + glm::vec3(.0001f)));
          if (!covered)
            throw std::runtime_error(
                "Fluid transition escapes render envelope");
          frames.push_back({{"step", step},
                            {"low", Vec(sink.bounds.low)},
                            {"high", Vec(sink.bounds.high)},
                            {"palette_hash", sink.paletteHash},
                            {"draws", queue.Size()},
                            {"visual_envelope_covers_geometry", covered}});
        }
        cases.push_back({{"from", from.name},
                         {"to", to.name},
                         {"from_clip", fromClip},
                         {"to_clip", toClip},
                         {"from_state", ToString(from.state)},
                         {"to_state", ToString(to.state)},
                         {"outgoing_phase", phase},
                         {"from_in_fluid", from.fluid},
                         {"from_supported", from.supported},
                         {"in_fluid", to.fluid},
                         {"supported", to.supported},
                         {"on_fluid_bottom", facts.onFluidBottom},
                         {"frames", frames}});
      }
    report["resources"].push_back({{"id", id}, {"cases", cases}});
  }
  std::ofstream file(directory / "fluid_transition_probe.json");
  file << report.dump(2) << '\n';
  if (!file)
    throw std::runtime_error("Fluid transition manifest write failed");
}
} // namespace cutum

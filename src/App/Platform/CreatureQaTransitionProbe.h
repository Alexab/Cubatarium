#pragma once
#include "App/Platform/CreatureQaEnvelopeCapture.h"
#include <set>

namespace cutum
{
inline void ExportCreatureQaTransitionProbe(UCreaturePreviewRenderer &preview,
    const std::vector<std::string> &ids,const std::filesystem::path &directory)
{
  const char *enabled=std::getenv("CUBA_FLIGHT_QA_TRANSITION_PROBE");
  if(!enabled || std::string(enabled)!="1")return;
  using namespace creature_envelope_qa;
  Json report={{"schema","creature-native-transition-probe.v1"},
    {"scope","Actual retained visual UpdatePose and CPU submitted vertices/palettes, cold incoming clip, sampled grounded/free support; not GPU raster, terrain, sliding or every clip-history acceptance"},
    {"resources",Json::array()}};
  RenderSettings settings;settings.CreatureTexturedParts=true;
  CreatureDrawContext context;context.Textures=preview.GetTextures();context.Settings=&settings;
  for(const auto &id:ids)
  {
    const auto *def=preview.GetSpecies()->Get(id);
    if(!def || def->visual.backend!="gltf_skeleton")throw std::runtime_error("Transition initial probe requires glTF species");
    const auto asset=CreatureGltfCache::Instance().Load(id,def->visual.gltf.modelPath,def->visual.sourceDirectory);
    if(!asset)throw std::runtime_error("Transition source mesh missing");
    std::vector<std::pair<LocomotionState,std::string>> states;std::set<std::string> seen;
    for(const auto state:{LocomotionState::Idle,LocomotionState::Walk,LocomotionState::Run,LocomotionState::Action})
    {
      const auto clip=ResolveAnimationClipId(state,def->visual.Animation.stateMap).value_or("idle");
      if(asset->animationIndexByName.count(clip) && seen.insert(clip).second)states.emplace_back(state,clip);
    }
    Json cases=Json::array();
    for(const bool supported:{true,false})
    {
      // Seahorse is a free swimmer. Lobster's explicit QA fixture is benthic.
      if(supported && id=="seahorse")continue;
      for(const auto &from:states)for(const auto &to:states)
      {
        if(from.second==to.second)continue;
        if(!supported && (from.first!=LocomotionState::Walk || to.first!=LocomotionState::Idle))continue;
        for(int phaseIndex=0;phaseIndex<(supported?16:4);++phaseIndex)
        {
          const float phase=static_cast<float>(phaseIndex)/(supported?16.f:4.f);
          UCreature creature(1,id,glm::vec3(0.f),glm::vec3(0.f,def->eyeHeight,0.f));
          creature.GetBoundsMutable().profile=def->bounds;creature.GetBoundsMutable().currentSizeBlocks=def->bounds.restSizeBlocks;
          auto visual=CreateCreatureVisual(*def);
          visual->SetAppearance(ResolveCreatureAppearance(*preview.GetSpecies(),*preview.GetSkins(),id,""));
          CreatureLocomotionFacts facts;facts.onGround=supported;facts.inFluid=def->habitat==CreatureHabitat::Aquatic;
          facts.onFluidBottom=supported && facts.inFluid;facts.horizontalSpeed=def->locomotion.walkSpeed;
          const auto settingsIt=def->visual.Animation.clips.find(from.second);
          const float speed=settingsIt==def->visual.Animation.clips.end()?1.f:settingsIt->second.speed;
          const float duration=GltfAnimationDuration(asset->animations[asset->animationIndexByName.at(from.second)]);
          facts.state=from.first;visual->UpdatePose(creature,facts,CreaturePoseParams{},*def,phase*duration/std::max(.001f,speed));
          facts.state=to.first;visual->UpdatePose(creature,facts,CreaturePoseParams{},*def,0.f);
          Json frames=Json::array();
          for(int step=0;step<=10;++step)
          {
            glm::vec3 renderLow,renderHigh;
            if(!visual->GetConservativeWorldBounds(creature,*def,renderLow,renderHigh))
              throw std::runtime_error("Transition species lacks conservative bounds");
            if(step)visual->UpdatePose(creature,facts,CreaturePoseParams{},*def,.015f);
            CreatureDrawQueue queue;context.Queue=&queue;
            visual->SubmitDraw(context,glm::mat4(1.f));WorldBounds sink(glm::mat4(1.f));queue.Flush(sink);
            if(!sink.bounds.any)throw std::runtime_error("Transition has no actual submissions");
            const bool covered=glm::all(glm::greaterThanEqual(sink.bounds.low,renderLow-glm::vec3(.0001f))) &&
              glm::all(glm::lessThanEqual(sink.bounds.high,renderHigh+glm::vec3(.0001f)));
            if(!covered)throw std::runtime_error("Crossfade geometry escapes pre-update visual envelope");
            frames.push_back({{"step",step},{"low",Vec(sink.bounds.low)},{"high",Vec(sink.bounds.high)},
              {"palette_hash",sink.paletteHash},{"draws",queue.Size()},
              {"render_low",Vec(renderLow)},{"render_high",Vec(renderHigh)},{"visual_envelope_covers_geometry",covered}});
          }
          cases.push_back({{"from",from.second},{"to",to.second},{"outgoing_phase",phase},
            {"supported",supported},{"in_fluid",facts.inFluid},{"on_fluid_bottom",facts.onFluidBottom},{"frames",frames}});
        }
      }
    }
    report["resources"].push_back({{"id",id},{"cases",cases}});
  }
  std::ofstream file(directory/"transition_probe.json");file<<report.dump(2)<<'\n';
  if(!file)throw std::runtime_error("Transition probe manifest write failed");
}
}

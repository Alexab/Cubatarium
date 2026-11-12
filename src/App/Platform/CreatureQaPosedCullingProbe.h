#pragma once
#include "App/Platform/CreatureQaEnvelopeCapture.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Visual/CreatureVisualBackendSelect.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Items/ItemDefinitionStorage.h"
#include "Pose/CreaturePosePresenterRegistry.h"

namespace cutum
{
inline void ExportCreatureQaPosedCulling(UCreaturePreviewRenderer &preview,
    const std::vector<std::string> &ids,const std::filesystem::path &directory)
{
  const char *enabled=std::getenv("CUBA_FLIGHT_QA_POSED_CULLING");
  if(!enabled || std::string(enabled)!="1")return;
  using namespace creature_envelope_qa;
  struct Resource{std::string id,species,skin;};std::vector<Resource> resources;
  for(const auto &id:ids)
  {
    const auto *def=preview.GetSpecies()->Get(id);
    if(def && (CreatureUsesBillboardSprite(*def) || ResolveCreatureVisualBackend(*def)!=CreatureVisualBackend::GltfSkeleton))
      resources.push_back({id,id,{}});
  }
  for(const auto &id:preview.GetSkins()->ListEquippable())
  {
    const auto *skin=preview.GetSkins()->Get(id);
    if(skin && std::find(ids.begin(),ids.end(),skin->creatureId)!=ids.end())resources.push_back({id,skin->creatureId,id});
  }
  const auto folder=directory/"posed_culling";std::filesystem::create_directories(folder);
  Json report={{"schema","creature-native-posed-culling.v1"},
    {"scope","Actual IUCreatureVisual with synthetic actor/facts/inventory rendered by native FBO sink; clip predicate includes all body/wear/wield requests. Not world camera routes, stride or performance acceptance"},
    {"resources",Json::array()}};
  struct Reset{UCreaturePreviewRenderer &p;~Reset(){p.SetQaEnvelopeProbe({});p.SetTransparentBackground(false);}} reset{preview};
  preview.SetTransparentBackground(true);constexpr int size=128;
  UCreaturePosePresenterRegistry presenters;RegisterDefaultCreaturePosePresenters(presenters);
  RenderSettings settings;settings.CreatureTexturedParts=true;
  CreatureDrawContext ctx;ctx.Textures=preview.GetTextures();ctx.Settings=&settings;ctx.Items=WornEquipmentDrawer::ItemDefinitions();
  for(const auto &resource:resources)
  {
    const auto *def=preview.GetSpecies()->Get(resource.species);
    const bool humanoid=def->visual.backend=="bone_skeleton" && def->visual.boneSkeleton.animationProfile=="humanoid";
    std::vector<LocomotionState> states={LocomotionState::Idle,LocomotionState::Walk,LocomotionState::Run,
      LocomotionState::Crouch,LocomotionState::Jump,LocomotionState::Fall,LocomotionState::Action};
    if(def->habitat==CreatureHabitat::Aquatic || def->habitat==CreatureHabitat::Amphibious)
    {states.push_back(LocomotionState::Swim);states.push_back(LocomotionState::Tread);}
    if(def->habitat==CreatureHabitat::Aerial)
    {states.push_back(LocomotionState::Fly);states.push_back(LocomotionState::Hover);states.push_back(LocomotionState::Glide);}
    if(def->locomotionArchetype==LocomotionArchetype::Serpentine)
    {states.push_back(LocomotionState::Slither);states.push_back(LocomotionState::Coil);}
    Json stateNames=Json::array();for(const auto s:states)stateNames.push_back(ToString(s));
    Json loadouts=Json::array();
    for(const std::string material:humanoid?std::vector<std::string>{"bare","copper","iron","leather"}:std::vector<std::string>{"bare"})
    {
      UCreature creature(89999,resource.species,glm::vec3(0),glm::vec3(0,def->eyeHeight,0));
      creature.GetBoundsMutable().profile=def->bounds;creature.GetBoundsMutable().currentSizeBlocks=def->bounds.restSizeBlocks;
      if(!resource.skin.empty())creature.SetSkinId(resource.skin);
      if(material!="bare")
      {
        if(!ctx.Items)throw std::runtime_error("Posed culling item definitions missing");
        const char *slots[]={"head","chest","arms","hands","legs","feet"};
        auto &inventory=creature.GetInventory();
        for(size_t slot=0;slot<6;++slot)
        {
          const std::string item=material+"_"+slots[slot];inventory.AddItem(item,1);
          InventoryEntryRef entry{InventoryEntryKind::Item,item,1,false};
          if(!inventory.EquipArmor(slot,entry,*ctx.Items))throw std::runtime_error("Posed culling armor equip failed");
        }
        inventory.AddItem("wood_sword",1);inventory.AddItem("wood_torch",1);
        inventory.EnsureHotbarCount(1);
        inventory.AssignToHotbar(0,0,{InventoryEntryKind::Item,"wood_sword",1,false});inventory.SetActiveSlot(0,0);
        if(!inventory.EquipOffhand({InventoryEntryKind::Item,"wood_torch",1,false},*ctx.Items))throw std::runtime_error("Posed culling offhand failed");
      }
      Json frames=Json::array(),edge=nullptr;size_t index=0;
      for(const auto state:states)for(int phase=0;phase<4;++phase)
      {
        CreatureLocomotionFacts facts;facts.state=state;facts.archetype=def->locomotionArchetype;
        facts.stanceBlend=state==LocomotionState::Crouch?.85f:0.f;
        facts.horizontalSpeed=state==LocomotionState::Idle?0.f:def->locomotion.walkSpeed*(state==LocomotionState::Run?1.5f:1.f);
        facts.onGround=state!=LocomotionState::Jump && state!=LocomotionState::Fall &&
          state!=LocomotionState::Fly && state!=LocomotionState::Hover && state!=LocomotionState::Glide;
        facts.inFluid=def->habitat==CreatureHabitat::Aquatic || state==LocomotionState::Swim || state==LocomotionState::Tread;
        facts.onFluidBottom=false;if(facts.inFluid)facts.onGround=false;
        float period=1.f/std::max(.1f,def->visual.Animation.walkCycleHz);
        if(def->visual.boneSkeleton.animationProfile=="aerial")period*=.25f;
        const float time=phase*.25f*period;facts.animPhase=time*def->visual.Animation.walkCycleHz*6.283185307179586f;
        creature.GetLocomotion().SetStanceBlendForView(facts.stanceBlend);
        creature.GetBoundsMutable()=LerpBoundsStance(creature.GetBounds(),facts.stanceBlend);
        auto visual=CreateCreatureVisual(*def);
        visual->SetAppearance(ResolveCreatureAppearance(*preview.GetSpecies(),*preview.GetSkins(),resource.species,resource.skin));
        CreaturePoseParams pose;
        // Same facts/pose source as the world submit path, no GUI-only armor.
        if(auto *presenter=presenters.Get(ResolveCreaturePoseArchetype(def->visual.rig.templateId,facts.archetype)))pose=presenter->Compute(facts,*def,0.f);
        visual->UpdatePose(creature,facts,pose,*def,time);
        ctx.CameraPosition=glm::vec3(0,def->bounds.restSizeBlocks.y*.5f,10.f);
        CreatureDrawQueue measure;ctx.Queue=&measure;visual->SubmitDraw(ctx,glm::mat4(1));WorldBounds geometry(glm::mat4(1));measure.Flush(geometry);
        if(!geometry.bounds.any)throw std::runtime_error("Posed culling geometry empty");
        const glm::vec3 a=geometry.bounds.low,b=geometry.bounds.high,centre=(a+b)*.5f;
        const float radius=std::max(.2f,glm::length(b-a)*.6f),distance=radius*3.f;
        glm::vec3 eye=centre+glm::normalize(glm::vec3(.6f,.35f,1.f))*distance;
        glm::mat4 vp=glm::perspective(glm::radians(45.f),1.f,.01f,std::max(50.f,distance*4.f))*glm::lookAt(eye,centre,glm::vec3(0,1,0));
        Json observed;
        const auto observer=[&](CreatureDrawQueue &queue,const glm::mat4 &projection)
        {
          queue.Clear();ctx.Queue=&queue;ctx.CameraPosition=eye;visual->SubmitDraw(ctx,projection);
          const bool accepted=queue.RangeIntersectsClip(0);WorldBounds actual(projection);queue.Flush(actual);
          if(!actual.bounds.any)throw std::runtime_error("Posed native requests absent");
          observed={{"low",Vec(actual.bounds.low)},{"high",Vec(actual.bounds.high)},
            {"new_clip_accepts",accepted},{"draws",queue.Size()},{"view_projection",Matrix(projection)}};
        };
        preview.SetAnimationPreviewState(state);
        preview.SetQaEnvelopeProbe(std::make_pair(a,b),observer,vp,eye);
        const auto name=resource.id+"_"+material+"_"+std::to_string(index++)+".png";
        const auto native=SaveTexture(preview.RenderToUniqueTexture(resource.species,resource.skin,size,0,0,time,true),folder/name,size);
        if(!observed["new_clip_accepts"].get<bool>())throw std::runtime_error("Visible posed native frame rejected");
        frames.push_back({{"state",ToString(state)},{"phase",phase*.25f},{"native",native},{"observed",observed}});
        if(edge.is_null())
        {
          const auto sizeBlocks=def->bounds.maxSizeBlocks;
          const glm::vec3 ca(-sizeBlocks.x*.5f,0,-sizeBlocks.z*.5f),cb(sizeBlocks.x*.5f,sizeBlocks.y,sizeBlocks.z*.5f);
          int axis=-1;bool positive=true;float excess=.025f;
          for(int k=0;k<3;++k){if(b[k]-cb[k]>excess){axis=k;positive=true;excess=b[k]-cb[k];}if(ca[k]-a[k]>excess){axis=k;positive=false;excess=ca[k]-a[k];}}
          if(axis>=0)
          {
            eye=axis==2?glm::vec3(a.x-10,(a.y+b.y)*.5f,0):glm::vec3(0,(a.y+b.y)*.5f,b.z+10);
            const auto view=glm::lookAt(eye,axis==2?eye+glm::vec3(1,0,0):eye+glm::vec3(0,0,-1),glm::vec3(0,1,0));
            const auto gb=TransformBounds(view,a,b),box=TransformBounds(view,ca,cb);
            auto lo=gb.first-glm::vec3(.2f),hi=gb.second+glm::vec3(.2f);const int screenAxis=axis==1?1:0;
            if(positive)lo[screenAxis]=(gb.second[screenAxis]+box.second[screenAxis])*.5f;
            else hi[screenAxis]=(gb.first[screenAxis]+box.first[screenAxis])*.5f;
            vp=glm::ortho(lo.x,hi.x,lo.y,hi.y,.1f,50.f+glm::length(b-a))*view;
            const bool oldRejects=!CreatureBoundsIntersectsFrustum(Frustum::FromViewProjection(vp),glm::vec3(0),sizeBlocks,eye,0,true);
            preview.SetQaEnvelopeProbe(std::make_pair(a,b),observer,vp,eye);
            const auto pixels=SaveTexture(preview.RenderToUniqueTexture(resource.species,resource.skin,size,0,0,time,true),folder/(resource.id+"_"+material+"_edge.png"),size);
            const bool visible=pixels["alpha_pixels"].get<size_t>()>0;
            if(visible && !observed["new_clip_accepts"].get<bool>())throw std::runtime_error("Visible posed edge rejected");
            edge={{"frame_index",frames.size()-1},{"old_collision_rejects",oldRejects},{"visible_pixels",pixels["alpha_pixels"]},
              {"recovered_false_cull",oldRejects && visible && observed["new_clip_accepts"].get<bool>()},{"native",pixels},{"observed",observed}};
          }
        }
      }
      loadouts.push_back({{"material",material},{"frames",frames},{"edge",edge}});
    }
    report["resources"].push_back({{"id",resource.id},{"species",resource.species},{"skin",resource.skin},
      {"declared_backend",def->visual.backend},{"billboard",CreatureUsesBillboardSprite(*def)},
      {"collision_max",Vec(def->bounds.maxSizeBlocks)},{"sampled_states",stateNames},{"loadouts",loadouts}});
  }
  std::ofstream file(folder/"manifest.json");file<<report.dump(2)<<'\n';
  if(!file)throw std::runtime_error("Posed culling manifest failed");
}
} // namespace cutum

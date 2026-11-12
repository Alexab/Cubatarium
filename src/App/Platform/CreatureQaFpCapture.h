#pragma once
#include "App/Core.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Player/User.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Visual/CreatureAppearance.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonCache.h"
#include "Items/FpViewmodelRenderer.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ItemVisualDefaults.h"
#include "Render/Pipeline/GlStateScope.h"
#include "Render/Pipeline/GlStateMask.h"
#include "ThirdParty/stb_image_write.h"
#include "World/Core/World.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

namespace cutum
{
inline void ExportCreatureQaFpFrames(UCore &core,UWorld &world,
    const std::shared_ptr<UShaderManager> &shaders,const std::filesystem::path &directory)
{
  const char *enabled=std::getenv("CUBA_FLIGHT_QA_FP_MATRIX");
  if(!enabled || std::string(enabled)!="1")return;
  using Json=nlohmann::json;
  const auto defs=world.GetCreatureDefinitionStorage();
  const auto skins=world.GetSkinDefinitionStorage();
  const auto user=world.GetCurrentUser();
  const auto items=core.GetItemDefinitionStorage();
  if(!defs || !skins || !user || !items)throw std::runtime_error("FP QA catalogs/user missing");
  struct RestoreUser
  {
    std::shared_ptr<UUser> user;std::string skin,appearance;
    ~RestoreUser(){user->SetSelectedSkinId(skin);user->SetSelectedAppearanceTypeId(appearance);}
  } restore{user,user->GetSelectedSkinId(),user->GetSelectedAppearanceTypeId()};
  Json originalInventory;
  if(const auto *controlled=world.GetControlledCreature())controlled->GetInventory().SerializeToJson(originalInventory);
  struct Variant{const char *id,*species,*raw,*selected,*legacy;};
  const Variant variants[]={
    {"human","human","","",""},{"bot","bot","","",""},
    {"zombie","zombie","","",""},{"skeleton","skeleton","","",""},
    {"human_adventurer","human","human_adventurer","",""},
    {"human_guard","human","human_guard","",""},
    {"user_adventurer","human","","human_adventurer",""},
    {"user_guard","human","","human_guard",""},
    {"legacy_guard","human","","","human_guard"}};
  struct Loadout{const char *id,*main,*off;};
  const Loadout loadouts[]={{"bare","",""},{"main","wood_sword",""},
    {"off","","wood_torch"},{"dual","wood_sword","wood_torch"}};
  constexpr int width=768,height=432;
  const auto folder=directory/"fp";std::filesystem::create_directories(folder);
  UGlStateScope glState(kGlMaskIconFbo);
  GLuint fbo=0,color=0,depth=0;GLint oldRenderbuffer=0;
  glGetIntegerv(GL_RENDERBUFFER_BINDING,&oldRenderbuffer);
  glGenFramebuffers(1,&fbo);glGenTextures(1,&color);glGenRenderbuffers(1,&depth);
  struct Cleanup{GLuint fbo,color,depth;~Cleanup(){glDeleteRenderbuffers(1,&depth);glDeleteTextures(1,&color);glDeleteFramebuffers(1,&fbo);}} cleanup{fbo,color,depth};
  glBindFramebuffer(GL_FRAMEBUFFER,fbo);glBindTexture(GL_TEXTURE_2D,color);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
  glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,width,height);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
  glBindRenderbuffer(GL_RENDERBUFFER,oldRenderbuffer);
  if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("FP QA framebuffer incomplete");
  Json report={{"schema","creature-fp-native-matrix.v1"},
    {"scope","Actual FP renderer offscreen, real world appearance/user fallback; sampled humanoids. Not world camera/terrain/save/performance acceptance"},
    {"width",width},{"height",height},{"resources",Json::array()}};
  bool passed=true;
  const auto readVertexHash=[](const FpArmDrawAudit &arm) {
    if(!arm.VertexBuffer || !arm.VertexFloatCount)return uint64_t(0);
    GLint oldBuffer=0;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldBuffer);
    glBindBuffer(GL_ARRAY_BUFFER,arm.VertexBuffer);
    std::vector<unsigned char> bytes(arm.VertexFloatCount*sizeof(float));
    glGetBufferSubData(GL_ARRAY_BUFFER,0,static_cast<GLsizeiptr>(bytes.size()),bytes.data());
    glBindBuffer(GL_ARRAY_BUFFER,static_cast<GLuint>(oldBuffer));
    uint64_t hash=14695981039346656037ull;
    for(const auto byte:bytes){hash^=byte;hash*=1099511628211ull;}
    return hash;
  };
  for(const auto &variant:variants)
  {
    const auto *def=defs->Get(variant.species);if(!def)throw std::runtime_error("FP QA definition missing");
    user->SetSelectedSkinId(variant.selected);user->SetSelectedAppearanceTypeId(variant.legacy);
    UCreature actor(999999,variant.species,glm::vec3(0.f),glm::vec3(0.f,def->eyeHeight,0.f));
    actor.SetPlayerCharacter(true);actor.SetSkinId(variant.raw);
    const auto appearance=world.GetResolvedAppearanceShared(actor);
    const std::string expectedSkin=std::string(variant.raw).empty()?
      (std::string(variant.selected).empty()?variant.legacy:variant.selected):variant.raw;
    const auto expectedTexture=core.GetCreatureTextureStorage()->GetTexture(expectedSkin.empty()?
      std::string(variant.species)+"/diffuse":"skin/"+expectedSkin+"/diffuse");
    const bool resolved=appearance && appearance->effectiveSkinId==expectedSkin && expectedTexture!=0;
    passed=passed && resolved;
    Json resource={{"id",variant.id},{"species",variant.species},{"raw_skin",variant.raw},
      {"selected_skin",variant.selected},{"legacy_selection",variant.legacy},{"effective_skin",appearance?appearance->effectiveSkinId:""},
      {"expected_texture",expectedTexture},{"appearance_resolved",resolved},{"frames",Json::array()}};
    for(const auto &loadout:loadouts)for(const std::string action:{"idle","walk","attack"})
    {
      const int count=action=="idle"?1:action=="walk"?8:17;
      for(int phase=0;phase<count;++phase)
      {
        UFpViewmodelRenderer renderer(items,core.GetBlockDefinitionStorage(),core.GetTextureCubeStorage(),core.GetCreatureTextureStorage(),shaders);
        if(!renderer.Initialize())throw std::runtime_error("FP QA renderer initialization failed");
        InventoryEntryRef main{InventoryEntryKind::Item,loadout.main,1,std::string(loadout.main).empty()};
        InventoryEntryRef off{InventoryEntryKind::Item,loadout.off,1,std::string(loadout.off).empty()};
        renderer.Update(0.f,0.f,0.f,0.f);
        renderer.SetActiveItemHint(loadout.main);
        std::string preset;
        float duration=0.f,time=0.f;
        if(action=="attack")
        {
          preset="dig_tool";
          if(const auto *item=items->Get(loadout.main))preset=DefaultSwingPreset(*item,FpSwingKind::Melee);
          const auto *visual=items->VisualPresets().Get(preset);duration=visual?visual->Duration:.28f;
          renderer.NotifySwing(FpSwingKind::Melee);time=duration*static_cast<float>(phase)/16.f;
          renderer.Update(time,0.f,0.f,0.f);
        }
        else if(action=="walk")
        {time=(6.28318530718f/7.5f)*static_cast<float>(phase)/8.f;renderer.Update(time,0.f,0.f,3.f);}
        FpViewmodelDrawAudit audit;
        FpViewmodelDrawParams params;
        params.FramebufferW=width;params.FramebufferH=height;params.TargetFramebuffer=fbo;
        params.SpeciesId=variant.species;params.Definition=def;params.Appearance=appearance;
        params.Active=&main;params.Offhand=&off;params.Audit=&audit;
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,width,height);glClearColor(0.f,0.f,0.f,0.f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        renderer.DrawWorldOverlay(params);
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        std::vector<unsigned char> pixels(static_cast<size_t>(width)*height*4);
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        size_t coverage=0;for(size_t i=3;i<pixels.size();i+=4)if(pixels[i])++coverage;
        const size_t stride=static_cast<size_t>(width)*4;
        for(int y=0;y<height/2;++y)std::swap_ranges(pixels.begin()+y*stride,pixels.begin()+(y+1)*stride,pixels.begin()+(height-1-y)*stride);
        const auto name=std::string(variant.id)+"_"+loadout.id+"_"+action+"_"+std::to_string(phase)+".png";
        if(!stbi_write_png((folder/name).string().c_str(),width,height,4,pixels.data(),width*4))throw std::runtime_error("FP QA PNG failed");
        Json arms=Json::array();bool armTextures=!audit.Arms.empty();
        for(const auto &arm:audit.Arms)
        {
          Json matrix=Json::array();for(int c=0;c<4;++c)for(int r=0;r<4;++r)matrix.push_back(arm.Mvp[c][r]);
          Json root=Json::array();for(int c=0;c<4;++c)for(int r=0;r<4;++r)root.push_back(arm.RootMvp[c][r]);
          const uint64_t gpuHash=readVertexHash(arm);
          if(arm.VertexBuffer && arm.VertexFloatCount)
          {
            armTextures=armTextures && gpuHash==arm.MeshHash;
          }
          arms.push_back({{"left",arm.Left},{"source",arm.Source},{"texture",arm.Texture},{"mvp",matrix},{"mesh_hash",arm.MeshHash},
            {"vertex_buffer",arm.VertexBuffer},{"vertex_float_count",arm.VertexFloatCount},{"gpu_vertex_hash",gpuHash},
            {"source_scale",arm.SourceScale},{"root_mvp",root}});
          armTextures=armTextures && arm.Texture==expectedTexture;
        }
        const bool valid=audit.SkinTexture==expectedTexture && armTextures && coverage>0;
        passed=passed && valid;
        resource["frames"].push_back({{"png",name},{"loadout",loadout.id},{"main",loadout.main},{"offhand",loadout.off},
          {"action",action},{"phase",phase},{"time",time},{"duration",duration},{"preset",preset},
          {"skin_texture",audit.SkinTexture},{"coverage_pixels",coverage},{"arms",arms},{"passed",valid}});
      }
    }
    report["resources"].push_back(std::move(resource));
  }
  // Reuse one real renderer across same-path file replacement and species /
  // definition-locator changes. The phased matrix above uses fresh instances.
  {
    auto def=*defs->Get("human");
    const auto fixture=folder/"cache_fixture";std::filesystem::create_directories(fixture);
    const auto source=std::filesystem::path(def.visual.sourceDirectory)/def.visual.boneSkeleton.geometryFile;
    const auto target=fixture/std::filesystem::path(def.visual.boneSkeleton.geometryFile).filename();
    std::filesystem::copy_file(source,target,std::filesystem::copy_options::overwrite_existing);
    def.visual.sourceDirectory=std::filesystem::absolute(fixture).string();
    UFpViewmodelRenderer renderer(items,core.GetBlockDefinitionStorage(),core.GetTextureCubeStorage(),core.GetCreatureTextureStorage(),shaders);
    if(!renderer.Initialize())throw std::runtime_error("FP cache fixture renderer failed");
    renderer.Update(0.f,0.f,0.f,0.f);
    Json stages=Json::array();
    auto draw=[&](const char *name,const CreatureDefinition &definition) {
      FpViewmodelDrawAudit audit;FpViewmodelDrawParams params;
      params.FramebufferW=width;params.FramebufferH=height;params.TargetFramebuffer=fbo;
      params.SpeciesId=definition.Id;params.Definition=&definition;
      params.Appearance=std::make_shared<const ResolvedCreatureAppearance>(ResolveCreatureAppearance(*defs,*skins,definition.Id,""));
      params.Audit=&audit;renderer.DrawWorldOverlay(params);
      Json arms=Json::array();std::vector<uint64_t> hashes;
      for(const auto &arm:audit.Arms)
      {
        const auto gpu=readVertexHash(arm);const bool valid=arm.MeshHash!=0 && gpu==arm.MeshHash;
        passed=passed && valid;hashes.push_back(arm.MeshHash);
        arms.push_back({{"left",arm.Left},{"source",arm.Source},{"cpu_hash",arm.MeshHash},{"gpu_hash",gpu},{"passed",valid}});
      }
      passed=passed && arms.size()==2;
      stages.push_back({{"name",name},{"species",definition.Id},{"source_directory",definition.visual.sourceDirectory},{"arms",arms}});
      return hashes;
    };
    const auto initial=draw("initial_fixture",def);
    std::ifstream input(source);Json changed;input>>changed;bool modified=false;
    for(auto &entry:changed.items())
    {
      if(!entry.value().is_object() || !entry.value().contains("bones"))continue;
      for(auto &bone:entry.value()["bones"])
        if(bone.value("name","")=="rightArm" && bone.contains("cubes") && !bone["cubes"].empty())
        {auto &u=bone["cubes"][0]["uv"][0];u=u.get<int>()+1;modified=true;break;}
      if(modified)break;
    }
    if(!modified)throw std::runtime_error("FP cache fixture right arm absent");
    {std::ofstream file(target);file<<changed.dump(2);}
    {std::ofstream file(fixture/"geometry.changed.json");file<<changed.dump(2);}
    const auto unchanged=draw("file_changed_without_invalidation",def);
    CreatureBoneSkeletonCache::Instance().Clear();
    const auto reloaded=draw("explicit_clear",def);
    std::filesystem::copy_file(source,target,std::filesystem::copy_options::overwrite_existing);
    CreatureBoneSkeletonCache::Instance().Clear();
    const auto restored=draw("restored_fixture",def);
    const auto other=draw("species_switch",*defs->Get("skeleton"));
    const auto returned=draw("source_directory_return",*defs->Get("human"));
    const bool valid=initial.size()==2 && reloaded.size()==2 && initial==unchanged && initial==restored &&
      initial==returned && reloaded[0]!=initial[0] && reloaded[1]==initial[1] && other!=initial;
    passed=passed && valid;
    report["retained_cache"]={{"source",source.string()},{"fixture",target.string()},{"stages",stages},{"passed",valid}};
  }
  Json afterInventory;if(const auto *controlled=world.GetControlledCreature())controlled->GetInventory().SerializeToJson(afterInventory);
  report["controlled_inventory_unchanged"]=originalInventory==afterInventory;passed=passed && originalInventory==afterInventory;
  user->SetSelectedSkinId(restore.skin);user->SetSelectedAppearanceTypeId(restore.appearance);
  report["user_selection_restored"]=user->GetSelectedSkinId()==restore.skin && user->GetSelectedAppearanceTypeId()==restore.appearance;
  report["passed"]=passed;
  std::ofstream file(folder/"manifest.json");file<<report.dump(2)<<'\n';file.close();
  if(!file)throw std::runtime_error("FP manifest failed");
  if(!passed)throw std::runtime_error("FP appearance, draw coverage or inventory preservation failed");
}
} // namespace cutum

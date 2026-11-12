#pragma once

#include "Gui/Preview/CreaturePreviewRenderer.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonCache.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonHierarchy.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonModelSpace.h"
#include "Creatures/Visual/CreatureRootTransform.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Creatures/Visual/Gltf/CreatureGltfLoader.h"
#include "Items/ItemGltfTextureCache.h"
#include "App/Platform/IUPlatformPaths.h"
#include "Pose/BoneSkeleton/BoneSkeletonPoseEngine.h"
#include "Render/GlIncludes.h"
#include "Render/Pipeline/GlStateScope.h"
#include "ThirdParty/stb_image_write.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>

namespace cutum
{
inline void ProbeCreatureQaEquipmentTextureModels(const std::filesystem::path &folder)
{
  using Json=nlohmann::json;
  UGlStateScope glState(kGlMaskIconFbo);
  auto *paths=IUPlatformPaths::TryGet();
  if(!paths)throw std::runtime_error("Equipment texture probe has no asset root");
  Json report={{"schema","equipment-texture-model-isolation.v1"},
    {"scope","Actual texture IDs and unshaded source material RGBA; alternating lookups, not a cold-cache/reload/FP-render test"},
    {"models",Json::array()}};
  GLuint fbo=0;glGenFramebuffers(1,&fbo);
  int index=0;
  for(const char *material:{"copper","iron","leather"}) for(const char *slot:{"chest","arms","legs"})
  {
    const std::string item=std::string(material)+"_"+slot;
    const auto source=paths->AssetRoot()/"models/items"/item/"model.gltf";
    const auto worn=source.parent_path()/"wear.gltf";
    const auto sourceMesh=CreatureGltfLoader::LoadFromFile(source.string());
    const auto wornMesh=CreatureGltfLoader::LoadFromFile(worn.string());
    if(!sourceMesh || !wornMesh || sourceMesh->primitives.empty() || wornMesh->primitives.empty())
      throw std::runtime_error("Equipment texture probe model missing: "+item);
    GLuint sourceTex=0,wornTex=0;
    auto getSource=[&](){return ItemGltfTextureCache::Instance().Get(source,sourceMesh->primitives[0].textureStem);};
    auto getWorn=[&](){return ItemGltfTextureCache::Instance().Get(worn,wornMesh->primitives[0].textureStem);};
    if(index++%2==0){sourceTex=getSource();wornTex=getWorn();}
    else {wornTex=getWorn();sourceTex=getSource();}
    std::string text;
    if(!paths->ReadAssetText("models/items/"+item+"/model.gltf",text))
      throw std::runtime_error("Equipment texture source definition missing");
    const auto definition=Json::parse(text);
    const size_t mat=definition["meshes"][0]["primitives"][0].value("material",0u);
    const auto factor=definition["materials"][mat]["pbrMetallicRoughness"]["baseColorFactor"];
    std::array<unsigned char,4> expected{};
    for(size_t c=0;c<4;++c)expected[c]=static_cast<unsigned char>((c<factor.size()?factor[c].get<float>():1.f)*255.f);
    std::array<unsigned char,4> actual{};
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,sourceTex,0);
    const bool complete=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    if(complete)glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());
    const bool valid=sourceTex && wornTex && sourceTex!=wornTex && complete && actual==expected &&
        getWorn()==wornTex && getSource()==sourceTex;
    report["models"].push_back({{"item",item},{"source_texture",sourceTex},{"worn_texture",wornTex},
      {"source_rgba",actual},{"expected_source_rgba",expected},{"isolated_and_repeatable",valid}});
    if(!valid)
    {
      glDeleteFramebuffers(1,&fbo);
      std::ofstream stream(folder/"texture_model_isolation.json");stream<<report.dump(2)<<'\n';stream.close();
      throw std::runtime_error("Equipment texture models share/stale material cache: "+item);
    }
  }
  glDeleteFramebuffers(1,&fbo);
  std::ofstream stream(folder/"texture_model_isolation.json");stream<<report.dump(2)<<'\n';
  if(!stream)throw std::runtime_error("Equipment texture probe report failed");
}

inline void ExportCreatureQaEquipmentFrames(UCreaturePreviewRenderer &preview,
    const std::vector<std::string> &speciesIds, const std::filesystem::path &directory)
{
  const char *enabled=std::getenv("CUBA_FLIGHT_QA_EQUIPMENT_MATRIX");
  if(!enabled || std::string(enabled)!="1") return;
  const char *selectedMaterial=std::getenv("CUBA_FLIGHT_QA_EQUIPMENT_MATERIAL");
  const std::string material=selectedMaterial?selectedMaterial:"copper";
  if(material!="copper" && material!="iron" && material!="leather")throw std::runtime_error("Unknown equipment QA material");
  using Json=nlohmann::json;
  struct Resource {std::string id,species,skin;};
  std::vector<Resource> resources;
  for(const auto &id:speciesIds)
  {
    const auto *def=preview.GetSpecies()->Get(id);
    if(def && def->visual.backend=="bone_skeleton" && def->visual.boneSkeleton.animationProfile=="humanoid")
      resources.push_back({id,id,{}});
  }
  for(const auto &id:preview.GetSkins()->ListEquippable())
  {
    const auto *skin=preview.GetSkins()->Get(id);
    const bool owner=std::any_of(resources.begin(),resources.end(),[&](const Resource &r){return r.species==skin->creatureId;});
    if(owner) resources.push_back({id,skin->creatureId,id});
  }
  const auto folder=directory/"equipment";std::filesystem::create_directories(folder);
  ProbeCreatureQaEquipmentTextureModels(folder);
  auto saveTexture=[&](GLuint texture,const std::string &name) {
    if(!texture)throw std::runtime_error("Equipment QA texture missing");
    UGlStateScope glState(kGlMaskIconFbo);GLuint fbo=0;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Equipment QA FBO incomplete");
    std::vector<unsigned char> pixels(256*256*4);glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    for(int y=0;y<128;++y)std::swap_ranges(pixels.begin()+y*1024,pixels.begin()+(y+1)*1024,pixels.begin()+(255-y)*1024);
    const bool saved=stbi_write_png((folder/name).string().c_str(),256,256,4,pixels.data(),1024)!=0;
    glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);
    if(!saved)throw std::runtime_error("Equipment QA PNG failed");
  };
  Json manifest={{"schema","creature-native-equipment-matrix.v1"},
    {"material",material},{"alpha_coverage",true},
    {"supplemental_view_yaws",Json::array({0,45,90,135,180,225,270,315})},
    {"supplemental_view_phase_indices",Json::array({2,10,16})},
    {"scope","Preview attachments from the actual body pose/root; not first-person/save/world-route acceptance"},
    {"resources",Json::array()}};
  auto matrixJson=[](const glm::mat4 &matrix) {
    Json result=Json::array();for(int c=0;c<4;++c)for(int r=0;r<4;++r)result.push_back(matrix[c][r]);return result;
  };
  preview.SetTransparentBackground(true);
  for(const auto &resource:resources)
  {
    const auto &def=*preview.GetSpecies()->Get(resource.species);
    const auto mesh=CreatureBoneSkeletonCache::Instance().Load(resource.species,
        def.visual.boneSkeleton.geometryFile,def.visual.boneSkeleton.geometryId,def.visual.sourceDirectory);
    if(!mesh) throw std::runtime_error("Equipment QA mesh missing: "+resource.id);
    BoneSkeletonHierarchy hierarchy(mesh->geometry);
    auto expectedPose=[&](float time,LocomotionState state,float ratio,std::vector<glm::mat4> &bones) {
      CreatureLocomotionFacts facts;facts.state=state;
      facts.horizontalSpeed=def.locomotion.walkSpeed*ratio;
      facts.animPhase=time*def.visual.Animation.walkCycleHz*6.283185307179586f;
      const auto pose=BoneSkeletonPoseEngine::Compute(facts,def,0.f);
      bones.resize(mesh->boneMeshes.size());float floor=std::numeric_limits<float>::max();
      for(size_t b=0;b<bones.size();++b)
      {
        bones[b]=hierarchy.ComputeBoneMatrix(b,pose);
        for(const auto &cube:mesh->boneMeshes[b].cubes)
        {
          const auto model=bones[b]*cube.restLocalMatrix;const auto &v=cube.interleavedPosUv;
          for(size_t i=0;i+4<v.size();i+=5)floor=std::min(floor,(model*glm::vec4(v[i],v[i+1],v[i+2],1.f)).y);
        }
      }
      return std::max(0.f,-floor);
    };
    Json row={{"id",resource.id},{"species",resource.species},{"skin",resource.skin},{"frames",Json::array()},
      {"views",Json::array()}};
    for(auto state:{LocomotionState::Walk,LocomotionState::Run}) for(float ratio:{.22f,1.f,1.5f})
    {
      const float period=1.f/std::max(.1f,def.visual.Animation.walkCycleHz);
      float worstPhase=0.f,worstLift=-1.f;std::vector<glm::mat4> bones;
      for(int p=0;p<512;++p) {
        const float phase=p/512.f;const float lift=expectedPose(phase*period,state,ratio,bones);
        if(lift>worstLift){worstLift=lift;worstPhase=phase;}
      }
      for(int p=0;p<17;++p)
      {
        const float phase=p<16?p/16.f:worstPhase;const float time=phase*period;
        const float lift=expectedPose(time,state,ratio,bones);
        const glm::mat4 root=BuildCreatureRootMatrix(glm::vec3(0.f,lift,0.f),def.visual.modelYawOffsetDeg,0.f,
            [](){return BoneSkeletonEntityConventionMatrix();});
        for(int loadout=0;loadout<4;++loadout)
        {
          std::array<WornArmorPreviewSlot,6> armor{};
          if(loadout==0 || loadout==3) {
            const char *parts[]={"head","chest","arms","hands","legs","feet"};
            for(int i=0;i<6;++i)armor[i].ItemId=material+"_"+parts[i];
          }
          const std::string main=(loadout==1 || loadout==3)?"wood_sword":"";
          const std::string off=(loadout==2 || loadout==3)?"wood_torch":"";
          std::map<std::string,glm::mat4> anchors;float maxError=0.f;
          std::map<std::string,std::pair<glm::vec3,glm::vec3>> fitBounds;
          preview.SetAttachmentObserver([&](const std::string &name,const glm::mat4 &actual) {
            const auto bone=mesh->geometry.boneIndexByName.find(name);
            if(bone==mesh->geometry.boneIndexByName.end())throw std::runtime_error("Unknown observed equipment bone");
            const auto expected=root*bones[bone->second];
            for(int c=0;c<4;++c)for(int r=0;r<4;++r)maxError=std::max(maxError,std::abs(actual[c][r]-expected[c][r]));
            anchors[name]=actual;
          });
          preview.SetAttachmentBoundsObserver([&](const std::string &name,const glm::vec3 &low,const glm::vec3 &high)
              {fitBounds[name]={low,high};});
          preview.SetAnimationPreviewState(state);preview.SetAnimationPreviewSpeedRatio(ratio);
          const GLuint texture=preview.RenderToUniqueTexture(resource.species,resource.skin,256,35.f,15.f,time,true,
              loadout==0 || loadout==3?&armor:nullptr,&main,&off);
          preview.SetAttachmentObserver({});
          preview.SetAttachmentBoundsObserver({});
          if(!texture || anchors.empty() || maxError>5e-6f)throw std::runtime_error("Equipment QA root/pose mismatch: "+resource.id);
          if(!main.empty() && !anchors.count("rightItem"))throw std::runtime_error("Missing main-hand anchor: "+resource.id);
          if(!off.empty() && !anchors.count("leftItem"))throw std::runtime_error("Missing offhand anchor: "+resource.id);
          if(fitBounds.size()!=anchors.size())throw std::runtime_error("Equipment did not use anatomical fit bounds: "+resource.id);
          const std::string stateName=LocomotionStateCatalogKey(state);
          const std::string name=resource.id+"_"+stateName+"_"+std::to_string(ratio)+"_"+std::to_string(p)+"_"+std::to_string(loadout)+".png";
          saveTexture(texture,name);
          Json actual=Json::object();for(const auto &anchor:anchors)actual[anchor.first]=matrixJson(anchor.second);
          Json localBounds=Json::object();
          for(const auto &bound:fitBounds)localBounds[bound.first]={{"low",{bound.second.first.x,bound.second.first.y,bound.second.first.z}},
              {"high",{bound.second.second.x,bound.second.second.y,bound.second.second.z}}};
          row["frames"].push_back({{"state",stateName},{"speed_ratio",ratio},{"phase",phase},{"phase_index",p},
            {"worst_clearance_phase",p==16},{"loadout",loadout},{"root_lift",lift},{"max_anchor_matrix_error",maxError},
            {"anchors",actual},{"fit_bounds",localBounds},{"png",name}});
          // Supplemental views expose rear/side cavities and minification at
          // both opposite strides and the deepest-clearance phase. The core
          // matrix above remains the dense anchor audit; these are art views.
          if(state==LocomotionState::Run && ratio==1.5f && (p==2 || p==10 || p==16))
          {
            for(int yaw=0;yaw<360;yaw+=45)
            {
              const std::string viewName=name.substr(0,name.size()-4)+"_yaw_"+std::to_string(yaw)+".png";
              const GLuint viewTexture=preview.RenderToUniqueTexture(resource.species,resource.skin,256,
                  static_cast<float>(yaw),15.f,time,true,
                  loadout==0 || loadout==3?&armor:nullptr,&main,&off);
              saveTexture(viewTexture,viewName);
              row["views"].push_back({{"state",stateName},{"speed_ratio",ratio},{"phase",phase},
                {"phase_index",p},{"loadout",loadout},{"yaw",yaw},{"png",viewName}});
            }
          }
        }
      }
    }
    manifest["resources"].push_back(std::move(row));
  }
  preview.SetAttachmentObserver({});preview.SetAttachmentBoundsObserver({});preview.SetAnimationPreviewSpeedRatio(1.f);
  preview.SetTransparentBackground(false);
  std::ofstream stream(folder/"manifest.json");stream<<manifest.dump(2)<<'\n';
  if(!stream)throw std::runtime_error("Equipment QA manifest failed");
}
} // namespace cutum

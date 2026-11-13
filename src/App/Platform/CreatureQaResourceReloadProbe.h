#pragma once
#include "App/Platform/CreatureQaTextureMappingProbe.h"
#include "ResourcePacks/ResourcePackResolver.h"
#include "ThirdParty/stb_image.h"
#include <map>

namespace cutum
{
namespace creature_reload_qa
{
using Json=nlohmann::json;
using Sources=std::map<std::string,std::filesystem::path>;

inline uint64_t Hash(const unsigned char *data,size_t size)
{
  uint64_t result=14695981039346656037ull;
  for(size_t i=0;i<size;++i){result^=data[i];result*=1099511628211ull;}
  return result;
}

inline Sources TextureSources(const std::filesystem::path &creatures,const std::filesystem::path &skins)
{
  Sources result;
  for(const auto &root:{creatures,skins})
  {
    if(!std::filesystem::exists(root))continue;
    for(const auto &folder:std::filesystem::directory_iterator(root))
    {
      const auto directory=folder.path()/"textures";
      if(!folder.is_directory() || !std::filesystem::exists(directory))continue;
      for(const auto &file:std::filesystem::directory_iterator(directory))
        if(file.is_regular_file() && file.path().extension()==".png")
          result[(root==skins?"skin/":"")+folder.path().filename().string()+"/"+file.path().stem().string()]=file.path();
    }
  }
  return result;
}

inline std::filesystem::path TexturePath(const std::filesystem::path &root,const std::string &key)
{
  const bool skin=key.rfind("skin/",0)==0;
  const auto local=skin?key.substr(5):key;
  const auto slash=local.find('/');
  if(slash==std::string::npos)throw std::runtime_error("Reload probe invalid texture key");
  return root/(skin?"skins":"creatures")/local.substr(0,slash)/"textures"/(local.substr(slash+1)+".png");
}

inline void WriteColor(const std::filesystem::path &path,const std::string &key,unsigned variant)
{
  const uint64_t h=Hash(reinterpret_cast<const unsigned char *>(key.data()),key.size());
  std::array<unsigned char,64> rgba{};
  const std::array<unsigned char,4> color={static_cast<unsigned char>(23+(h+variant*67)%211),
    static_cast<unsigned char>(19+((h>>8)+variant*41)%223),
    static_cast<unsigned char>(29+((h>>16)+variant*83)%199),255};
  for(size_t i=0;i<16;++i)std::copy(color.begin(),color.end(),rgba.begin()+i*4);
  std::filesystem::create_directories(path.parent_path());
  if(!stbi_write_png(path.string().c_str(),4,4,4,rgba.data(),16))throw std::runtime_error("Reload probe PNG failed");
}

inline Json ReadGpu(UCreatureTextureStorage &textures,const std::string &key,const std::filesystem::path &source)
{
  const GLuint texture=textures.GetTexture(key);
  int width=0,height=0,channels=0;
  auto *pixels=stbi_load(source.string().c_str(),&width,&height,&channels,4);
  if(!pixels || width<=0 || height<=0 || !texture)throw std::runtime_error("Reload probe source/texture missing: "+key);
  const size_t size=static_cast<size_t>(width)*height*4;
  const uint64_t expected=Hash(pixels,size);stbi_image_free(pixels);
  UGlStateScope state(kGlMaskIconFbo);GLuint fbo=0;glGenFramebuffers(1,&fbo);
  glBindFramebuffer(GL_FRAMEBUFFER,fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
  if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Reload probe GPU framebuffer incomplete");
  std::vector<unsigned char> actual(size);
  glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());glDeleteFramebuffers(1,&fbo);
  const uint64_t observed=Hash(actual.data(),actual.size());
  return {{"key",key},{"source",source.string()},{"texture",texture},{"width",width},{"height",height},
    {"source_rgba_fnv64",expected},{"gpu_rgba_fnv64",observed},{"passed",observed==expected}};
}

struct Actor
{
  std::string id,species,skin;
  CreatureDefinition definition;
  std::unique_ptr<UCreature> creature;
  std::unique_ptr<IUCreatureVisual> visual;
  CreatureLocomotionFacts facts;
  CreatureQaResourceDrawSnapshot baseline;
  std::vector<std::string> keys;
};

inline Actor MakeActor(const CreatureDefinition &def,const ResolvedCreatureAppearance &appearance,
                       std::string id,std::string skin,float dt=.1375f)
{
  Actor actor;actor.id=std::move(id);actor.species=def.Id;actor.skin=std::move(skin);actor.definition=def;
  actor.creature=std::make_unique<UCreature>(1,def.Id,glm::vec3(0.f),glm::vec3(0.f,def.eyeHeight,0.f));
  actor.creature->SetSkinId(actor.skin);
  actor.creature->GetBoundsMutable().profile=def.bounds;
  actor.creature->GetBoundsMutable().currentSizeBlocks=def.bounds.restSizeBlocks;
  actor.facts.state=LocomotionState::Walk;actor.facts.horizontalSpeed=def.locomotion.walkSpeed;actor.facts.animPhase=.375f;
  actor.visual=CreateCreatureVisual(def);actor.visual->SetAppearance(appearance);
  actor.visual->UpdatePose(*actor.creature,actor.facts,CreaturePoseParams{},actor.definition,dt);
  return actor;
}

inline CreatureQaResourceDrawSnapshot Draw(Actor &actor,UCreatureTextureStorage &textures)
{
  RenderSettings settings;settings.CreatureTexturedParts=true;settings.CreatureWireframeOverlay=false;
  CreatureDrawQueue queue;CreatureDrawContext context;
  context.Queue=&queue;context.Textures=&textures;context.Settings=&settings;
  actor.visual->SubmitDraw(context,glm::mat4(1.f));
  CreatureQaResourceDrawSnapshot result;queue.Flush(result);
  std::sort(result.geometryHashes.begin(),result.geometryHashes.end());return result;
}
} // namespace creature_reload_qa

inline void ExportCreatureQaResourceReloadProbe(UCreaturePreviewRenderer &preview,
    const std::vector<std::string> &ids,const std::filesystem::path &directory)
{
  const char *enabled=std::getenv("CUBA_FLIGHT_QA_RESOURCE_RELOAD_PROBE");
  if(!enabled || std::string(enabled)!="1")return;
  using namespace creature_reload_qa;
  namespace fs=std::filesystem;
  const fs::path creatures=fs::absolute(CreatureGltfCache::Instance().GetCreaturesRoot()).lexically_normal();
  const auto skins=creatures.parent_path()/"skins";
  const auto fixture=fs::absolute(directory/"resource_reload_fixture").lexically_normal();
  fs::create_directories(fixture/"empty/creatures");fs::create_directories(fixture/"empty/skins");
  if(ids.size()!=56 || preview.GetSkins()->Count()!=5)throw std::runtime_error("Reload probe requires production56+5");
  struct RestoreRoots
  {
    std::string bone,gltf;
    ~RestoreRoots(){CreatureBoneSkeletonCache::Instance().SetCreaturesRoot(bone);CreatureGltfCache::Instance().SetCreaturesRoot(gltf);CreatureRigidModelCache::Instance().Clear();}
  } restore{CreatureBoneSkeletonCache::Instance().GetCreaturesRoot(),CreatureGltfCache::Instance().GetCreaturesRoot()};
  UCreatureTextureStorage textures;
  struct Cleanup{UCreatureTextureStorage &textures;~Cleanup(){textures.LoadFromCreatureAndSkinRoots("","");}} cleanup{textures};
  textures.LoadFromCreatureAndSkinRoots(creatures.string(),skins.string());
  const auto baseSources=TextureSources(creatures,skins);
  std::map<GLuint,std::string> names;
  for(const auto &entry:baseSources)names[textures.GetTexture(entry.first)]=entry.first;
  Json report={{"schema","creature-resource-reload-probe.v1"},
    {"scope","Native retained61 texture/pack reload,52 negative mesh recovery,3 rigid recovery; fixed sampled pose, not art/FP/save/performance or full UI acceptance"},
    {"resources",Json::array()},{"texture_stages",Json::array()},{"mesh_stages",Json::array()},
    {"rigid_recovery",Json::array()},{"source_copies",Json::array()}};
  bool passed=true;
  std::vector<Actor> actors;actors.reserve(61);
  for(const auto &id:ids)
  {
    const auto *def=preview.GetSpecies()->Get(id);if(!def)throw std::runtime_error("Reload species missing");
    actors.push_back(MakeActor(*def,ResolveCreatureAppearance(*preview.GetSpecies(),*preview.GetSkins(),id,""),id,""));
  }
  for(const auto &skin:preview.GetSkins()->ListEquippable())
  {
    const auto *s=preview.GetSkins()->Get(skin);const auto *def=s?preview.GetSpecies()->Get(s->creatureId):nullptr;
    if(!def)throw std::runtime_error("Reload skin owner missing");
    actors.push_back(MakeActor(*def,ResolveCreatureAppearance(*preview.GetSpecies(),*preview.GetSkins(),def->Id,skin),skin,skin));
  }
  for(auto &actor:actors)
  {
    actor.baseline=Draw(actor,textures);
    for(const auto texture:actor.baseline.textures)actor.keys.push_back(names.at(texture));
    if(actor.keys.empty())throw std::runtime_error("Reload baseline has no draws");
    report["resources"].push_back({{"id",actor.id},{"species",actor.species},{"skin",actor.skin},
      {"backend",actor.definition.visual.backend},{"runtime_backend",CreatureUsesBillboardSprite(actor.definition)?"sprite":actor.definition.visual.backend},
      {"keys",actor.keys},{"draws",actor.baseline.draws},{"geometry_hashes",actor.baseline.geometryHashes}});
  }
  const auto copyFile=[&](const fs::path &source,const fs::path &target) {
    fs::create_directories(target.parent_path());fs::copy_file(source,target,fs::copy_options::overwrite_existing);
    report["source_copies"].push_back({{"source",source.string()},{"target",target.string()}});
  };
  auto payloadFiles=[](const CreatureDefinition &def) {
    std::vector<fs::path> files;
    if(def.visual.backend=="bone_skeleton")files.push_back(def.visual.boneSkeleton.geometryFile);
    else if(def.visual.backend=="gltf_skeleton")
    {
      files.push_back(def.visual.gltf.modelPath);
      std::ifstream file(fs::path(def.visual.sourceDirectory)/def.visual.gltf.modelPath);Json model;file>>model;
      for(const auto &buffer:model["buffers"])files.push_back(buffer["uri"].get<std::string>());
    }
    else if(!def.visual.rigidModelPath.empty())files.push_back(def.visual.rigidModelPath);
    // Human appearance selects a matched optional mesh, not the legacy
    // definition locator. A complete pack/recovery fixture must carry it.
    const auto appearance=fs::path(def.visual.sourceDirectory)/"appearance";
    if(def.Id=="human" && fs::exists(appearance))
      for(const auto &file:fs::directory_iterator(appearance))
        if(file.is_regular_file())files.push_back(fs::path("appearance")/file.path().filename());
    for(const auto &file:files)
      if(file.is_absolute() || file.lexically_normal().generic_string().find("..")!=std::string::npos)
        throw std::runtime_error("Unsafe diagnostic payload path");
    return files;
  };
  auto syntheticSources=[&](const fs::path &root,unsigned variant) {
    Sources sources;
    for(const auto &entry:baseSources)
    {const auto path=TexturePath(root,entry.first);WriteColor(path,entry.first,variant);sources[entry.first]=path;}
    return sources;
  };
  auto stage=[&](const std::string &name,const Sources &sources,bool missing=false,Json extra=Json::object()) {
    Json rows=Json::array();std::map<std::string,bool> used;bool valid=true;
    for(auto &actor:actors)
    {
      const auto actual=Draw(actor,textures);std::vector<GLuint> expected;
      if(!missing)for(const auto &key:actor.keys){expected.push_back(textures.GetTexture(key));used[key]=true;}
      std::sort(expected.begin(),expected.end());
      const bool good=missing?actual.draws==0:actual.draws==actor.baseline.draws &&
        actual.textures==expected && std::all_of(expected.begin(),expected.end(),[](GLuint t){return t!=0;}) &&
        actual.geometryHashes==actor.baseline.geometryHashes;
      valid=valid && good;
      rows.push_back({{"id",actor.id},{"draws",actual.draws},{"textures",actual.textures},{"expected_textures",expected},
        {"definition_source_directory",actor.definition.visual.sourceDirectory},
        {"geometry_hashes",actual.geometryHashes},{"passed",good}});
    }
    Json gpu=Json::array();
    for(const auto &entry:used){auto sample=ReadGpu(textures,entry.first,sources.at(entry.first));valid=valid && sample["passed"].get<bool>();gpu.push_back(std::move(sample));}
    extra["name"]=name;extra["missing"]=missing;extra["resources"]=std::move(rows);extra["gpu_textures"]=std::move(gpu);extra["passed"]=valid;
    report["texture_stages"].push_back(std::move(extra));passed=passed && valid;
  };
  stage("baseline",baseSources);
  std::vector<GLuint> originalTextures;for(const auto &entry:names)if(entry.first)originalTextures.push_back(entry.first);
  textures.LoadFromCreatureAndSkinRoots((fixture/"empty/creatures").string(),(fixture/"empty/skins").string());
  const bool deleted=std::all_of(originalTextures.begin(),originalTextures.end(),[](GLuint t){return glIsTexture(t)==GL_FALSE;});
  stage("missing_textures",{},true,{{"old_gpu_names_deleted",deleted}});passed=passed && deleted;
  textures.MergeCreatureRoot(creatures.string());textures.MergeSkinRoot(skins.string());stage("merge_recovery",baseSources);
  const auto rootB=fixture/"texture_root_b";const auto alternateSources=syntheticSources(rootB,1);
  textures.LoadFromCreatureAndSkinRoots((rootB/"creatures").string(),(rootB/"skins").string());stage("texture_root_swap",alternateSources);
  textures.LoadFromCreatureAndSkinRoots(creatures.string(),skins.string());stage("texture_root_return",baseSources);

  const auto assetRoot=fixture/"pack_assets";
  std::map<std::string,Sources> packSources;
  for(unsigned index=0;index<2;++index)
  {
    const auto id=index==0?"qa_reload_a":"qa_reload_b";const auto root=assetRoot/"resource_packs"/id;
    fs::create_directories(root);
    {std::ofstream file(root/"pack.json");file<<Json({{"id",id},{"name",id},{"license","CC0-1.0"},{"priority",0},{"resolution",16},{"worldgen_role","secondary"}}).dump(2);}
    for(const auto &sid:ids)
    {
      const auto &def=*preview.GetSpecies()->Get(sid);
      copyFile(fs::path(def.visual.sourceDirectory)/"creature.json",root/"creatures"/sid/"creature.json");
      for(const auto &file:payloadFiles(def))copyFile(fs::path(def.visual.sourceDirectory)/file,root/"creatures"/sid/file);
    }
    for(const auto &skin:preview.GetSkins()->ListEquippable())copyFile(skins/skin/"skin.json",root/"skins"/skin/"skin.json");
    packSources[id]=syntheticSources(root,index+2);
  }
  UCreatureDefinitionStorage definitions;USkinDefinitionStorage skinDefinitions;
  auto packStage=[&](const std::string &name,const ResourcePackSelection &selection,const std::string &winner) {
    const auto packs=UResourcePackResolver{}.Resolve(selection,assetRoot,fixture/"no_writable_packs");
    Json order=Json::array();for(const auto &p:packs)order.push_back({{"id",p.Id},{"priority",p.EffectivePriority},{"selection_index",p.SelectionIndex}});
    const auto before=definitions.Revision.load();
    ApplyCreaturePackOverlays(definitions,textures,&skinDefinitions,creatures,skins,packs);
    bool directories=true;
    for(auto &actor:actors)
    {
      const auto *def=definitions.Get(actor.species);if(!def)throw std::runtime_error("Pack reload species lost");
      const auto expected=winner.empty()?creatures/actor.species:assetRoot/"resource_packs"/winner/"creatures"/actor.species;
      directories=directories && fs::path(def->visual.sourceDirectory)==expected;
      actor.definition=*def;
      actor.visual->SetAppearance(ResolveCreatureAppearance(definitions,skinDefinitions,actor.species,actor.skin));
      actor.visual->UpdatePose(*actor.creature,actor.facts,CreaturePoseParams{},actor.definition,0.f);
    }
    const bool revision=definitions.Revision.load()>before;passed=passed && directories && revision;
    stage(name,winner.empty()?baseSources:packSources.at(winner),false,
      {{"selection_primary",selection.Primary},{"selection_secondary",selection.Secondary},{"resolved_order",order},
       {"winner",winner},{"source_directories_correct",directories},{"catalog_revision_changed",revision}});
  };
  ResourcePackSelection ab;ab.Secondary={"qa_reload_a","qa_reload_b"};packStage("pack_selection_ab",ab,"qa_reload_a");
  ResourcePackSelection ba;ba.Secondary={"qa_reload_b","qa_reload_a"};packStage("pack_selection_ba",ba,"qa_reload_b");
  ResourcePackSelection tier;tier.Primary={"qa_reload_a"};tier.Secondary={"qa_reload_b"};packStage("pack_tier",tier,"qa_reload_b");
  packStage("pack_removed",{},"");packStage("pack_repeat",ab,"qa_reload_a");packStage("pack_final_base",{},"");

  // Prove the limited legacy path separately from complete v2 packs. Its
  // six rigid cubes and diffuse atlas intentionally differ from the v2 rig.
  const auto humanAppearance=assetRoot/"resource_packs/qa_reload_a/creatures/human/appearance";
  const auto hiddenAppearance=humanAppearance.parent_path()/"appearance_disabled";
  fs::rename(humanAppearance,hiddenAppearance);
  {
    const auto packs=UResourcePackResolver{}.Resolve(ab,assetRoot,fixture/"no_writable_packs");
    ApplyCreaturePackOverlays(definitions,textures,&skinDefinitions,creatures,skins,packs);
    Json rows=Json::array();bool valid=true;
    for(auto &actor:actors)if(actor.species=="human") {
      const auto *def=definitions.Get(actor.species);
      const auto resolved=ResolveCreatureAppearance(definitions,skinDefinitions,actor.species,actor.skin);
      actor.definition=*def;actor.visual->SetAppearance(resolved);
      actor.visual->UpdatePose(*actor.creature,actor.facts,CreaturePoseParams{},actor.definition,0.f);
      const auto actual=Draw(actor,textures);
      auto fresh=MakeActor(*def,resolved,actor.id,actor.skin,0.f);
      const auto expected=Draw(fresh,textures);
      const bool good=resolved.geometryFileOverride.empty() && actual.draws==6 && actual.hash==expected.hash &&
        !actual.textures.empty() && std::all_of(actual.textures.begin(),actual.textures.end(),[](GLuint t){return t!=0;});
      rows.push_back({{"id",actor.id},{"draws",actual.draws},{"fresh_draws",expected.draws},{"passed",good}});valid=valid && good;
    }
    report["legacy_human_fallback"]={{"resources",rows},{"passed",valid}};passed=passed && valid;
  }
  fs::rename(hiddenAppearance,humanAppearance);
  packStage("legacy_restored_base",{},"");

  // Negative-cache recovery uses the root lookup, deliberately clearing the
  // definition locator. Files appearing alone must not trigger frame-by-frame
  // reads; an explicit content generation change makes the live visual retry.
  const auto missingRoot=fixture/"missing_mesh/creatures";fs::create_directories(missingRoot);
  auto &boneCache=CreatureBoneSkeletonCache::Instance();auto &gltfCache=CreatureGltfCache::Instance();
  boneCache.SetCreaturesRoot(missingRoot.string());gltfCache.SetCreaturesRoot(missingRoot.string());
  std::vector<Actor> missingActors;missingActors.reserve(52);
  for(const auto &id:ids)
  {
    auto def=*preview.GetSpecies()->Get(id);if(def.visual.backend!="bone_skeleton" && def.visual.backend!="gltf_skeleton")continue;
    def.visual.sourceDirectory.clear();
    missingActors.push_back(MakeActor(def,ResolveCreatureAppearance(*preview.GetSpecies(),*preview.GetSkins(),id,""),id,"",0.f));
  }
  auto meshStage=[&](const std::string &name,bool absent) {
    Json rows=Json::array();bool valid=true;
    for(auto &actor:missingActors)
    {
      actor.visual->UpdatePose(*actor.creature,actor.facts,CreaturePoseParams{},actor.definition,0.f);
      const auto actual=Draw(actor,textures);
      auto fresh=MakeActor(actor.definition,ResolveCreatureAppearance(*preview.GetSpecies(),*preview.GetSkins(),actor.species,""),actor.id,"",0.f);
      const auto reference=Draw(fresh,textures);
      const bool good=absent?actual.draws==0 && reference.draws==0:actual.draws>0 && actual.hash==reference.hash && actual.meshes==reference.meshes;
      valid=valid && good;
      rows.push_back({{"id",actor.id},{"draws",actual.draws},{"fresh_draws",reference.draws},
        {"definition_source_directory",actor.definition.visual.sourceDirectory},
        {"live_hash",actual.hash},{"fresh_hash",reference.hash},{"fresh_meshes",actual.meshes==reference.meshes},{"passed",good}});
    }
    report["mesh_stages"].push_back({{"name",name},{"expected_absent",absent},
      {"bone_root",boneCache.GetCreaturesRoot()},{"gltf_root",gltfCache.GetCreaturesRoot()},
      {"resources",rows},{"passed",valid}});passed=passed && valid;
  };
  meshStage("missing_initial",true);
  for(const auto &actor:missingActors)
  {
    const auto &def=*preview.GetSpecies()->Get(actor.species);
    for(const auto &file:payloadFiles(def))copyFile(fs::path(def.visual.sourceDirectory)/file,missingRoot/actor.species/file);
  }
  meshStage("files_appeared_without_invalidation",true);
  boneCache.Clear();gltfCache.Clear();meshStage("explicit_clear_recovery",false);
  const auto emptyRoot=fixture/"empty_mesh_root";fs::create_directories(emptyRoot);
  boneCache.SetCreaturesRoot(emptyRoot.string());gltfCache.SetCreaturesRoot(emptyRoot.string());meshStage("root_missing",true);
  boneCache.SetCreaturesRoot(creatures.string());gltfCache.SetCreaturesRoot(creatures.string());meshStage("root_return",false);

  // All three actual rigid model files: absent -> loaded -> same-path edited
  // through the real pack entry point -> exact original payload restored.
  const auto rigidRoot=fixture/"rigid_recovery/creatures";fs::create_directories(rigidRoot);
  for(const auto &id:ids)
  {
    const auto &def=*preview.GetSpecies()->Get(id);
    if(def.visual.rigidModelPath.empty() || CreatureUsesBillboardSprite(def))continue;
    copyFile(fs::path(def.visual.sourceDirectory)/"creature.json",rigidRoot/id/"creature.json");
  }
  CreatureRigidModelCache::Instance().Clear();UCreatureDefinitionStorage rigidDefs;rigidDefs.Load(rigidRoot.string());
  UCreatureTextureStorage unusedTextures;
  for(const auto &id:ids)
  {
    const auto &original=*preview.GetSpecies()->Get(id);
    if(original.visual.rigidModelPath.empty() || CreatureUsesBillboardSprite(original))continue;
    const auto target=rigidRoot/id/original.visual.rigidModelPath;
    const auto *missing=rigidDefs.Get(id);if(!missing)throw std::runtime_error("Rigid recovery definition absent");
    auto actor=MakeActor(*missing,ResolveCreatureAppearance(rigidDefs,*preview.GetSkins(),id,""),id,"");
    const auto absent=Draw(actor,textures);const bool placeholder=missing->visual.Parts.empty() && absent.draws==1;
    copyFile(fs::path(original.visual.sourceDirectory)/original.visual.rigidModelPath,target);
    rigidDefs.Load(rigidRoot.string());const auto loaded=*rigidDefs.Get(id);
    actor.visual->SetAppearance(ResolveCreatureAppearance(rigidDefs,*preview.GetSkins(),id,""));actor.definition=loaded;
    actor.visual->UpdatePose(*actor.creature,actor.facts,CreaturePoseParams{},actor.definition,0.f);
    const auto recovered=Draw(actor,textures);
    auto originalActor=MakeActor(original,ResolveCreatureAppearance(*preview.GetSpecies(),*preview.GetSkins(),id,""),id,"");
    const auto originalDraw=Draw(originalActor,textures);
    std::ifstream input(target);Json model;input>>model;
    const float before=model["parts"][0]["size"][0].get<float>();model["parts"][0]["size"][0]=before*1.125f;
    {std::ofstream file(target);file<<model.dump(2);}
    const auto changedFile=fixture/"rigid_changes"/id/"changed_model.json";fs::create_directories(changedFile.parent_path());
    {std::ofstream file(changedFile);file<<model.dump(2);}
    ApplyCreaturePackOverlays(rigidDefs,unusedTextures,nullptr,rigidRoot,fixture/"empty/skins",{});
    const auto modified=*rigidDefs.Get(id);actor.definition=modified;
    actor.visual->SetAppearance(ResolveCreatureAppearance(rigidDefs,*preview.GetSkins(),id,""));
    actor.visual->UpdatePose(*actor.creature,actor.facts,CreaturePoseParams{},actor.definition,0.f);
    const auto changed=Draw(actor,textures);
    auto fresh=MakeActor(modified,ResolveCreatureAppearance(rigidDefs,*preview.GetSkins(),id,""),id,"");const auto freshDraw=Draw(fresh,textures);
    fs::copy_file(fs::path(original.visual.sourceDirectory)/original.visual.rigidModelPath,target,fs::copy_options::overwrite_existing);
    ApplyCreaturePackOverlays(rigidDefs,unusedTextures,nullptr,rigidRoot,fixture/"empty/skins",{});
    actor.definition=*rigidDefs.Get(id);actor.visual->SetAppearance(ResolveCreatureAppearance(rigidDefs,*preview.GetSkins(),id,""));
    actor.visual->UpdatePose(*actor.creature,actor.facts,CreaturePoseParams{},actor.definition,0.f);const auto restored=Draw(actor,textures);
    const bool good=placeholder && recovered.hash==originalDraw.hash && changed.hash==freshDraw.hash &&
      changed.geometryHashes!=recovered.geometryHashes && restored.hash==originalDraw.hash &&
      std::abs(modified.visual.Parts[0].sizeBlocks.x-before*1.125f)<1e-6f;
    passed=passed && good;
    report["rigid_recovery"].push_back({{"id",id},{"placeholder_draws",absent.draws},{"placeholder_parts_empty",placeholder},
      {"recovered_hash",recovered.hash},{"original_hash",originalDraw.hash},{"changed_hash",changed.hash},{"fresh_hash",freshDraw.hash},
      {"restored_hash",restored.hash},{"before_width",before},{"changed_width",modified.visual.Parts[0].sizeBlocks.x},
      {"changed_geometry",changed.geometryHashes!=recovered.geometryHashes},{"changed_fixture",changedFile.string()},{"passed",good}});
  }
  UCreatureDefinitionStorage emptyCatalog;emptyCatalog.Load(creatures.string());const auto before=emptyCatalog.Revision.load();
  emptyCatalog.Load((fixture/"empty/creatures").string());
  const bool cleared=emptyCatalog.Count()==0 && emptyCatalog.Revision.load()>before;passed=passed && cleared;
  report["empty_catalog"]={{"count",emptyCatalog.Count()},{"revision_before",before},{"revision_after",emptyCatalog.Revision.load()},{"passed",cleared}};
  report["passed"]=passed;
  std::ofstream file(directory/"resource_reload_probe.json");file<<report.dump(2)<<'\n';file.close();
  if(!file)throw std::runtime_error("Resource reload report write failed");
  if(!passed)throw std::runtime_error("Resource reload, recovery or pack priority failed");
}
} // namespace cutum

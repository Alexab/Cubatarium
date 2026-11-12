#pragma once
#include "Creatures/Core/Creature.h"
#include "Creatures/Diagnostics/CreatureQaWorldMotion.h"
#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include "Creatures/Visual/Gltf/CreatureGltfClipClock.h"
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace cutum
{
// QA only: observe actual submitted badger skin/root, never change a pose.
// Disk IO here is deliberately excluded from performance benchmark runs.
inline const std::string &CreatureQaFootProbePath()
{
  static const std::string path=[]{
    const char *p=std::getenv("CUBA_CREATURE_QA_FOOT_PROBE_PATH");
    return p?std::string(p):std::string();
  }();
  return path;
}
struct CreatureQaFootMemo
{
  uint64_t id{0}; double elapsed{0}; float dt{0}, yaw{0};
  CreatureLocomotionFacts facts; glm::vec3 origin{0};
  std::string clip,fade; float time{0},fadeTime{0},fadeWeight{0};
};
inline std::unordered_map<const void *,CreatureQaFootMemo> &CreatureQaFootMemos()
{
  static std::unordered_map<const void *,CreatureQaFootMemo> memos;
  return memos;
}
inline void RememberCreatureQaFootPose(const void *visual,const UCreature &actor,
    const CreatureLocomotionFacts &facts,float dt,float yaw,const UGltfClipClock &clock)
{
  if(CreatureQaFootProbePath().empty())return;
  auto &memos=CreatureQaFootMemos();
  if(actor.GetSpawnSource()!=CreatureSpawnSource::Qa || actor.GetTypeId()!="badger")
  {memos.erase(visual);return;}
  auto &m=memos[visual];
  if(m.id!=actor.GetId())m={};
  m.id=actor.GetId();m.elapsed+=std::max(0.f,dt);m.dt=dt;m.yaw=yaw;
  m.facts=facts;m.origin=actor.GetFeetPosition();
  m.clip=clock.GetClip();m.time=clock.GetTime();m.fade=clock.GetFadeClip();
  m.fadeTime=clock.GetFadeTime();m.fadeWeight=clock.GetFadeWeight();
}
struct CreatureQaFootVertex {size_t primitive{0},vertex{0};};
struct CreatureQaFootSpec
{
  std::string bone; std::vector<CreatureQaFootVertex> vertices,sole,surface;
  std::vector<size_t> memberJoints;
};
struct CreatureQaFootAsset
{
  std::shared_ptr<const CreatureGltfMeshAsset> owner;
  std::vector<CreatureQaFootSpec> feet;
};
inline const CreatureQaFootAsset &CreatureQaFootGeometry(std::shared_ptr<const CreatureGltfMeshAsset> asset)
{
  static std::unordered_map<const CreatureGltfMeshAsset *,CreatureQaFootAsset> cache;
  const auto found=cache.find(asset.get());if(found!=cache.end())return found->second;
  if(cache.size()>=8)throw std::runtime_error("QA foot probe model limit");
  CreatureQaFootAsset result;result.owner=asset;
  for(const char *name:{"lower_fl","lower_fr","lower_bl","lower_br"})
  {
    int joint=-1;
    for(size_t j=0;j<asset->skin.jointNodes.size();++j)
      if(asset->nodes[asset->skin.jointNodes[j]].name==name)joint=static_cast<int>(j);
    if(joint<0)throw std::runtime_error("QA foot probe missing semantic badger joint");
    CreatureQaFootSpec foot;foot.bone=name;
    const int rootNode=asset->skin.jointNodes[static_cast<size_t>(joint)];
    for(size_t j=0;j<asset->skin.jointNodes.size();++j)
    {
      int node=asset->skin.jointNodes[j];size_t depth=0;
      while(node>=0)
      {
        if(node==rootNode){foot.memberJoints.push_back(j);break;}
        if(++depth>asset->nodes.size())throw std::runtime_error("QA foot probe cyclic hierarchy");
        node=asset->nodes.at(static_cast<size_t>(node)).parent;
      }
    }
    float bottom=std::numeric_limits<float>::infinity();
    for(size_t p=0;p<asset->primitives.size();++p)
    {
      const auto &prim=asset->primitives[p];
      for(size_t v=0;v<prim.mesh.interleavedPosUv.size()/5;++v)
      {
        float memberWeight=0.f;
        for(size_t k=0;k<4;++k)
          for(size_t member:foot.memberJoints)
            if(prim.jointIndices.at(v*4+k)==member)memberWeight+=prim.jointWeights.at(v*4+k);
        if(memberWeight<.9999f)continue;
        foot.vertices.push_back({p,v});bottom=std::min(bottom,prim.mesh.interleavedPosUv[v*5+1]);
      }
    }
    for(const auto &ref:foot.vertices)
    {
      const auto &data=asset->primitives[ref.primitive].mesh.interleavedPosUv;
      bool duplicate=false;
      for(const auto &old:foot.surface)
      {
        const auto &b=asset->primitives[old.primitive].mesh.interleavedPosUv;
        bool sameSkin=true;
        const auto &aPrim=asset->primitives[ref.primitive];const auto &bPrim=asset->primitives[old.primitive];
        for(size_t k=0;k<4;++k)
          if(aPrim.jointIndices.at(ref.vertex*4+k)!=bPrim.jointIndices.at(old.vertex*4+k) ||
             aPrim.jointWeights.at(ref.vertex*4+k)!=bPrim.jointWeights.at(old.vertex*4+k))sameSkin=false;
        if(sameSkin && glm::length(glm::vec3(data[ref.vertex*5]-b[old.vertex*5],
            data[ref.vertex*5+1]-b[old.vertex*5+1],data[ref.vertex*5+2]-b[old.vertex*5+2]))<1e-6f)duplicate=true;
      }
      if(!duplicate)
      {
        foot.surface.push_back(ref);
        if(std::abs(data[ref.vertex*5+1]-bottom)<=1e-6f)foot.sole.push_back(ref);
      }
    }
    if(foot.sole.size()<3)throw std::runtime_error("QA foot probe sole geometry absent");
    result.feet.push_back(std::move(foot));
  }
  return cache.emplace(asset.get(),std::move(result)).first->second;
}
inline glm::vec3 CreatureQaSkinnedWorldVertex(const CreatureGltfMeshAsset &asset,
    const std::vector<glm::mat4> &palette,const glm::mat4 &body,CreatureQaFootVertex ref)
{
  const auto &p=asset.primitives[ref.primitive];const auto &v=p.mesh.interleavedPosUv;
  glm::mat4 skin(0.f);
  for(size_t k=0;k<4;++k)
    skin+=palette.at(p.jointIndices.at(ref.vertex*4+k))*p.jointWeights.at(ref.vertex*4+k);
  return glm::vec3(body*(skin*glm::vec4(v[ref.vertex*5],v[ref.vertex*5+1],v[ref.vertex*5+2],1.f)));
}
inline void RecordCreatureQaFootDraw(const void *visual,std::shared_ptr<const CreatureGltfMeshAsset> asset,
    const std::vector<glm::mat4> &palette,const glm::mat4 &body)
{
  if(CreatureQaFootProbePath().empty())return;
  const auto found=CreatureQaFootMemos().find(visual);if(found==CreatureQaFootMemos().end())return;
  const auto &m=found->second;const auto &geometry=CreatureQaFootGeometry(asset);
  using Json=nlohmann::json;const auto vec=[](glm::vec3 v){return Json::array({v.x,v.y,v.z});};
  Json feet=Json::array();
  for(const auto &foot:geometry.feet)
  {
    float low=std::numeric_limits<float>::infinity();
    for(const auto &ref:foot.vertices)low=std::min(low,CreatureQaSkinnedWorldVertex(*asset,palette,body,ref).y);
    Json points=Json::array(),bind=Json::array(),surface=Json::array(),bindSurface=Json::array(),surfaceSkin=Json::array(),members=Json::array();
    for(size_t member:foot.memberJoints)members.push_back(asset->nodes[asset->skin.jointNodes[member]].name);
    for(const auto &ref:foot.sole)
    {
      points.push_back(vec(CreatureQaSkinnedWorldVertex(*asset,palette,body,ref)));
      const auto &v=asset->primitives[ref.primitive].mesh.interleavedPosUv;
      bind.push_back(vec(glm::vec3(v[ref.vertex*5],v[ref.vertex*5+1],v[ref.vertex*5+2])));
    }
    for(const auto &ref:foot.surface)
    {
      surface.push_back(vec(CreatureQaSkinnedWorldVertex(*asset,palette,body,ref)));
      const auto &v=asset->primitives[ref.primitive].mesh.interleavedPosUv;
      bindSurface.push_back(vec(glm::vec3(v[ref.vertex*5],v[ref.vertex*5+1],v[ref.vertex*5+2])));
      const auto &prim=asset->primitives[ref.primitive];Json names=Json::array(),weights=Json::array();
      for(size_t k=0;k<4;++k)
      {
        names.push_back(asset->nodes[asset->skin.jointNodes.at(prim.jointIndices.at(ref.vertex*4+k))].name);
        weights.push_back(prim.jointWeights.at(ref.vertex*4+k));
      }
      surfaceSkin.push_back({{"joints",names},{"weights",weights}});
    }
    feet.push_back({{"bone",foot.bone},{"lowest_vertex_y",low},{"sole_points",points},{"bind_sole_points",bind},
      {"surface_points",surface},{"bind_surface_points",bindSurface},{"surface_skin",surfaceSkin},{"joint_members",members}});
  }
  auto &progress=CreatureQaWorldMotionProgressById();const auto motion=progress.find(m.id);
  static std::ofstream out=[]{
    if(std::filesystem::exists(CreatureQaFootProbePath()))throw std::runtime_error("QA foot probe output must be fresh");
    std::ofstream file(CreatureQaFootProbePath());if(!file)throw std::runtime_error("QA foot probe open failed");return file;
  }();
  out<<Json({{"schema","creature-qa-foot-world.v3"},{"scope","CPU observation of actual submitted palette/root; vertices wholly weighted to each semantic lower-foot subtree, including blended weights/claw tips. Same-position vertices with distinct skin retained. Actor feet plane reference, not terrain query/GPU or performance acceptance"},
    {"id",m.id},{"species","badger"},{"elapsed_render",m.elapsed},{"dt",m.dt},
    {"body_origin",vec(m.origin)},{"body_yaw",m.yaw},{"state",ToString(m.facts.state)},
    {"on_ground",m.facts.onGround},{"in_fluid",m.facts.inFluid},{"actual_speed",m.facts.horizontalSpeed},
    {"clip",m.clip},{"clip_time",m.time},{"fade_clip",m.fade},{"fade_time",m.fadeTime},{"fade_weight",m.fadeWeight},
    {"physics_elapsed",motion==progress.end()?Json(nullptr):Json(motion->second.elapsed)},
    {"requested_move",motion==progress.end()?Json(nullptr):Json(motion->second.move)},
    {"feet",feet}}).dump()<<std::endl;
  if(!out)throw std::runtime_error("QA foot probe write failed");
}
}

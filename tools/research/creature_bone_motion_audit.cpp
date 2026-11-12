// Diagnostic only: the production loaders, mesh builder, hierarchy and pose
// engine evaluate every submitted mesh vertex. No duplicate Python gait.
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonLoader.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonCubeMeshBuilder.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonHierarchy.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonGroundClearance.h"
#include "Pose/BoneSkeleton/BoneSkeletonPoseEngine.h"
#include "Pose/BoneSkeleton/SkeletalAnimationProfiles.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace cutum;
using Json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
struct Bounds {
  glm::vec3 low{std::numeric_limits<float>::max()};
  glm::vec3 high{-std::numeric_limits<float>::max()};
  void Add(const glm::vec3 &p) { low=glm::min(low,p); high=glm::max(high,p); }
};
Json Vec(const glm::vec3 &v) { return Json::array({v.x,v.y,v.z}); }
}

int main(int argc, char **argv) {
  try {
    if (argc != 3 && argc != 4) throw std::runtime_error("usage: creature_bone_motion_audit <models-root> <report.json> [--clearance]");
    const bool clearance=argc==4 && std::string(argv[3])=="--clearance";
    if(argc==4 && !clearance) throw std::runtime_error("Unknown diagnostic option");
    const fs::path models=fs::absolute(argv[1]);
    UCreatureDefinitionStorage definitions;
    definitions.Load((models/"creatures").string());
    Json report={{"schema","creature-native-bone-motion-diagnostic.v1"},
      {"scope","Native production pose/mesh diagnostics; no artistic or route/stride acceptance"},
      {"models_root",models.string()}, {"phase_samples",1025},
      {"ground_clearance_enabled",clearance},
      {"phase_span_radians",12.566370614359172},
      {"speed_ratio_semantics","horizontalSpeed / walkSpeed; not glTF travelScale"},
      {"resources",Json::array()}};
    const LocomotionState states[]={LocomotionState::Idle,LocomotionState::Walk,
      LocomotionState::Run,LocomotionState::Crouch,LocomotionState::Jump,
      LocomotionState::Fall,LocomotionState::Swim,LocomotionState::Tread,
      LocomotionState::Fly,LocomotionState::Hover,LocomotionState::Glide,
      LocomotionState::Slither,LocomotionState::Coil,LocomotionState::Action};
    for (const auto &id: definitions.ListAllIds()) {
      const auto &def=*definitions.Get(id);
      if(def.visual.backend!="bone_skeleton") continue;
      const auto &spec=def.visual.boneSkeleton;
      const fs::path file=models/"creatures"/id/(spec.geometryFile.empty()?"geometry.geo.json":spec.geometryFile);
      const auto geometry=CreatureBoneSkeletonLoader::LoadFromFile(file.string(),spec.geometryId);
      if(!geometry) throw std::runtime_error("Missing bone geometry: "+id);
      const auto mesh=BoneSkeletonCubeMeshBuilder::BuildMeshAsset(*geometry);
      const BoneSkeletonHierarchy hierarchy(*geometry);
      Json resource={{"id",id},{"habitat",ToString(def.habitat)},
        {"profile",SkeletalAnimationProfiles::ResolveProfileId(def)},
        {"geometry_file",file.string()},{"scenarios",Json::array()}};
      for (auto state:states) for (float ratio:{0.f,.22f,.5f,1.f,1.5f}) {
        if(state==LocomotionState::Idle && ratio!=0.f) continue;
        CreatureLocomotionFacts facts;
        facts.state=state;facts.archetype=def.locomotionArchetype;
        facts.horizontalSpeed=def.locomotion.walkSpeed*ratio;
        facts.onGround=state==LocomotionState::Idle || state==LocomotionState::Walk ||
          state==LocomotionState::Run || state==LocomotionState::Crouch;
        facts.inFluid=state==LocomotionState::Swim || state==LocomotionState::Tread;
        Bounds envelope;float lowest=std::numeric_limits<float>::max();
        float highestFloor=-lowest;int lowSample=0,highSample=0;
        float greatestLift=0.f;
        std::vector<Bounds> perBone(geometry->bones.size());
        std::vector<float> boneLowest(geometry->bones.size(),lowest),boneHighestFloor(geometry->bones.size(),-lowest);
        for(int sample=0;sample<1025;++sample) {
          facts.animPhase=12.566370614359172f*sample/1024.f;
          const auto pose=BoneSkeletonPoseEngine::Compute(facts,def,0.f);
          std::vector<glm::mat4> matrices(mesh.boneMeshes.size());
          for(size_t bone=0;bone<matrices.size();++bone) matrices[bone]=hierarchy.ComputeBoneMatrix(bone,pose);
          const float lift=clearance && UsesBoneGroundClearance(facts,def,resource["profile"].get<std::string>())
              ? ComputeBoneGroundClearance(mesh,matrices) : 0.f;
          greatestLift=std::max(greatestLift,lift);
          float floor=std::numeric_limits<float>::max();
          for(size_t b=0;b<mesh.boneMeshes.size();++b) {
            const auto bone=matrices[b];
            float boneFloor=std::numeric_limits<float>::max();
            for(const auto &cube:mesh.boneMeshes[b].cubes) {
              const auto model=bone*cube.restLocalMatrix;
              const auto &vertices=cube.interleavedPosUv;
              for(size_t v=0;v+4<vertices.size();v+=5) {
                glm::vec3 point(model*glm::vec4(vertices[v],vertices[v+1],vertices[v+2],1.f));
                point.y+=lift;
                if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z))
                  throw std::runtime_error("Non-finite posed vertex: "+id);
                envelope.Add(point);perBone[b].Add(point);
                floor=std::min(floor,point.y);boneFloor=std::min(boneFloor,point.y);
              }
            }
            if(boneFloor!=std::numeric_limits<float>::max()) {
              boneLowest[b]=std::min(boneLowest[b],boneFloor);
              boneHighestFloor[b]=std::max(boneHighestFloor[b],boneFloor);
            }
          }
          if(floor<lowest) {lowest=floor;lowSample=sample;}
          if(floor>highestFloor) {highestFloor=floor;highSample=sample;}
        }
        Json bones=Json::array();
        for(size_t b=0;b<geometry->bones.size();++b) if(boneLowest[b]!=std::numeric_limits<float>::max())
          bones.push_back({{"name",geometry->bones[b].name},{"lowest_y",boneLowest[b]},
            {"highest_floor_y",boneHighestFloor[b]},{"low",Vec(perBone[b].low)},{"high",Vec(perBone[b].high)}});
        resource["scenarios"].push_back({{"state",LocomotionStateCatalogKey(state)},
          {"speed_ratio",ratio},{"ground_contact_candidate",facts.onGround &&
            (def.habitat==CreatureHabitat::Terrestrial || def.habitat==CreatureHabitat::Amphibious)},
          {"lowest_y",lowest},{"highest_floor_y",highestFloor},
          {"greatest_root_lift",greatestLift},
          {"lowest_sample",lowSample},{"highest_floor_sample",highSample},
          {"low",Vec(envelope.low)},{"high",Vec(envelope.high)},{"bones",bones}});
      }
      std::cout<<"MEASURED "<<id<<std::endl;
      report["resources"].push_back(std::move(resource));
    }
    if(report["resources"].size()!=17) throw std::runtime_error("Expected all 17 bone species");
    const fs::path output=fs::absolute(argv[2]);fs::create_directories(output.parent_path());
    std::ofstream stream(output);stream<<report.dump(2)<<'\n';
    if(!stream) throw std::runtime_error("Cannot write diagnostic report");
    return 0;
  } catch(const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}

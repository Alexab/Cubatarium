#pragma once
#include "App/Platform/CreatureQaResourceCacheProbe.h"
#include "Gui/Preview/CreaturePreviewBoundsSink.h"
#include "Creatures/Visual/CreatureVisibility.h"
#include <glm/gtc/matrix_transform.hpp>

namespace cutum
{
namespace creature_envelope_qa
{
using Json=nlohmann::json;
inline Json Vec(const glm::vec3 &v){return {v.x,v.y,v.z};}
inline glm::vec3 Vec(const Json &v){return {v[0].get<float>(),v[1].get<float>(),v[2].get<float>()};}
inline Json Matrix(const glm::mat4 &m){Json out=Json::array();for(int c=0;c<4;++c)for(int r=0;r<4;++r)out.push_back(m[c][r]);return out;}
class WorldBounds final: public ICreatureDrawSink
{
public:
  glm::mat4 inverse;CreaturePreviewBoundsSink bounds;uint64_t paletteHash{14695981039346656037ull};
  explicit WorldBounds(const glm::mat4 &vp):inverse(glm::inverse(vp)){}
  void DrawTexturedPart(const glm::mat4 &m,GLuint t,CreaturePartMesh p)override{bounds.DrawTexturedPart(inverse*m,t,p);}
  void DrawBoneSkeletonMesh(const glm::mat4 &m,GLuint t,const BoneSkeletonCubeMeshCpu &p)override{bounds.DrawBoneSkeletonMesh(inverse*m,t,p);}
  void DrawSkinnedMesh(const glm::mat4 &m,GLuint t,const GltfPrimitiveCpu &p,const std::shared_ptr<const std::vector<glm::mat4>> &b)override
  {
    bounds.DrawSkinnedMesh(inverse*m,t,p,b);
    if(b){const auto *bytes=reinterpret_cast<const unsigned char *>(b->data());
      for(size_t i=0;i<b->size()*sizeof(glm::mat4);++i){paletteHash^=bytes[i];paletteHash*=1099511628211ull;}}
  }
  void DrawBoxWireframe(const glm::mat4 &,const glm::vec4 &)override{}
  void DrawBillboard(const glm::mat4 &m,GLuint t,const glm::vec4 &v)override{bounds.DrawBillboard(inverse*m,t,v);}
};
inline Json SaveTexture(GLuint texture,const std::filesystem::path &path,int size)
{
  if(!texture)throw std::runtime_error("Envelope capture empty native texture");
  UGlStateScope state(kGlMaskIconFbo);GLuint fbo=0;glGenFramebuffers(1,&fbo);
  glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
  if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Envelope readback FBO incomplete");
  std::vector<unsigned char> pixels(static_cast<size_t>(size)*size*4);
  glReadPixels(0,0,size,size,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
  size_t alpha=0;for(size_t i=3;i<pixels.size();i+=4)if(pixels[i])++alpha;
  const size_t stride=static_cast<size_t>(size)*4;
  for(int y=0;y<size/2;++y)std::swap_ranges(pixels.begin()+y*stride,pixels.begin()+(y+1)*stride,pixels.begin()+(size-1-y)*stride);
  const bool saved=stbi_write_png(path.string().c_str(),size,size,4,pixels.data(),size*4)!=0;
  glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);
  if(!saved)throw std::runtime_error("Envelope PNG failed");
  return {{"png",path.filename().string()},{"alpha_pixels",alpha}};
}
inline std::pair<glm::vec3,glm::vec3> TransformBounds(const glm::mat4 &m,const glm::vec3 &low,const glm::vec3 &high)
{
  glm::vec3 a(std::numeric_limits<float>::max()),b(-std::numeric_limits<float>::max());
  for(int x:{0,1})for(int y:{0,1})for(int z:{0,1})
  {const glm::vec3 p(m*glm::vec4(x?high.x:low.x,y?high.y:low.y,z?high.z:low.z,1.f));a=glm::min(a,p);b=glm::max(b,p);}
  return {a,b};
}
}
inline void ExportCreatureQaEnvelopes(UCreaturePreviewRenderer &preview,const std::vector<std::string> &ids,const std::filesystem::path &directory)
{
  const char *profilePath=std::getenv("CUBA_FLIGHT_QA_ENVELOPE_PROFILE");
  if(!profilePath || !*profilePath)return;
  const char *variantValue=std::getenv("CUBA_FLIGHT_QA_ENVELOPE_VARIANT");
  const std::string variant=variantValue?variantValue:"";
  if(variant!="baseline" && variant!="current")throw std::runtime_error("Envelope QA variant missing");
  using namespace creature_envelope_qa;
  Json profile;{std::ifstream file(profilePath);file>>profile;}
  if(profile.value("schema","")!="creature-native-envelope-profile.v1")throw std::runtime_error("Envelope QA invalid profile");
  const auto folder=directory/"envelopes";std::filesystem::create_directories(folder);
  Json report={{"schema","creature-native-envelope-capture.v1"},{"variant",variant},
    {"scope","Actual preview draws at shared world scale; actual CPU submitted vertex bounds and GPU culling-edge witnesses, not contact/terrain/all-backend acceptance"},
    {"resources",Json::array()}};
  struct Reset{UCreaturePreviewRenderer &p;~Reset(){p.SetQaEnvelopeProbe({});p.SetTransparentBackground(false);}} reset{preview};
  preview.SetTransparentBackground(true);preview.SetAnimationPreviewSpeedRatio(1.f);
  constexpr int size=256;
  for(const auto &id:ids)
  {
    const auto found=profile["resources"].find(id);if(found==profile["resources"].end())throw std::runtime_error("Envelope species absent from profile");
    const auto &resource=*found;const auto *def=preview.GetSpecies()->Get(id);
    const auto asset=CreatureGltfCache::Instance().Load(id,def->visual.gltf.modelPath,def->visual.sourceDirectory);
    if(!asset || def->visual.backend!="gltf_skeleton")throw std::runtime_error("Envelope initial matrix requires glTF17");
    UCreature envelopeCreature(1,id,glm::vec3(0.f),glm::vec3(0.f,def->eyeHeight,0.f));
    auto envelopeVisual=CreateCreatureVisual(*def);
    envelopeVisual->SetAppearance(ResolveCreatureAppearance(*preview.GetSpecies(),*preview.GetSkins(),id,""));
    glm::vec3 renderLow,renderHigh;
    if(!envelopeVisual->GetConservativeWorldBounds(envelopeCreature,*def,renderLow,renderHigh))
      throw std::runtime_error("Envelope species lacks visual culling bounds");
    if(!std::isfinite(asset->conservativeRenderRadius))throw std::runtime_error("QA glTF radius is unbounded");
    Json frames=Json::array(),edge=nullptr;size_t index=0;
    const auto low=Vec(resource["framing_low"]),high=Vec(resource["framing_high"]);
    for(const auto &frame:resource["frames"])
    {
      LocomotionState state=LocomotionState::Count;
      for(int s=0;s<static_cast<int>(LocomotionState::Count);++s)
        if(frame["state"]==LocomotionStateCatalogKey(static_cast<LocomotionState>(s)))state=static_cast<LocomotionState>(s);
      if(state==LocomotionState::Count)throw std::runtime_error("Envelope state invalid");
      const auto clip=ResolveAnimationClipId(state,def->visual.Animation.stateMap).value_or("idle");
      if(frame["expected_clips"][variant]!=clip)throw std::runtime_error("Envelope native clip differs from profile");
      const auto clipIndex=asset->animationIndexByName.find(clip);
      if(clipIndex==asset->animationIndexByName.end())throw std::runtime_error("Envelope native clip absent");
      const auto settings=def->visual.Animation.clips.find(clip);
      const float speed=settings==def->visual.Animation.clips.end()?1.f:settings->second.speed;
      const float phase=std::min(frame["phase"].get<float>(),.99999f);
      const float time=phase*GltfAnimationDuration(asset->animations[clipIndex->second])/std::max(.001f,speed);
      preview.SetAnimationPreviewState(state);
      Json observed;
      const auto observer=[&](CreatureDrawQueue &queue,const glm::mat4 &vp){WorldBounds bounds(vp);queue.Flush(bounds);
        if(!bounds.bounds.any)throw std::runtime_error("Envelope submissions empty");
        const bool covered=glm::all(glm::greaterThanEqual(bounds.bounds.low,renderLow-glm::vec3(.0001f))) &&
          glm::all(glm::lessThanEqual(bounds.bounds.high,renderHigh+glm::vec3(.0001f)));
        if(!covered)throw std::runtime_error("Actual submitted geometry escapes conservative visual envelope");
        observed={{"low",Vec(bounds.bounds.low)},{"high",Vec(bounds.bounds.high)},{"view_projection",Matrix(vp)},{"draws",queue.Size()},
          {"visual_envelope_covers_geometry",covered}};};
      preview.SetQaEnvelopeProbe(std::make_pair(low,high),observer);
      const auto png=id+"_"+std::to_string(index++)+".png";
      auto row=frame;row["native"]=SaveTexture(preview.RenderToUniqueTexture(id,"",size,frame["yaw"],frame["pitch"],time,true),folder/png,size);
      row["observed"]=observed;row["clip"]=clip;row["effective_phase"]=phase;row["anim_time_sec"]=time;
      frames.push_back(row);
      if(edge.is_null())
      {
        const auto a=Vec(observed["low"]),b=Vec(observed["high"]),collision=def->bounds.maxSizeBlocks;
        const glm::vec3 ca(-collision.x*.5f,0.f,-collision.z*.5f),cb(collision.x*.5f,collision.y,collision.z*.5f);
        int axis=-1;bool positive=true;float excess=.025f;
        for(int k=0;k<3;++k){if(b[k]-cb[k]>excess){axis=k;positive=true;excess=b[k]-cb[k];}if(ca[k]-a[k]>excess){axis=k;positive=false;excess=ca[k]-a[k];}}
        if(axis>=0)
        {
          const glm::vec3 eye=axis==2?glm::vec3(a.x-10.f,(a.y+b.y)*.5f,0.f):glm::vec3(0.f,(a.y+b.y)*.5f,b.z+10.f);
          const auto view=glm::lookAt(eye,axis==2?eye+glm::vec3(1,0,0):eye+glm::vec3(0,0,-1),glm::vec3(0,1,0));
          const auto bounds=TransformBounds(view,a,b),box=TransformBounds(view,ca,cb);
          auto va=bounds.first-glm::vec3(.2f),vb=bounds.second+glm::vec3(.2f);
          const int screenAxis=axis==1?1:0;
          if(positive)va[screenAxis]=(bounds.second[screenAxis]+box.second[screenAxis])*.5f;
          else vb[screenAxis]=(bounds.first[screenAxis]+box.first[screenAxis])*.5f;
          const auto vp=glm::ortho(va.x,vb.x,va.y,vb.y,.1f,50.f+glm::length(b-a))*view;
          const auto frustum=Frustum::FromViewProjection(vp);
          const bool culled=!CreatureBoundsIntersectsFrustum(frustum,glm::vec3(0.f),collision,eye,0.f,true);
          const bool visualCulled=!CreatureAabbIntersectsFrustum(frustum,renderLow,renderHigh);
          preview.SetQaEnvelopeProbe(std::make_pair(low,high),observer,vp,eye);
          auto pixels=SaveTexture(preview.RenderToUniqueTexture(id,"",size,0.f,0.f,time,true),folder/(id+"_edge.png"),size);
          edge={{"frame_index",frames.size()-1},{"axis",axis},{"positive",positive},{"excess_blocks",excess},
            {"old_collision_culling_rejects",culled},{"new_visual_culling_rejects",visualCulled},{"actual_visible_pixels",pixels["alpha_pixels"]},
            {"confirmed_false_cull",culled && pixels["alpha_pixels"].get<size_t>()>0},{"native",pixels},{"observed",observed}};
        }
      }
    }
    report["resources"].push_back({{"id",id},{"collision_rest",Vec(def->bounds.restSizeBlocks)},
      {"collision_max",Vec(def->bounds.maxSizeBlocks)},{"framing_low",Vec(low)},{"framing_high",Vec(high)},
      {"render_low",Vec(renderLow)},{"render_high",Vec(renderHigh)},{"model_render_radius",asset->conservativeRenderRadius},
      {"frames",frames},{"culling_edge",edge}});
  }
  std::ofstream file(folder/"manifest.json");file<<report.dump(2)<<'\n';
  if(!file)throw std::runtime_error("Envelope manifest write failed");
}
}

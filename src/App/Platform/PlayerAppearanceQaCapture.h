#pragma once
#include "App/Platform/CreatureQaMatrixCapture.h"
namespace cutum {
inline void ExportPlayerAppearanceQa(UCreaturePreviewRenderer &preview,const std::filesystem::path& root) {
 const char* enabled=std::getenv("CUBA_PLAYER_APPEARANCE_QA");
 if(!enabled || std::string(enabled)!="1")return;
 const auto folder=root/"player_appearance";std::filesystem::create_directories(folder);
 nlohmann::json rows=nlohmann::json::array();
 preview.SetTransparentBackground(true);
 auto capture=[&](const HumanAppearance& look,const std::string& outfit,const std::string& id,float yaw,
                  LocomotionState state,float time,bool helmet=false) {
  preview.SetAnimationPreviewState(state);
  std::array<WornArmorPreviewSlot,kWornArmorSlotCount> armor{};
  if(helmet) {
    const char* slots[]={"head","chest","legs","feet"};
    for(size_t i=0;i<4;++i)armor[i].ItemId=std::string("copper_")+slots[i];
  }
  const std::string sword="wood_sword",torch="wood_torch";
  constexpr int size=256;
  const GLuint texture=preview.RenderToUniqueTexture("human",outfit,size,yaw,10.f,time,time>0,&armor,helmet ? &sword : nullptr,helmet ? &torch : nullptr,&look);
  if(!texture)throw std::runtime_error("Player QA empty texture");
  UGlStateScope scope(kGlMaskIconFbo);GLuint fbo=0;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
  std::vector<unsigned char> pixels(size*size*4);glReadPixels(0,0,size,size,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
  for(int y=0;y<size/2;++y)std::swap_ranges(pixels.begin()+y*size*4,pixels.begin()+(y+1)*size*4,pixels.begin()+(size-1-y)*size*4);
  if(!stbi_write_png((folder/(id+".png")).string().c_str(),size,size,4,pixels.data(),size*4))throw std::runtime_error("Player QA write failed");
  glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);
  rows.push_back({{"id",id},{"appearance",look},{"outfit",outfit},{"yaw",yaw},{"helmet",helmet},{"full_armor_and_items",helmet},{"state",ToString(state)}});
 };
 for(auto body:HumanAppearance::Bodies)for(auto hair:HumanAppearance::Hairstyles) {
  HumanAppearance look;look.bodyVariant=body;look.hairstyleId=hair;
  for(int view=0;view<4;++view)capture(look,"",std::string(body)+"_"+hair+"_view"+std::to_string(view),view==3?45.f:view*90.f,LocomotionState::Idle,0);
  capture(look,"",std::string(body)+"_"+hair+"_helmet",45.f,LocomotionState::Idle,0,true);
 }
 for(auto color:HumanAppearance::Colors)for(auto body:HumanAppearance::Bodies) {
  HumanAppearance look;look.bodyVariant=body;look.hairColorId=color;look.hairstyleId="long";
  for(const auto& outfit:std::vector<std::string>{"","human_adventurer","human_guard"})
   capture(look,outfit,std::string(body)+"_color_"+color+"_"+(outfit.empty()?"base":outfit),45.f,LocomotionState::Idle,0);
 }
 for(auto body:HumanAppearance::Bodies) {
  HumanAppearance look;look.bodyVariant=body;
  for(auto state:{LocomotionState::Walk,LocomotionState::Run,LocomotionState::Crouch,LocomotionState::Jump,LocomotionState::Swim,LocomotionState::Action})
   for(int phase=1;phase<4;++phase)capture(look,"",std::string(body)+"_motion_"+ToString(state)+std::to_string(phase),45.f,state,phase*.125f);
 }
 preview.SetTransparentBackground(false);preview.SetAnimationPreviewState(LocomotionState::Walk);
 std::ofstream out(folder/"manifest.json");out<<rows.dump(2)<<'\n';
}
}

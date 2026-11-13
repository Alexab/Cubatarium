#pragma once
#include <string>
#include <array>
#include <nlohmann/json.hpp>
namespace cutum {
struct HumanAppearance {
  std::string bodyVariant{"male"};
  std::string hairstyleId{"short"};
  std::string hairColorId{"chestnut"};
  static constexpr std::array<const char*,2> Bodies{"male","female"};
  static constexpr std::array<const char*,6> Hairstyles{"none","short","bob","long","ponytail","curly"};
  static constexpr std::array<const char*,5> Colors{"black","chestnut","brown","blond","ginger"};
  void Normalize() {
    auto valid=[](const auto& options,const std::string& s){for(auto v:options)if(s==v)return true;return false;};
    if(!valid(Bodies,bodyVariant))bodyVariant="male";
    if(!valid(Hairstyles,hairstyleId))hairstyleId="short";
    if(!valid(Colors,hairColorId))hairColorId="chestnut";
  }
  std::string Key() const {return bodyVariant+"/"+hairstyleId+"/"+hairColorId;}
};
inline void to_json(nlohmann::json& j,const HumanAppearance& a) {
 j={{"body_variant",a.bodyVariant},{"hairstyle_id",a.hairstyleId},{"hair_color_id",a.hairColorId}};
}
inline void from_json(const nlohmann::json& j,HumanAppearance& a) {
 a=HumanAppearance{};
 if(j.is_object()) {
  auto read=[&](const char* k,std::string& v){auto it=j.find(k);if(it!=j.end()&&it->is_string())v=it->get<std::string>();};
  read("body_variant",a.bodyVariant);read("hairstyle_id",a.hairstyleId);read("hair_color_id",a.hairColorId);
 }
 a.Normalize();
}
}

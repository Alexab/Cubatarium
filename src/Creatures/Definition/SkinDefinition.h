#ifndef SKINDEFINITION_H
#define SKINDEFINITION_H

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

struct SkinCatalogInfo
{
  std::vector<std::string> tags;
  bool equippable{true};
  int sortOrder{0};
};

struct SkinDefinition
{
  std::string id;
  std::string displayName;
  std::string creatureId;
  SkinCatalogInfo catalog;
  glm::vec4 wireframeTint{1.f, 1.f, 1.f, 1.f};
  std::string textureKey{"diffuse"};
  std::unordered_map<std::string, std::string> textureMap;
  std::string iconMode{"skin_texture"};
  glm::vec4 iconFallbackColor{1.f, 1.f, 1.f, 1.f};
};

} // namespace cutum

#endif

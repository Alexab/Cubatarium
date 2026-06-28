#ifndef CONTENT_TYPE_REGISTRY_H
#define CONTENT_TYPE_REGISTRY_H

#include "Content/ContentType.h"
#include "Gui/Interfaces/IContentCatalog.h"
#include <unordered_map>
#include <vector>

namespace cutum
{

class UBlockDefinitionStorage;
class UCreatureDefinitionStorage;
class UObjectLibrary;
class USkinDefinitionStorage;

class UContentTypeRegistry : public IContentCatalog
{
public:
  void LoadTypes(const std::string &typesJsonPath);
  void IndexBlocks(const UBlockDefinitionStorage &storage);
  void IndexObjects(const UObjectLibrary &prefabs);
  void IndexCreatures(const UCreatureDefinitionStorage &storage);
  void IndexSkins(const USkinDefinitionStorage &storage);

  std::vector<std::string> GetTypeIds(ContentKind kind) const override;
  std::string GetTypeDisplayName(const std::string &typeId) const override;
  std::vector<CatalogEntry>
  GetEntries(ContentKind kind, const std::string &typeId) const override;

private:
  void EnsureDefaultTypes();
  std::vector<std::string>
  GetTypesForTags(const std::vector<std::string> &tags) const;

  std::vector<ContentType> BlockTypes;
  std::vector<ContentType> ObjectTypes;
  std::vector<ContentType> CreatureTypes;
  std::unordered_map<std::string, ContentType> BlockTypeById;
  std::unordered_map<std::string, ContentType> ObjectTypeById;
  std::unordered_map<std::string, ContentType> CreatureTypeById;
  std::unordered_map<std::string, std::vector<CatalogEntry>> BlockEntries;
  std::unordered_map<std::string, std::vector<CatalogEntry>> ObjectEntries;
  std::unordered_map<std::string, std::vector<CatalogEntry>> CreatureEntries;
  std::unordered_map<std::string, std::vector<CatalogEntry>> SkinEntries;
};

} // namespace cutum

#endif

#ifndef RECIPE_REGISTRY_H
#define RECIPE_REGISTRY_H

#include "Creatures/Core/CreatureInventory.h"
#include "Game/Crafting/RecipeDefinition.h"

#include <string>
#include <vector>

namespace cutum
{

class URecipeRegistry
{
public:
  bool LoadFromDirectory(const std::string &dirPath);
  const std::vector<RecipeDefinition> &GetRecipes() const { return Recipes; }
  const RecipeDefinition *FindById(const std::string &id) const;

  /// Shapeless craft: requires all inputs in storage; consumes and grants
  /// output.
  bool TryCraft(UCreatureInventory &inv, const RecipeDefinition &recipe,
                bool stationAvailable = false,
                std::string *reason = nullptr) const;
  bool TryCraftBatch(UCreatureInventory &inv, const RecipeDefinition &recipe,
                     size_t count, bool stationAvailable, std::string *reason = nullptr) const;
  bool CanCraftBatch(const UCreatureInventory &inv, const RecipeDefinition &recipe,
                     size_t count, bool stationAvailable, std::string *reason = nullptr) const;

private:
  bool EvaluateBatch(const UCreatureInventory &inv, UCreatureInventory *publish,
                     const RecipeDefinition &recipe, size_t count, bool stationAvailable,
                     std::string *reason) const;
  std::vector<RecipeDefinition> Recipes;
};

} // namespace cutum

#endif

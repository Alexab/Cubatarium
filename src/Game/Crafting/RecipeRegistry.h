#ifndef RECIPE_REGISTRY_H
#define RECIPE_REGISTRY_H

#include "Game/Crafting/RecipeDefinition.h"
#include "Creatures/Core/CreatureInventory.h"

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

  /// Shapeless craft: requires all inputs in storage; consumes and grants output.
  bool TryCraft(UCreatureInventory &inv, const RecipeDefinition &recipe) const;

private:
  std::vector<RecipeDefinition> Recipes;
};

} // namespace cutum

#endif

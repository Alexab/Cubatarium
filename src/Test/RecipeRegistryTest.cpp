#include "Game/Crafting/RecipeRegistry.h"
#include "Creatures/Core/CreatureInventory.h"

#include <iostream>

static void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << msg << std::endl;
    std::exit(1);
  }
}

int main()
{
  using cutum::RecipeDefinition;
  using cutum::RecipeIngredient;
  using cutum::UCreatureInventory;
  using cutum::URecipeRegistry;

  RecipeDefinition recipe;
  recipe.Id = "wood_sword";
  recipe.Inputs.push_back(RecipeIngredient{"wood", 2});
  recipe.Output = RecipeIngredient{"wood_sword", 1};

  UCreatureInventory inv;
  inv.GetStorageMutable()["wood"] = 2;

  URecipeRegistry registry;
  Expect(registry.TryCraft(inv, recipe), "craft succeeds with ingredients");
  Expect(inv.GetStorage().find("wood") == inv.GetStorage().end(),
         "wood consumed");
  Expect(inv.GetStorage().at("wood_sword") == 1, "sword granted");

  Expect(!registry.TryCraft(inv, recipe), "craft fails without wood");

  std::cout << "recipe_registry_test: OK" << std::endl;
  return 0;
}

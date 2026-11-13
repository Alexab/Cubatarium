#include "Game/Crafting/RecipeRegistry.h"
#include "Creatures/Core/CreatureInventory.h"

#include <filesystem>
#include <iostream>

static void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << msg << std::endl;
    std::exit(1);
  }
}

int main(int argc, char **argv)
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

  // Resolve the published recipes relative to the executable (build tree:
  // build/<config>/<compiler>/<Release> -> repo root), then fall back to the
  // working directory. A wrong cwd must fail the test, not skip it.
  URecipeRegistry loaded;
  bool recipesLoaded = false;
  std::filesystem::path exeDir;
  if (argc > 0 && argv[0] != nullptr)
  {
    std::error_code ec;
    exeDir = std::filesystem::absolute(std::filesystem::path(argv[0]), ec)
                 .parent_path();
  }
  const std::filesystem::path candidates[] = {
      exeDir / ".." / ".." / ".." / "content" / "recipes",
      std::filesystem::path("content") / "recipes",
  };
  for (const auto &dir : candidates)
  {
    if (loaded.LoadFromDirectory(dir.string()))
    {
      recipesLoaded = true;
      break;
    }
  }
  Expect(recipesLoaded, "published recipes load (repo root resolvable)");
  const cutum::RecipeDefinition *iron = loaded.FindById("iron_pickaxe");
  Expect(iron != nullptr, "iron_pickaxe recipe published");
  Expect(iron && iron->Inputs.size() >= 1, "iron_pickaxe has inputs");

  Expect(!registry.TryCraft(inv, recipe), "craft fails without wood");

  std::cout << "recipe_registry_test: OK" << std::endl;
  return 0;
}

#include "Game/Crafting/RecipeRegistry.h"

#include <filesystem>
#include <climits>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

bool URecipeRegistry::LoadFromDirectory(const std::string &dirPath)
{
  Recipes.clear();
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec))
  {
    return false;
  }
  for (const auto &entry : fs::directory_iterator(dirPath, ec))
  {
    if (ec || !entry.is_regular_file())
    {
      continue;
    }
    if (entry.path().extension() != ".json")
    {
      continue;
    }
    std::ifstream file(entry.path());
    if (!file)
    {
      continue;
    }
    nlohmann::json data;
    try
    {
      file >> data;
    }
    catch (...)
    {
      continue;
    }
    RecipeDefinition recipe;
    recipe.Id = data.value("id", entry.path().stem().string());
    recipe.Shapeless = data.value("shapeless", true);
    if (data.contains("inputs") && data["inputs"].is_array())
    {
      for (const auto &in : data["inputs"])
      {
        RecipeIngredient ing;
        if (in.is_string())
        {
          ing.Id = in.get<std::string>();
          ing.Count = 1;
        }
        else if (in.is_object())
        {
          ing.Id = in.value("id", "");
          ing.Count = in.value("count", 1);
        }
        if (!ing.Id.empty() && ing.Count > 0)
        {
          recipe.Inputs.push_back(ing);
        }
      }
    }
    if (data.contains("output") && data["output"].is_object())
    {
      recipe.Output.Id = data["output"].value("id", "");
      recipe.Output.Count = data["output"].value("count", 1);
    }
    if (!recipe.Id.empty() && !recipe.Output.Id.empty() &&
        !recipe.Inputs.empty())
    {
      Recipes.push_back(std::move(recipe));
    }
  }
  return !Recipes.empty();
}

const RecipeDefinition *URecipeRegistry::FindById(const std::string &id) const
{
  for (const auto &r : Recipes)
  {
    if (r.Id == id)
    {
      return &r;
    }
  }
  return nullptr;
}

bool URecipeRegistry::TryCraft(UCreatureInventory &inv, const RecipeDefinition &recipe) const
{
  if (recipe.Output.Id.empty() || recipe.Output.Count <= 0) return false;
  std::map<std::string, int> costs;
  for (const auto &input : recipe.Inputs)
  {
    if (input.Id.empty() || input.Count <= 0 || costs[input.Id] > INT_MAX - input.Count) return false;
    costs[input.Id] += input.Count;
  }
  auto trial = inv;
  for (const auto &cost : costs)
  {
    const auto &stock = trial.GetStorage();
    const auto it = stock.find(cost.first);
    if (!trial.IsCreative() && (it == stock.end() || it->second < cost.second)) return false;
    if (!trial.IsCreative())
    {
      bool paid = false;
      for (auto kind : {InventoryEntryKind::Item, InventoryEntryKind::Block})
        if (trial.Count(kind, cost.first) >= cost.second) { paid = static_cast<bool>(trial.Consume(kind, cost.first, cost.second)); break; }
      if (!paid) return false;
    }
  }
  InventoryEntryRef output;
  output.kind = trial.KindForId(recipe.Output.Id); output.Id = recipe.Output.Id;
  output.count = recipe.Output.Count; output.empty = false;
  if (!trial.Insert(output)) return false;
  inv = std::move(trial);
  return true;
}
} // namespace cutum

#include "Game/Crafting/RecipeRegistry.h"
#include <algorithm>

#include <climits>
#include <filesystem>
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
    recipe.Station = data.value("station", std::string{});
    recipe.Category = data.value("category", std::string{"Materials"});
    recipe.Seconds = std::clamp(data.value("seconds", 0.f), 0.f, 60.f);
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
          if (in.contains("alternatives") && in["alternatives"].is_array())
            for (const auto &id : in["alternatives"])
              if (id.is_string() && !id.get<std::string>().empty())
                ing.Alternatives.push_back(id.get<std::string>());
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
  std::sort(Recipes.begin(), Recipes.end(),
            [](const auto &a, const auto &b)
            {
              return a.Category == b.Category ? a.Id < b.Id
                                              : a.Category < b.Category;
            });
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

bool URecipeRegistry::TryCraft(UCreatureInventory &inv,
                               const RecipeDefinition &recipe,
                               bool stationAvailable, std::string *reason) const
{
  if (reason)
    *reason = "Missing ingredients or inventory space";
  if (!inv.IsCreative() && !recipe.Station.empty() && !stationAvailable)
  {
    if (reason)
      *reason = "Requires nearby " + recipe.Station;
    return false;
  }

  if (recipe.Output.Id.empty() || recipe.Output.Count <= 0)
    return false;
  std::map<std::string, int> costs;
  for (const auto &input : recipe.Inputs)
  {
    if (input.Id.empty() || input.Count <= 0)
      return false;
    if (!input.Alternatives.empty())
      continue;
    if (costs[input.Id] > INT_MAX - input.Count)
      return false;
    costs[input.Id] += input.Count;
  }
  auto trial = inv;
  for (const auto &cost : costs)
  {
    const auto &stock = trial.GetStorage();
    const auto it = stock.find(cost.first);
    if (!trial.IsCreative() && (it == stock.end() || it->second < cost.second))
    {
      if (reason)
        *reason = "Missing " + std::to_string(cost.second) + " x " + cost.first;
      return false;
    }
    if (!trial.IsCreative())
    {
      bool paid = false;
      for (auto kind : {InventoryEntryKind::Item, InventoryEntryKind::Block})
        if (trial.Count(kind, cost.first) >= cost.second)
        {
          paid =
              static_cast<bool>(trial.Consume(kind, cost.first, cost.second));
          break;
        }
      if (!paid)
        return false;
    }
  }
  for (const auto &input : recipe.Inputs)
    if (!input.Alternatives.empty() && !trial.IsCreative())
    {
      int remaining = input.Count;
      for (const auto &id : input.Alternatives)
      {
        const auto kind = trial.KindForId(id);
        const int n = std::min(remaining, trial.Count(kind, id));
        if (n > 0)
        {
          trial.Consume(kind, id, n);
          remaining -= n;
        }
        if (!remaining)
          break;
      }
      if (remaining)
      {
        if (reason)
          *reason = "Missing " + input.Id;
        return false;
      }
    }
  InventoryEntryRef output;
  output.kind = trial.KindForId(recipe.Output.Id);
  output.Id = recipe.Output.Id;
  output.count = recipe.Output.Count;
  output.empty = false;
  if (!trial.Insert(output))
  {
    if (reason)
      *reason = "Inventory is full";
    return false;
  }
  inv = std::move(trial);
  return true;
}
} // namespace cutum

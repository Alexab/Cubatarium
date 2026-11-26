#include "Game/Crafting/RecipeRegistry.h"
#include <algorithm>

#include <climits>
#include <filesystem>
#include <fstream>
#include <new>
#include <nlohmann/json.hpp>

namespace cutum
{

bool URecipeRegistry::LoadFromDirectory(const std::string &dirPath)
{
  std::vector<RecipeDefinition> loaded;
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
    catch (const std::bad_alloc &)
    {
      throw;
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
          recipe.Inputs.push_back(std::move(ing));
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
      loaded.push_back(std::move(recipe));
    }
  }
  std::sort(loaded.begin(), loaded.end(),
            [](const auto &a, const auto &b)
            {
              return a.Category == b.Category ? a.Id < b.Id
                                              : a.Category < b.Category;
            });
  Recipes.swap(loaded);
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

namespace
{
struct CraftCost
{
  const std::string *Id{};
  int Count{};
};

// GetStorage includes every kind with the same ID, even though a single
// material kind must be able to pay the cost.
int CountStock(const UCreatureInventory &inv, const std::string &id)
{
  int count = 0;
  for (const auto &row : inv.GetHotbars())
    for (const auto &slot : row.slots)
    {
      const auto &entry = slot.entry;
      if (slot.empty || entry.empty || !entry.count || entry.Id != id)
        continue;
      if (entry.IsCatalogReference())
        return -1;
      count = static_cast<int>(std::min<long long>(INT_MAX, static_cast<long long>(count) + entry.count));
    }
  return count;
}

bool ConsumeIngredients(UCreatureInventory &trial, const RecipeDefinition &recipe,
                        const std::vector<CraftCost> &costs, std::string *reason)
{
  for (const auto &cost : costs)
  {
    if (CountStock(trial, *cost.Id) < cost.Count)
    {
      if (reason)
        *reason = "Missing " + std::to_string(cost.Count) + " x " + *cost.Id;
      return false;
    }
    bool paid = false;
    for (auto kind : {InventoryEntryKind::Item, InventoryEntryKind::Block})
      if (trial.Count(kind, *cost.Id) >= cost.Count)
      {
        paid = static_cast<bool>(trial.Consume(kind, *cost.Id, cost.Count));
        break;
      }
    if (!paid)
      return false;
  }

  for (const auto &input : recipe.Inputs)
    if (!input.Alternatives.empty())
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
  return true;
}
} // namespace

bool URecipeRegistry::TryCraft(UCreatureInventory &inv, const RecipeDefinition &recipe,
                               bool stationAvailable, std::string *reason) const
{
  return TryCraftBatch(inv, recipe, 1, stationAvailable, reason);
}

bool URecipeRegistry::TryCraftBatch(UCreatureInventory &inv, const RecipeDefinition &recipe,
                                    size_t count, bool stationAvailable, std::string *reason) const
{
  return EvaluateBatch(inv, &inv, recipe, count, stationAvailable, reason);
}

bool URecipeRegistry::CanCraftBatch(const UCreatureInventory &inv, const RecipeDefinition &recipe,
                                    size_t count, bool stationAvailable, std::string *reason) const
{
  return EvaluateBatch(inv, nullptr, recipe, count, stationAvailable, reason);
}

bool URecipeRegistry::EvaluateBatch(const UCreatureInventory &inv, UCreatureInventory *publish,
                                    const RecipeDefinition &recipe, size_t count,
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
  if (!count || recipe.Output.Id.empty() || recipe.Output.Count <= 0)
    return false;

  std::vector<CraftCost> costs;
  costs.reserve(recipe.Inputs.size());
  for (const auto &input : recipe.Inputs)
  {
    if (input.Id.empty() || input.Count <= 0)
      return false;
    if (input.Alternatives.empty())
      costs.push_back({&input.Id, input.Count});
  }
  std::sort(costs.begin(), costs.end(), [](const auto &a, const auto &b) { return *a.Id < *b.Id; });
  size_t unique = 0;
  for (const auto cost : costs)
  {
    if (!unique || *costs[unique - 1].Id != *cost.Id)
      costs[unique++] = cost;
    else
    {
      auto &previous = costs[unique - 1].Count;
      if (previous > INT_MAX - cost.Count)
        return false;
      previous += cost.Count;
    }
  }
  costs.resize(unique);

  UCreatureInventory trial(UCreatureInventory::CraftingTrial{}, inv);
  InventoryEntryRef output;
  output.kind = trial.KindForId(recipe.Output.Id);
  output.Id = recipe.Output.Id;
  output.count = recipe.Output.Count;
  output.empty = false;
  for (size_t i = 0; i < count; ++i)
  {
    if (!trial.IsCreative() && !ConsumeIngredients(trial, recipe, costs, reason))
      return false;
    if (!trial.Insert(output))
    {
      if (reason)
        *reason = "Inventory is full";
      return false;
    }
  }
  if (publish)
  {
    publish->GetHotbarsMutable().swap(trial.GetHotbarsMutable());
    publish->NextInstanceId = trial.NextInstanceId;
  }
  return true;
}
} // namespace cutum

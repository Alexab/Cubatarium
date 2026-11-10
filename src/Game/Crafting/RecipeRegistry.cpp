#include "Game/Crafting/RecipeRegistry.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{

bool HasEnough(const UCreatureInventory &inv, const std::string &id, int count)
{
  if (count <= 0)
  {
    return true;
  }
  const auto &storage = inv.GetStorage();
  const auto it = storage.find(id);
  if (it == storage.end())
  {
    return false;
  }
  return it->second < 0 || it->second >= count;
}

bool Consume(UCreatureInventory &inv, const std::string &id, int count)
{
  if (count <= 0)
  {
    return true;
  }
  auto &storage = inv.GetStorageMutable();
  const auto it = storage.find(id);
  if (it == storage.end())
  {
    return false;
  }
  if (it->second < 0)
  {
    return true; // unlimited
  }
  if (it->second < count)
  {
    return false;
  }
  it->second -= count;
  if (it->second <= 0)
  {
    storage.erase(it);
  }
  return true;
}

} // namespace

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

bool URecipeRegistry::TryCraft(UCreatureInventory &inv,
                               const RecipeDefinition &recipe) const
{
  for (const auto &in : recipe.Inputs)
  {
    if (!HasEnough(inv, in.Id, in.Count))
    {
      return false;
    }
  }
  for (const auto &in : recipe.Inputs)
  {
    if (!Consume(inv, in.Id, in.Count))
    {
      return false;
    }
  }
  inv.AddItem(recipe.Output.Id, recipe.Output.Count);
  return true;
}

} // namespace cutum

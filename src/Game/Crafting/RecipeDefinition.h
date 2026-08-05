#ifndef RECIPE_DEFINITION_H
#define RECIPE_DEFINITION_H

#include <string>
#include <vector>

namespace cutum
{

struct RecipeIngredient
{
  std::string Id;
  int Count{1};
};

struct RecipeDefinition
{
  std::string Id;
  std::vector<RecipeIngredient> Inputs;
  RecipeIngredient Output;
  bool Shapeless{true};
};

} // namespace cutum

#endif

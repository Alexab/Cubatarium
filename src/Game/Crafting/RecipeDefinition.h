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
  std::vector<std::string> Alternatives;
};

struct RecipeDefinition
{
  std::string Id;
  std::vector<RecipeIngredient> Inputs;
  RecipeIngredient Output;
  bool Shapeless{true};
  std::string Station, Category{"Materials"};
  float Seconds{0};
};

} // namespace cutum

#endif

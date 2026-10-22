#ifndef CREATURESPAWNSELECTION_H
#define CREATURESPAWNSELECTION_H

#include <algorithm>
#include <cstddef>
#include <vector>

namespace cutum
{

template <typename T> struct WeightedSpawnOption
{
  T value{};
  float weight{0.f};
};

template <typename T, typename IsViable>
std::vector<WeightedSpawnOption<T>> FilterViableSpawnOptions(
    const std::vector<WeightedSpawnOption<T>> &options, IsViable isViable)
{
  std::vector<WeightedSpawnOption<T>> viable;
  viable.reserve(options.size());
  for (const WeightedSpawnOption<T> &option : options)
  {
    if (option.weight > 0.f && isViable(option.value))
    {
      viable.push_back(option);
    }
  }
  return viable;
}

template <typename T>
float TotalSpawnOptionWeight(
    const std::vector<WeightedSpawnOption<T>> &options)
{
  float total = 0.f;
  for (const WeightedSpawnOption<T> &option : options)
  {
    if (option.weight > 0.f)
    {
      total += option.weight;
    }
  }
  return total;
}

/// Select by a roll in [0, total weight]. The last positive option is used
/// when the roll lands exactly on the upper endpoint.
template <typename T>
const WeightedSpawnOption<T> *ChooseWeightedSpawnOption(
    const std::vector<WeightedSpawnOption<T>> &options, float roll)
{
  const float total = TotalSpawnOptionWeight(options);
  if (total <= 0.f)
  {
    return nullptr;
  }
  roll = std::clamp(roll, 0.f, total);
  const WeightedSpawnOption<T> *lastPositive = nullptr;
  for (const WeightedSpawnOption<T> &option : options)
  {
    if (option.weight <= 0.f)
    {
      continue;
    }
    lastPositive = &option;
    if (roll < option.weight)
    {
      return &option;
    }
    roll -= option.weight;
  }
  return lastPositive;
}

} // namespace cutum

#endif

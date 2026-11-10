#include "Items/ItemVisualPresetLibrary.h"

#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

namespace cutum
{
namespace
{

ItemVisualEasing EasingFromString(const std::string &value)
{
  if (value == "linear")
  {
    return ItemVisualEasing::Linear;
  }
  if (value == "smoothstep")
  {
    return ItemVisualEasing::Smoothstep;
  }
  return ItemVisualEasing::SinPi;
}

bool ReadPair(const nlohmann::json &obj, const char *key, float out[2],
              bool &has)
{
  if (!obj.contains(key) || !obj[key].is_array() || obj[key].size() < 2)
  {
    return false;
  }
  out[0] = obj[key][0].get<float>();
  out[1] = obj[key][1].get<float>();
  has = true;
  return true;
}

} // namespace

bool UItemVisualPresetLibrary::Load(const std::string &path)
{
  Clear();
  try
  {
    std::ifstream file(path);
    if (!file.is_open())
    {
      std::cerr << "UItemVisualPresetLibrary: cannot open " << path
                << std::endl;
      return false;
    }
    nlohmann::json data;
    file >> data;
    if (!data.is_object())
    {
      return false;
    }
    for (auto it = data.begin(); it != data.end(); ++it)
    {
      if (!it.value().is_object())
      {
        continue;
      }
      const auto &p = it.value();
      ItemVisualPreset preset;
      preset.Id = it.key();
      preset.Duration = p.value("duration", 0.28f);
      preset.Easing = EasingFromString(p.value("easing", std::string("sin_pi")));
      preset.HoldAtEnd = p.value("hold_at_end", false);
      if (p.contains("arm") && p["arm"].is_object())
      {
        const auto &arm = p["arm"];
        ReadPair(arm, "rx_deg", preset.Arm.RxDeg, preset.Arm.HasRx);
        ReadPair(arm, "ry_deg", preset.Arm.RyDeg, preset.Arm.HasRy);
        ReadPair(arm, "rz_deg", preset.Arm.RzDeg, preset.Arm.HasRz);
        ReadPair(arm, "tx", preset.Arm.Tx, preset.Arm.HasTx);
        ReadPair(arm, "ty", preset.Arm.Ty, preset.Arm.HasTy);
        ReadPair(arm, "tz", preset.Arm.Tz, preset.Arm.HasTz);
      }
      Presets[preset.Id] = std::move(preset);
    }
    return !Presets.empty();
  }
  catch (const std::exception &e)
  {
    std::cerr << "UItemVisualPresetLibrary: " << path << ": " << e.what()
              << std::endl;
    Clear();
    return false;
  }
}

void UItemVisualPresetLibrary::Clear()
{
  Presets.clear();
}

const ItemVisualPreset *UItemVisualPresetLibrary::Get(
    const std::string &id) const
{
  const auto it = Presets.find(id);
  if (it == Presets.end())
  {
    return nullptr;
  }
  return &it->second;
}

size_t UItemVisualPresetLibrary::Count() const
{
  return Presets.size();
}

} // namespace cutum

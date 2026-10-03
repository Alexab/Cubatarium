#ifndef ITEM_VISUAL_PRESET_LIBRARY_H
#define ITEM_VISUAL_PRESET_LIBRARY_H

#include <cstdint>
#include <string>
#include <unordered_map>

namespace cutum
{

enum class FpSwingKind : uint8_t
{
  Dig = 0,
  Place = 1,
  Melee = 2
};

enum class ItemVisualEasing : uint8_t
{
  Linear = 0,
  SinPi,
  Smoothstep
};

struct ItemVisualArmKeys
{
  float RxDeg[2]{0.f, 0.f};
  float RyDeg[2]{0.f, 0.f};
  float RzDeg[2]{0.f, 0.f};
  float Tx[2]{0.f, 0.f};
  float Ty[2]{0.f, 0.f};
  float Tz[2]{0.f, 0.f};
  bool HasRx{false};
  bool HasRy{false};
  bool HasRz{false};
  bool HasTx{false};
  bool HasTy{false};
  bool HasTz{false};
};

struct ItemVisualPreset
{
  std::string Id;
  float Duration{0.28f};
  ItemVisualEasing Easing{ItemVisualEasing::SinPi};
  ItemVisualArmKeys Arm;
  bool HoldAtEnd{false};
};

class UItemVisualPresetLibrary
{
public:
  bool Load(const std::string &path);
  void Clear();
  const ItemVisualPreset *Get(const std::string &id) const;
  size_t Count() const;

private:
  std::unordered_map<std::string, ItemVisualPreset> Presets;
};

} // namespace cutum

#endif

#pragma once
#include "Game/Physics/ThrowSettings.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiCheckbox.h"
#include "Gui/Widgets/GuiPanel.h"
#include <array>
#include <memory>
#include <string>
namespace cutum
{
class UThrowSettingsForm
{
  const GuiTheme *Theme;
  ThrowSettings Settings;
  UGuiButton *Preset{nullptr};
  std::array<UGuiCheckbox *, 9> Boxes{};
  void Refresh()
  {
    if (Preset)
      Preset->SetLabel(std::string("Item throwing: ") +
                       (Settings.preset == ThrowPreset::Free ? "Free"
                        : Settings.preset == ThrowPreset::Restricted
                            ? "Restricted"
                            : "Custom"));
    for (size_t i = 0; i < Boxes.size(); ++i)
      if (Boxes[i])
        Boxes[i]->SetChecked(Settings.Allows(static_cast<ThrowCategory>(i)));
  }

public:
  UThrowSettingsForm(const GuiTheme *theme, ThrowSettings settings)
      : Theme(theme), Settings(settings)
  {
  }
  ThrowSettings ReadSettings() const { return Settings; }
  void BuildInto(UGuiPanel &panel)
  {
    auto button = std::make_unique<UGuiButton>(Theme, "");
    Preset = button.get();
    Preset->SetOnClick(
        [this]
        {
          Settings.preset = static_cast<ThrowPreset>(
              (static_cast<int>(Settings.preset) + 1) % 3);
          Refresh();
        });
    panel.AddChild(std::move(button));
    const char *labels[] = {
        "Throw blocks",    "Throw materials", "Throw special projectiles",
        "Throw tools",     "Throw weapons",   "Throw food",
        "Throw equipment", "Throw utilities", "Throw uncategorized items"};
    for (size_t i = 0; i < Boxes.size(); ++i)
    {
      auto box = std::make_unique<UGuiCheckbox>(Theme, labels[i]);
      Boxes[i] = box.get();
      box->SetOnChanged(
          [this, i](bool value)
          {
            unsigned mask = 0;
            for (size_t n = 0; n < 9; ++n)
              if (Settings.Allows(static_cast<ThrowCategory>(n)))
                mask |= 1u << n;
            Settings.preset = ThrowPreset::Custom;
            Settings.allowed = value ? mask | (1u << i) : mask & ~(1u << i);
            Refresh();
          });
      panel.AddChild(std::move(box));
    }
    Refresh();
  }
  int MeasureHeight() const
  {
    return (Theme->FontSizeBody + Theme->Padding * 2) * 10;
  }
  void Layout(const GuiRect &area) const
  {
    const int row = MeasureHeight() / 10;
    if (Preset)
      Preset->SetBounds({area.X, area.Y, area.W, row});
    for (size_t i = 0; i < Boxes.size(); ++i)
      if (Boxes[i])
        Boxes[i]->SetBounds({area.X, area.Y + row * int(i + 1), area.W, row});
  }
};
} // namespace cutum

#include "Gui/Layout/GuiTooltipLayout.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Items/ItemDefinition.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace cutum
{

namespace
{

constexpr int kTooltipZOrder = 1000;
constexpr int kPointerOffsetX = 12;
constexpr int kPointerOffsetY = 16;
constexpr int kViewportMargin = 8;

int EstimateTextWidth(const std::string &text, const GuiTheme &theme)
{
  return static_cast<int>(text.size()) * (theme.FontSizeBody * 3 / 5);
}

int CountTooltipLines(const std::string &text)
{
  int lines = 1;
  for (char c : text)
  {
    if (c == '\n')
    {
      ++lines;
    }
  }
  return lines;
}

int MeasureLongestLineWidth(const std::string &text, const GuiTheme &theme,
                            UGuiRenderer *renderer)
{
  int maxW = 0;
  size_t start = 0;
  while (start <= text.size())
  {
    const size_t nl = text.find('\n', start);
    const std::string line =
        text.substr(start, nl == std::string::npos ? std::string::npos
                                                   : nl - start);
    const int w = renderer ? renderer->MeasureTextWidth(line)
                           : EstimateTextWidth(line, theme);
    maxW = std::max(maxW, w);
    if (nl == std::string::npos)
    {
      break;
    }
    start = nl + 1;
  }
  return maxW;
}

} // namespace

int MeasureTooltipTextWidth(const std::string &text, const GuiTheme &theme,
                            UGuiRenderer *renderer)
{
  if (text.find('\n') != std::string::npos)
  {
    return MeasureLongestLineWidth(text, theme, renderer);
  }
  if (renderer)
  {
    return renderer->MeasureTextWidth(text);
  }
  return EstimateTextWidth(text, theme);
}

void LayoutTooltipNearPointer(UGuiLabel &label, const std::string &text,
                              int pointerX, int pointerY,
                              const GuiRect &viewport, const GuiTheme &theme,
                              int measuredTextWidth)
{
  if (text.empty())
  {
    label.SetVisible(false);
    return;
  }

  label.SetText(text);
  label.SetVisible(true);
  label.SetDrawBackground(true);
  label.SetTextAlign(GuiTextAlign::Left);
  label.SetZOrder(kTooltipZOrder);

  const int pad = theme.Padding * 2;
  const int lines = CountTooltipLines(text);
  const int lineH = theme.FontSizeBody + std::max(2, theme.Padding / 2);
  const int h = lines * lineH + pad;
  const int textW = measuredTextWidth >= 0
                        ? measuredTextWidth
                        : MeasureLongestLineWidth(text, theme, nullptr);
  const int maxW = std::max(80, viewport.W - kViewportMargin * 2);
  const int w = std::min(maxW, textW + pad);

  int x = pointerX + kPointerOffsetX;
  int y = pointerY + kPointerOffsetY;

  if (x + w > viewport.X + viewport.W - kViewportMargin)
  {
    x = pointerX - w - kPointerOffsetX;
  }
  if (x < viewport.X + kViewportMargin)
  {
    x = viewport.X + kViewportMargin;
  }

  if (y + h > viewport.Y + viewport.H - kViewportMargin)
  {
    y = pointerY - h - kPointerOffsetY;
  }
  if (y < viewport.Y + kViewportMargin)
  {
    y = viewport.Y + kViewportMargin;
  }

  label.SetBounds({x, y, w, h});
}

std::string BuildItemTooltipText(const std::string &displayName,
                                 const ItemDefinition *def, float wear01,
                                 bool broken)
{
  std::ostringstream oss;
  oss << displayName;
  if (!def)
  {
    return oss.str();
  }

  const int damage = def->Tool.Damage.FleshyOrMelee();
  if (damage > 0)
  {
    oss << "\nDamage: " << damage;
  }

  if (!def->Tool.GroupCaps.empty())
  {
    int bestLevel = -1;
    std::string bestGroup;
    for (const auto &pair : def->Tool.GroupCaps)
    {
      if (pair.second.MaxLevel > bestLevel)
      {
        bestLevel = pair.second.MaxLevel;
        bestGroup = pair.first;
      }
    }
    if (bestLevel >= 0 && !bestGroup.empty())
    {
      oss << "\nDig: " << bestGroup << " Lv" << bestLevel;
    }
  }

  if (!def->Armor.ArmorGroups.empty())
  {
    int armor = 0;
    const auto fleshy = def->Armor.ArmorGroups.find("fleshy");
    if (fleshy != def->Armor.ArmorGroups.end())
    {
      armor = fleshy->second;
    }
    else
    {
      armor = def->Armor.ArmorGroups.begin()->second;
    }
    oss << "\nArmor: " << armor;
  }

  if (def->WearEnd != ItemWearEnd::Indestructible)
  {
    if (broken)
    {
      oss << "\nDurability: broken";
    }
    else
    {
      const float clamped = std::clamp(wear01, 0.f, 1.f);
      const int pct =
          static_cast<int>(std::lround((1.f - clamped) * 100.f));
      oss << "\nDurability: " << pct << "%";
    }
  }

  return oss.str();
}

} // namespace cutum

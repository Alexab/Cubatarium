#include "Gui/Widgets/GuiProgressBar.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace cutum
{

UGuiProgressBar::UGuiProgressBar(const GuiTheme *theme) : Theme(theme) {}

void UGuiProgressBar::SetValue(float value)
{
  Value = std::clamp(value, 0.f, 1.f);
  Indeterminate = false;
}

void UGuiProgressBar::SetIndeterminate(bool indeterminate)
{
  Indeterminate = indeterminate;
}

int UGuiProgressBar::GetPreferredHeight() const
{
  const int barH = Theme ? Theme->ProgressBarHeight : 14;
  const int labelH = Label.empty() ? 0 : (Theme ? Theme->FontSizeBody : 16);
  const int gap = Label.empty() ? 0 : 4;
  const int percentH = ShowPercent ? (Theme ? Theme->FontSizeBody : 16) : 0;
  return barH + gap + labelH + percentH + 4;
}

int UGuiProgressBar::GetPreferredWidth() const
{
  return Bounds.W > 0 ? Bounds.W : 280;
}

void UGuiProgressBar::Update(double dt)
{
  if (Indeterminate)
  {
    IndeterminateOffset += static_cast<float>(dt) * 0.35f;
    if (IndeterminateOffset > 1.f)
    {
      IndeterminateOffset -= 1.f;
    }
  }
  UGuiWidget::Update(dt);
}

void UGuiProgressBar::Draw(UGuiRenderer &renderer)
{
  if (!Visible || !Theme)
  {
    return;
  }

  int y = Bounds.Y;
  if (!Label.empty())
  {
    renderer.DrawText(Label, Bounds.X, y, Theme->TextSecondary);
    y += Theme->FontSizeBody + 4;
  }

  const GuiRect track{Bounds.X, y, Bounds.W, Theme->ProgressBarHeight};
  renderer.DrawFilledRect(track, Theme->ProgressTrack);
  renderer.DrawBorderRect(track, Theme->PanelBorder, Theme->BorderThickness);

  if (Indeterminate)
  {
    const int segW = std::max(24, track.W / 4);
    const int travel = std::max(1, track.W - segW);
    const int segX =
        track.X + static_cast<int>(std::floor(IndeterminateOffset * travel));
    const GuiRect seg{segX, track.Y, segW, track.H};
    renderer.DrawFilledRect(seg, Theme->ProgressIndeterminate);
  }
  else
  {
    const int fillW = static_cast<int>(std::round(track.W * Value));
    if (fillW > 0)
    {
      const GuiRect fill{track.X, track.Y, fillW, track.H};
      renderer.DrawFilledRect(fill, Theme->ProgressFill);
    }
  }

  if (ShowPercent && !Indeterminate)
  {
    std::ostringstream oss;
    oss << static_cast<int>(std::round(Value * 100.f)) << '%';
    const int textY = track.Y + track.H + 4;
    renderer.DrawTextCenteredInRect(
        GuiRect{Bounds.X, textY, Bounds.W, Theme->FontSizeBody}, oss.str(),
        Theme->TextPrimary);
  }
}

} // namespace cutum

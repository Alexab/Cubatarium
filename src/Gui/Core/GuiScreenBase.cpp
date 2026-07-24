#include "Gui/Core/GuiScreenBase.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiScale.h"
#include "Gui/Widgets/GuiWidget.h"

#include <algorithm>

namespace cutum
{

UGuiScreenBase::~UGuiScreenBase() = default;

void UGuiScreenBase::OnAttach(UGuiContext &ctx)
{
  Context = &ctx;
  Metrics = ctx.GetMetrics();
}

void UGuiScreenBase::OnDetach()
{
  Context = nullptr;
}

void UGuiScreenBase::Update(double /*dt*/) {}

void UGuiScreenBase::SetViewportInsets(int insetLeft, int insetTop,
                                       int insetRight, int insetBottom)
{
  ContentOffsetX = std::max(0, insetLeft);
  ContentOffsetY = std::max(0, insetTop);
  ContentInsetRight = std::max(0, insetRight);
  ContentInsetBottom = std::max(0, insetBottom);
}

void UGuiScreenBase::OnViewportChanged(int width, int height)
{
  if (width > 0)
  {
    LastFrameWidth = width;
    ViewportW = std::max(0, width - ContentOffsetX - ContentInsetRight);
  }
  if (height > 0)
  {
    LastFrameHeight = height;
    ViewportH = std::max(0, height - ContentOffsetY - ContentInsetBottom);
  }
}

void UGuiScreenBase::OnMetricsChanged(const GuiMetrics &metrics)
{
  Metrics = metrics;
  RelayoutOnMetricsChange();
}

int UGuiScreenBase::Scaled(int design_px) const
{
  return ScalePx(design_px, Metrics.Scale);
}

void UGuiScreenBase::RelayoutOnMetricsChange()
{
  if (LastFrameWidth > 0 && LastFrameHeight > 0)
  {
    OnViewportChanged(LastFrameWidth, LastFrameHeight);
  }
}

} // namespace cutum

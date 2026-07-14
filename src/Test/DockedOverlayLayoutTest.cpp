#include "Gui/Core/GuiTheme.h"
#include "Gui/Layout/DockedOverlayLayout.h"

#include <cstdlib>
#include <iostream>

namespace
{

constexpr const char *kTestName = "docked_overlay_layout_test";

void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << kTestName << ": " << message << std::endl;
    std::exit(1);
  }
}

void TestSinglePaneOnNarrowViewport()
{
  cutum::GuiTheme theme;
  const auto layout =
      cutum::DockedOverlayLayout::Compute(800, 600, 0, 0, 80, 35, theme);
  Expect(layout.preview.W == 0, "preview hidden below width breakpoint");
  Expect(layout.main.W >= 200, "main pane keeps minimum width");
}

void TestSinglePaneOnShortViewport()
{
  cutum::GuiTheme theme;
  const auto layout =
      cutum::DockedOverlayLayout::Compute(1280, 640, 0, 0, 80, 35, theme);
  Expect(layout.preview.W == 0, "preview hidden below height breakpoint");
}

void TestDualPaneOnLargeViewport()
{
  cutum::GuiTheme theme;
  const auto layout =
      cutum::DockedOverlayLayout::Compute(1280, 800, 0, 0, 80, 35, theme);
  Expect(layout.preview.W > 0, "preview visible on large viewport");
  Expect(layout.main.W + layout.preview.W + theme.Padding <= 1280,
         "layout fits viewport width");
}

void TestBreakpointEdges()
{
  cutum::GuiTheme theme;
  const auto atWidth =
      cutum::DockedOverlayLayout::Compute(1100, 800, 0, 0, 80, 35, theme);
  const auto belowWidth =
      cutum::DockedOverlayLayout::Compute(1099, 800, 0, 0, 80, 35, theme);
  Expect(atWidth.preview.W > 0, "width breakpoint inclusive keeps preview");
  Expect(belowWidth.preview.W == 0, "width just below breakpoint hides preview");

  const auto atHeight =
      cutum::DockedOverlayLayout::Compute(1280, 700, 0, 0, 80, 35, theme);
  const auto belowHeight =
      cutum::DockedOverlayLayout::Compute(1280, 699, 0, 0, 80, 35, theme);
  Expect(atHeight.preview.W > 0, "height breakpoint inclusive keeps preview");
  Expect(belowHeight.preview.W == 0,
         "height just below breakpoint hides preview");
}

} // namespace

int main()
{
  TestSinglePaneOnNarrowViewport();
  TestSinglePaneOnShortViewport();
  TestDualPaneOnLargeViewport();
  TestBreakpointEdges();
  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}

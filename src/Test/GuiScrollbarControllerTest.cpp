#include "Gui/Core/GuiScrollbarController.h"

#include <cstdlib>
#include <iostream>

namespace
{

constexpr const char *kTestName = "gui_scrollbar_controller_test";

void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << kTestName << ": " << message << std::endl;
    std::exit(1);
  }
}

cutum::GuiScrollbarMetrics MakeMetrics(int trackY, int trackH, int thumbY,
                                     int thumbH, int viewportH, int maxScroll,
                                     int scrollY)
{
  cutum::GuiScrollbarMetrics metrics;
  metrics.Track = {100, trackY, 12, trackH};
  metrics.Thumb = {101, thumbY, 10, thumbH};
  metrics.ViewportH = viewportH;
  metrics.MaxScroll = maxScroll;
  metrics.ScrollY = scrollY;
  return metrics;
}

void TestHitTest()
{
  cutum::UGuiScrollbarController controller;
  const auto metrics = MakeMetrics(0, 200, 40, 30, 100, 300, 60);

  Expect(controller.HitTest(metrics, 105, 50) == cutum::GuiScrollbarHit::Thumb,
         "pointer on thumb");
  Expect(controller.HitTest(metrics, 105, 10) ==
             cutum::GuiScrollbarHit::TrackAbove,
         "pointer above thumb");
  Expect(controller.HitTest(metrics, 105, 180) ==
             cutum::GuiScrollbarHit::TrackBelow,
         "pointer below thumb");
  Expect(controller.HitTest(metrics, 50, 50) == cutum::GuiScrollbarHit::None,
         "pointer outside track");
}

void TestThumbDragMapping()
{
  cutum::UGuiScrollbarController controller;
  auto metrics = MakeMetrics(0, 200, 0, 40, 100, 160, 0);

  Expect(controller.BeginThumbDrag(metrics, 10), "thumb drag begins on thumb");
  Expect(controller.IsDragging(), "dragging flag set");

  metrics.ScrollY = controller.ScrollFromThumbDrag(metrics, 10);
  Expect(metrics.ScrollY == 0, "thumb at top maps to scroll 0");

  metrics.ScrollY = controller.ScrollFromThumbDrag(metrics, 170);
  Expect(metrics.ScrollY == 160, "thumb at bottom maps to max scroll");

  metrics.ScrollY = controller.ScrollFromThumbDrag(metrics, 90);
  Expect(metrics.ScrollY >= 70 && metrics.ScrollY <= 90,
         "thumb in middle maps to intermediate scroll");

  controller.EndDrag();
  Expect(!controller.IsDragging(), "drag ends");
}

void TestTrackPageJump()
{
  cutum::UGuiScrollbarController controller;
  const auto metrics = MakeMetrics(0, 200, 80, 30, 50, 200, 100);

  const int above =
      controller.ScrollFromTrackClick(metrics, cutum::GuiScrollbarHit::TrackAbove,
                                     20);
  Expect(above == 50, "track above pages up");

  const int below =
      controller.ScrollFromTrackClick(metrics, cutum::GuiScrollbarHit::TrackBelow,
                                     180);
  Expect(below == 150, "track below pages down");

  const int clamped =
      controller.ScrollFromTrackClick(metrics, cutum::GuiScrollbarHit::TrackBelow,
                                     180);
  Expect(clamped <= metrics.MaxScroll, "page jump clamps to max");
}

} // namespace

int main()
{
  TestHitTest();
  TestThumbDragMapping();
  TestTrackPageJump();
  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}

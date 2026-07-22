#ifndef WORLD_PROGRESS_SCREEN_H
#define WORLD_PROGRESS_SCREEN_H

#include "Core/Progress/ProgressTypes.h"
#include "Gui/Core/GuiScreenBase.h"

namespace cutum
{

class UGuiLabel;
class UGuiProgressBar;

class UWorldProgressScreen : public UGuiScreenBase
{
public:
  void Build(UGuiContext &ctx) override;
  void OnViewportChanged(int width, int height) override;
  bool BlocksGameInput() const override { return true; }

  void ApplySnapshot(const ProgressSnapshot &snapshot);

private:
  void LayoutCentered();

  UGuiLabel *TitleLabel{nullptr};
  UGuiLabel *PhaseLabel{nullptr};
  UGuiProgressBar *ProgressBar{nullptr};
};

} // namespace cutum

#endif

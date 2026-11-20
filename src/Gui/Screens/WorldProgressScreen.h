#ifndef WORLD_PROGRESS_SCREEN_H
#define WORLD_PROGRESS_SCREEN_H

#include "Core/Progress/ProgressTypes.h"
#include "Gui/Core/GuiScreenBase.h"
#include <functional>
#include <string>

namespace cutum
{

class UGuiLabel;
class UGuiProgressBar;
class UGuiButton;

class UWorldProgressScreen : public UGuiScreenBase
{
public:
  void Build(UGuiContext &ctx) override;
  void OnViewportChanged(int width, int height) override;
  bool BlocksGameInput() const override { return true; }

  void ApplySnapshot(const ProgressSnapshot &snapshot);
  void SetFailureActions(std::function<void()> retry,
                         std::function<void()> back);

private:
  void LayoutCentered();

  UGuiLabel *TitleLabel{nullptr};
  UGuiLabel *PhaseLabel{nullptr};
  UGuiLabel *SubstageLabel{nullptr};
  UGuiProgressBar *ProgressBar{nullptr};
  UGuiButton *RetryButton{nullptr};
  UGuiButton *BackButton{nullptr};
  std::string FailureMessage;
};

} // namespace cutum

#endif

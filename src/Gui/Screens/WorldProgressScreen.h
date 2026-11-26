#ifndef WORLD_PROGRESS_SCREEN_H
#define WORLD_PROGRESS_SCREEN_H

#include "Core/Progress/ProgressTypes.h"
#include "Gui/Core/GuiScreenBase.h"
#include <array>
#include <functional>
#include <string>

namespace cutum
{

class UGuiLabel;
class UGuiProgressBar;
class UGuiButton;
class UApplication;

struct WorldProgressScreenStorage
{
  size_t screen_bytes{}, widgets{}, widget_bytes{}, children_bytes{}, text_bytes{};
  size_t callback_bytes{}, requested{};
  std::array<bool, 2> callback_present{}, callback_supported{};
  bool bindings_match{}, supported{};
};

class UWorldProgressScreen : public UGuiScreenBase
{
public:
  void Build(UGuiContext &ctx) override;
  void OnViewportChanged(int width, int height) override;
  bool BlocksGameInput() const override { return true; }

  void ApplySnapshot(const ProgressSnapshot &snapshot);
  void SetFailureActions(std::function<void()> retry,
                         std::function<void()> back);
  // UI-thread snapshot of the live owned screen and its known widget tree.
  WorldProgressScreenStorage InspectOwnedStorage(const UGuiContext &context,
                                                  const UApplication *application) const noexcept;

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

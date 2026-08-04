#ifndef CHARACTER_SHEET_SCREEN_H
#define CHARACTER_SHEET_SCREEN_H

#include "Gui/Core/GuiScreenBase.h"
#include <memory>
#include <string>
#include <vector>

typedef unsigned int GLuint;

namespace cutum
{

class IUCharacterStatsViewModel;
class UCreaturePreviewRenderer;
class UGuiPreviewViewport;
class UGuiSlot;
class UGuiWindow;
class UGuiPanel;
class UGuiLabel;
class IUGuiIconSource;
struct GuiTheme;

class UCharacterSheetScreen : public UGuiScreenBase
{
public:
  explicit UCharacterSheetScreen(IUCharacterStatsViewModel *stats,
                                 UCreaturePreviewRenderer *creaturePreview,
                                 IUGuiIconSource *icons = nullptr);
  ~UCharacterSheetScreen() override;

  void Build(UGuiContext &ctx) override;
  void Update(double dt) override;
  void OnViewportChanged(int width, int height) override;
  bool BlocksGameInput() const override { return Visible; }

  void SetVisible(bool visible);
  void Toggle();

private:
  void Relayout();
  void RefreshLabels();
  void RenderCharacterPreview();
  static std::string FormatBar(const char *name, float cur, float max);

  IUCharacterStatsViewModel *Stats{nullptr};
  UCreaturePreviewRenderer *CreaturePreview{nullptr};
  IUGuiIconSource *Icons{nullptr};
  UGuiWindow *Window{nullptr};
  UGuiPreviewViewport *PreviewViewport{nullptr};
  std::vector<UGuiSlot *> ArmorSlots;
  std::vector<UGuiSlot *> ToolSlots;
  GLuint PreviewTexture{0};
  bool PreviewValid{false};
  std::string PreviewTypeId;
  std::string PreviewSkinId;
  float LastPreviewYaw{0.f};
  float LastPreviewPitch{0.f};
  int LastPreviewSize{0};
  UGuiLabel *TitleMeta{nullptr};
  UGuiLabel *ModeLabel{nullptr};
  std::vector<UGuiLabel *> VitalLabels;
  std::vector<UGuiLabel *> AttrLabels;
  bool Visible{false};
  bool Built{false};
};

} // namespace cutum

#endif

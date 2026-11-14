#ifndef CHARACTER_SHEET_SCREEN_H
#define CHARACTER_SHEET_SCREEN_H

#include "Creatures/Core/HumanAppearance.h"
#include "Gui/Core/GuiScreenBase.h"
#include "Gui/Core/GuiTypes.h"
#include <cstdint>
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
class UGuiButton;
class UGuiTabBar;
class UGuiTextInput;
class UGuiScrollView;
class IUGuiIconSource;
struct GuiTheme;
struct CharacterStatsSnapshot;

class UCharacterSheetScreen : public UGuiScreenBase
{
public:
  explicit UCharacterSheetScreen(IUCharacterStatsViewModel *stats,
                                 UCreaturePreviewRenderer *creaturePreview,
                                 IUGuiIconSource *icons = nullptr);
  ~UCharacterSheetScreen() override;

  bool RouteTextKey(const GuiKeyEvent &);
  bool RouteTextChar(const GuiCharEvent &);
  bool IsSearchFocused() const;
  void Build(UGuiContext &ctx) override;
  void Update(double dt) override;
  void OnViewportChanged(int width, int height) override;
  bool BlocksGameInput() const override { return Visible; }

  void SetVisible(bool visible);
  void Toggle();
  GuiRect GetMainSlotBounds() const;
  GuiRect GetDropButtonBounds() const;
  GuiRect GetDeleteButtonBounds() const;
  void SetSection(int section);
  int GetSection() const { return Section; }
  std::string GetSectionStateKey() const;
  GuiRect GetSearchBounds() const;
  GuiRect GetRecoveryButtonBounds() const;
  GuiRect GetBackpackSlotBounds(size_t index) const;
  GLuint GetBackpackIconTexture(size_t index) const;
  void SetHosted(bool hosted)
  {
    Hosted = hosted;
    Relayout();
  }
  bool PickBackpackSlot(int x, int y, size_t &slot) const;
  bool IsEditingText() const;
  /// Opt-in native QA exercises the same buttons and session commands as user
  /// input.
  void SetPointerPosition(int x, int y)
  {
    PointerX = x;
    PointerY = y;
  }
  void RunInventoryQa(const std::string &reportPath);
  void RunAppearanceQa(const std::string &reportPath);

  /// Hit-test armor slots for inventory drag routing (0..5), returns false if
  /// miss.
  bool PickArmorSlot(int x, int y, size_t &outSlot) const;
  /// Hit-test Main (= active hotbar / right hand).
  bool PickMainSlot(int x, int y) const;
  /// Hit-test offhand slot (Tool2).
  bool PickOffhandSlot(int x, int y) const;
  bool PickDoll(int x, int y) const;

private:
  void Relayout();
  void BuildEquipmentPanel(UGuiPanel &body, const GuiTheme &theme);
  void RefreshLabels();
  void RefreshTooltip(const CharacterStatsSnapshot &snap);
  void RenderCharacterPreview();
  static std::string FormatBar(const char *name, float cur, float max);

  IUCharacterStatsViewModel *Stats{nullptr};
  UCreaturePreviewRenderer *CreaturePreview{nullptr};
  IUGuiIconSource *Icons{nullptr};
  UGuiWindow *Window{nullptr};

  int Section{1};
  bool RecoveryMode{false};
  size_t RecoveryOffset{0};
  UGuiTabBar *Sections{nullptr};
  UGuiTextInput *Search{nullptr};
  UGuiButton *SortButton{nullptr}, *RecoveryButton{nullptr},
      *DropButton{nullptr}, *DeleteButton{nullptr};
  UGuiLabel *InventoryHint{nullptr};
  UGuiLabel *Tooltip{nullptr};
  int PointerX{-1}, PointerY{-1};
  UGuiScrollView *StatsScroll{nullptr};
  std::vector<UGuiLabel *> EffectLabels;
  UGuiPanel *BodyPanel{nullptr};
  UGuiPanel *DollPanel{nullptr};
  UGuiPanel *StatsPanel{nullptr};
  UGuiLabel *EquipmentHeader{nullptr};
  UGuiLabel *VitalsHeader{nullptr};
  UGuiLabel *AttrsHeader{nullptr};

  UGuiPreviewViewport *PreviewViewport{nullptr};
  std::vector<UGuiSlot *> ArmorSlots;
  std::vector<UGuiSlot *> ToolSlots;
  UGuiPanel *BagPanel{nullptr};
  std::vector<UGuiSlot *> BagSlots;
  UGuiPanel *AppearancePanel{nullptr};
  UGuiButton *AppearanceToggle{nullptr};
  std::vector<UGuiButton *> AppearanceButtons;
  bool EditingAppearance{false};
  HumanAppearance DraftAppearance;
  std::string DraftSkin;
  std::string LastCachedAppearance;
  void ResetAppearanceDraft();
  void RefreshAppearanceControls();
  bool WideLayout{true};
  GLuint PreviewTexture{0};
  bool PreviewValid{false};
  std::string PreviewTypeId;
  std::string PreviewSkinId;
  float LastPreviewYaw{0.f};
  float LastPreviewPitch{0.f};
  int LastPreviewSize{0};
  std::string LastCachedTypeId;
  std::string LastCachedSkinId;
  std::string LastCachedArmorKey;
  std::string LastCachedWieldKey;
  UGuiLabel *TitleMeta{nullptr};
  UGuiLabel *ModeLabel{nullptr};
  std::vector<UGuiLabel *> VitalLabels;
  std::vector<UGuiLabel *> AttrLabels;
  bool Visible{false};
  bool Built{false};
  bool Hosted{false};
  bool DraftInitialized{false};
  uint64_t DraftOwnerId{0};
  std::string DraftBase;
};

} // namespace cutum

#endif

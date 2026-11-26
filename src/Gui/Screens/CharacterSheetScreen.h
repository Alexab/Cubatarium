#ifndef CHARACTER_SHEET_SCREEN_H
#define CHARACTER_SHEET_SCREEN_H

#include "Creatures/Core/HumanAppearance.h"
#include "Gui/Core/GuiScreenBase.h"
#include "Gui/Core/GuiTypes.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>

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
  struct PageWidgets
  {
    UGuiWindow *Window{};
    UGuiTabBar *Sections{};
    UGuiTextInput *Search{};
    UGuiButton *SortButton{}, *RecoveryButton{}, *DropButton{}, *DeleteButton{};
    UGuiLabel *InventoryHint{}, *Tooltip{};
    UGuiScrollView *StatsScroll{};
    UGuiPanel *BodyPanel{}, *DollPanel{}, *StatsPanel{}, *BagPanel{}, *AppearancePanel{};
    UGuiLabel *EquipmentHeader{}, *VitalsHeader{}, *AttrsHeader{}, *TitleMeta{}, *ModeLabel{};
    UGuiPreviewViewport *PreviewViewport{};
    UGuiButton *AppearanceToggle{};
    std::array<UGuiSlot *, 4> ArmorSlots{};
    std::array<UGuiSlot *, 2> ToolSlots{};
    std::array<UGuiSlot *, 40> BagSlots{};
    std::array<UGuiButton *, 6> AppearanceButtons{};
    std::array<UGuiLabel *, 7> VitalLabels{}, AttrLabels{};
  };

  static constexpr size_t FixedStatusRows = 16;
  void Relayout();
  void Relayout(PageWidgets &page, UGuiWidget &root, int width, int height);
  void BuildEquipmentPanel(PageWidgets &page, UGuiPanel &body, const GuiTheme &theme);
  void RefreshLabels();
  void RefreshStatusRows(const CharacterStatsSnapshot &snap);
  void RefreshTooltip(const CharacterStatsSnapshot &snap);
  void RenderCharacterPreview(const CharacterStatsSnapshot &snap);
  static std::string FormatBar(const char *name, float cur, float max);

  IUCharacterStatsViewModel *Stats{nullptr};
  UCreaturePreviewRenderer *CreaturePreview{nullptr};
  IUGuiIconSource *Icons{nullptr};
  PageWidgets Widgets;

  int Section{1};
  bool RecoveryMode{false};
  size_t RecoveryOffset{0};
  int PointerX{-1}, PointerY{-1};
  bool EditingAppearance{false};
  HumanAppearance DraftAppearance;
  std::string DraftSkin;
  std::string LastCachedAppearance;
  void ResetAppearanceDraft(const CharacterStatsSnapshot &snap, std::string base);
  void RefreshAppearanceControls(bool human);
  GLuint PreviewTexture{0};
  float LastPreviewYaw{0.f};
  float LastPreviewPitch{0.f};
  int LastPreviewSize{0};
  std::string LastCachedTypeId;
  std::string LastCachedSkinId;
  std::string LastCachedArmorKey;
  std::string LastCachedWieldKey;
  bool Visible{false};
  bool Hosted{false};
  bool DraftInitialized{false};
  uint64_t DraftOwnerId{0};
  std::string DraftBase;
};

} // namespace cutum

#endif

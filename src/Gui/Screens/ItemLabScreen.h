#ifndef ITEM_LAB_SCREEN_H
#define ITEM_LAB_SCREEN_H

#include "Gui/Core/GuiScreenBase.h"
#include "Gui/Core/GuiTypes.h"
#include <filesystem>
#include <string>
#include <vector>

namespace cutum
{
class UItemDefinitionStorage;
class UShaderManager;
class UItemPreviewRenderer;
class UGuiListView;
class UGuiTextInput;
class UGuiPreviewViewport;
class UGuiLabel;
class UGuiButton;

class UItemLabScreen : public UGuiScreenBase
{
public:
  UItemLabScreen(std::shared_ptr<UItemDefinitionStorage> items,
                 std::shared_ptr<UShaderManager> shaders);
  ~UItemLabScreen();
  void Build(UGuiContext &ctx) override;
  void Update(double dt) override;
  void OnViewportChanged(int width, int height) override;
  bool BlocksGameInput() const override { return true; }
  bool WantsClose() const { return CloseRequested; }
  void SelectItem(const std::string &id);
  void SetMode(int mode);
  bool HasCandidate() const { return Textures[1] != 0; }
  unsigned GetPreviewTexture(int index) const { return Textures[index]; }
  std::string GetSelectedId() const { return Selected; }
  size_t GetItemCount() const { return AllIds.size(); }
  bool LayoutValid() const;
  GuiRect GetModeBounds() const;
  GuiRect GetReloadBounds() const;
  GuiRect GetExportBounds() const;
  int GetMode() const { return Mode; }

private:
  void Layout();
  void RebuildList();
  void Refresh();
  void Export();
  std::shared_ptr<UItemDefinitionStorage> Items;
  std::unique_ptr<UItemPreviewRenderer> Preview;
  std::vector<std::string> AllIds, Filtered;
  UGuiListView *List{nullptr};
  UGuiTextInput *Search{nullptr};
  UGuiPreviewViewport *Views[2]{nullptr, nullptr};
  UGuiLabel *Title{nullptr}, *Captions[2]{nullptr, nullptr},
      *Info[2]{nullptr, nullptr}, *Status{nullptr};
  UGuiButton *ModeButton{nullptr}, *LightButton{nullptr},
      *ReloadButton{nullptr}, *ExportButton{nullptr}, *CloseButton{nullptr};
  unsigned Textures[2]{0, 0};
  std::filesystem::path Paths[2];
  std::string Selected{"bucket"};
  float Yaw{35}, Pitch{25};
  int Mode{0}, Light{0};
  bool Dirty{true}, CloseRequested{false};
};
} // namespace cutum
#endif

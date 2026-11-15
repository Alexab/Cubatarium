#ifndef ITEM_LAB_SCREEN_H
#define ITEM_LAB_SCREEN_H

#include "Gui/Core/GuiScreenBase.h"
#include "Gui/Core/GuiTypes.h"
#include "Items/ItemLabDiagnostics.h"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace cutum
{
class UItemDefinitionStorage;
class UShaderManager;
class UItemPreviewRenderer;
class UItemLabSceneRenderer;
class UGuiListView;
class UGuiTextInput;
class UGuiPreviewViewport;
class UGuiLabel;
class UGuiButton;

class UItemLabScreen : public UGuiScreenBase
{
public:
  UItemLabScreen(std::shared_ptr<UItemDefinitionStorage> items,
                 std::shared_ptr<UShaderManager> shaders,
                 std::unique_ptr<UItemLabSceneRenderer> scenes);
  ~UItemLabScreen();
  void Build(UGuiContext &ctx) override;
  void Update(double dt) override;
  void OnViewportChanged(int width, int height) override;
  bool BlocksGameInput() const override { return true; }
  bool WantsClose() const { return CloseRequested; }
  void SelectItem(const std::string &id);
  void SetMode(int mode);
  void SetScene(int scene);
  void SetActor(int actor);
  void SetCamera(int camera);
  int GetScene() const { return Scene; }
  size_t GetSceneDrawCount(int panel) const { return SceneDraws[panel]; }
  bool SceneSourceMatches(int panel) const { return SceneSources[panel]; }
  bool BowSupportContact(int panel) const { return SceneBowContacts[panel]; }
  size_t DiagnosticVertices(int panel) const { return SceneDiagnostics[panel]; }
  bool DiagnosticsFramed(int panel) const
  {
    return SceneDiagnosticsFramed[panel];
  }
  const ItemLabResourceMetrics &ResourceMetrics(int panel) const
  {
    return PanelMetrics[panel];
  }
  const ItemVisualInputs &VisualInputs(int panel) const
  {
    return PanelInputs[panel];
  }
  GuiRect GetSceneBounds() const;
  void SetPosePreset(int pose);
  void SetDrawFraction(float value);
  void SetUsePhase(float value);
  void SetFillFraction(float value);
  void SetAmmunition(bool value);
  void SetStateAnimation(int channel);
  GuiRect GetStateControlBounds(int channel) const;
  int GetPosePreset() const { return PosePreset; }
  GuiRect GetPoseBounds() const;
  GuiRect GetMotionBounds() const;
  bool UsesArchivedReference() const { return ArchivedReference; }
  bool UsesStagedCandidate() const { return StagedCandidate; }
  std::filesystem::path GetReferencePath() const { return Paths[0]; }
  std::filesystem::path GetCandidatePath() const { return Paths[1]; }
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
  void RefreshStateControls();
  void Export();
  nlohmann::json ExportPanelMetadata(int panel);
  std::shared_ptr<UItemDefinitionStorage> Items;
  std::unique_ptr<UItemPreviewRenderer> Preview;
  std::unique_ptr<UItemLabSceneRenderer> ScenesRenderer;
  std::vector<std::string> AllIds, Filtered;
  UGuiListView *List{nullptr};
  UGuiTextInput *Search{nullptr};
  UGuiPreviewViewport *Views[2]{nullptr, nullptr};
  UGuiLabel *Title{nullptr}, *Captions[2]{nullptr, nullptr},
      *Info[2]{nullptr, nullptr}, *Status{nullptr};
  UGuiButton *ModeButton{nullptr}, *LightButton{nullptr}, *PoseButton{nullptr},
      *ReloadButton{nullptr}, *ExportButton{nullptr}, *CloseButton{nullptr},
      *SceneButton{nullptr}, *ActorButton{nullptr}, *CameraButton{nullptr},
      *MotionButton{nullptr};
  UGuiButton *StateButtons[5]{nullptr, nullptr, nullptr, nullptr, nullptr};
  unsigned Textures[2]{0, 0};
  size_t SceneDraws[2]{0, 0};
  bool SceneSources[2]{false, false};
  bool SceneBowContacts[2]{false, false};
  size_t SceneDiagnostics[2]{0, 0};
  bool SceneDiagnosticsFramed[2]{true, true};
  ItemLabResourceMetrics PanelMetrics[2];
  ItemVisualInputs PanelInputs[2];
  std::map<std::filesystem::path, ItemLabResourceMetrics> MetricsCache;
  std::filesystem::path Paths[2], PublishedPath;
  bool ArchivedReference{false}, StagedCandidate{false};
  std::string Selected{"bucket"};
  float Yaw{35}, Pitch{25};
  int Mode{0}, Light{0}, PosePreset{0}, Scene{0}, Actor{0}, Camera{0},
      Motion{0};
  float AnimationTime{0.f};
  float DrawFraction{0.f}, UsePhase{0.f}, FillFraction{0.f}, StateTime{0.f};
  bool Ammunition{false};
  int StateAnimation{0};
  int FillCapacity{1};
  bool SupportsDraw{false}, SupportsUse{false}, SupportsFill{false};
  bool Dirty{true}, CloseRequested{false};
};
} // namespace cutum
#endif

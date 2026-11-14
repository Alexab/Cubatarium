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
  int GetScene() const { return Scene; }
  size_t GetSceneDrawCount(int panel) const { return SceneDraws[panel]; }
  bool SceneSourceMatches(int panel) const { return SceneSources[panel]; }
  GuiRect GetSceneBounds() const;
  void SetPosePreset(int pose);
  int GetPosePreset() const { return PosePreset; }
  GuiRect GetPoseBounds() const;
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
  void Export();
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
  unsigned Textures[2]{0, 0};
  size_t SceneDraws[2]{0, 0};
  bool SceneSources[2]{false, false};
  std::filesystem::path Paths[2], PublishedPath;
  bool ArchivedReference{false}, StagedCandidate{false};
  std::string Selected{"bucket"};
  float Yaw{35}, Pitch{25};
  int Mode{0}, Light{0}, PosePreset{0}, Scene{0}, Actor{0}, Camera{0},
      Motion{0};
  float AnimationTime{0.f};
  bool Dirty{true}, CloseRequested{false};
};
} // namespace cutum
#endif

#include "Gui/Screens/ItemLabScreen.h"

#include "App/Platform/IUPlatformPaths.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Preview/ItemLabSceneRenderer.h"
#include "Gui/Preview/ItemPreviewRenderer.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiListView.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiPreviewViewport.h"
#include "Gui/Widgets/GuiTextInput.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ItemGltfTextureCache.h"
#include "Items/ItemLabAssets.h"
#include "Items/ItemRenderAsset.h"
#include "Render/GlIncludes.h"
#include "Render/Pipeline/GlStateScope.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

namespace cutum
{
namespace
{
const char *Modes[] = {"Lit",        "Silhouette", "Gray",     "Albedo",
                       "UV checker", "Normals",    "Wireframe"};
const char *Lights[] = {"Studio", "Dim", "Bright"};
const char *Poses[] = {"Rest", "Half", "Peak"};
const char *Scenes[] = {"Orbit", "FP",    "TP",       "Ground",
                        "Armor", "Icons", "Contacts", "UV"};
const char *Actors[] = {"Male / R", "Male / L", "Female / R", "Female / L"};
const char *Cameras[] = {"Orbit", "Front", "Back", "Side", "Top"};
const char *Motions[] = {"Still", "Walk pose", "Play"};
std::string Lower(std::string value)
{
  for (auto &c : value)
    c = char(std::tolower(static_cast<unsigned char>(c)));
  return value;
}
} // namespace
UItemLabScreen::UItemLabScreen(std::shared_ptr<UItemDefinitionStorage> items,
                               std::shared_ptr<UShaderManager> shaders,
                               std::unique_ptr<UItemLabSceneRenderer> scenes)
    : Items(std::move(items)), Preview(std::make_unique<UItemPreviewRenderer>(
                                   Items, std::move(shaders))),
      ScenesRenderer(std::move(scenes))
{
  if (!Preview->Initialize())
    throw std::runtime_error("Cannot initialize Item Lab renderer");
}
UItemLabScreen::~UItemLabScreen()
{
  for (auto &texture : Textures)
    if (texture)
      glDeleteTextures(1, &texture);
}
void UItemLabScreen::Build(UGuiContext &ctx)
{
  const auto *theme = &ctx.GetTheme();
  auto root = std::make_unique<UGuiPanel>(theme);
  auto add = [&](auto widget)
  {
    auto *p = widget.get();
    root->AddChild(std::move(widget));
    return p;
  };
  Title = add(std::make_unique<UGuiLabel>(
      theme, "Item Lab / read-only studio / Ctrl+F8 or Esc to close"));
  Search = add(std::make_unique<UGuiTextInput>(theme));
  Search->SetClipboard(ctx.GetClipboard());
  Search->SetOnEdited([this] { RebuildList(); });
  List = add(std::make_unique<UGuiListView>(theme));
  List->SetOnSelectionChanged(
      [this](int i)
      {
        if (i >= 0 && size_t(i) < Filtered.size())
          SelectItem(Filtered[size_t(i)]);
      });
  for (int i = 0; i < 2; ++i)
  {
    Captions[i] = add(std::make_unique<UGuiLabel>(
        theme, i == 0 ? "Published" : "Staged candidate"));
    Views[i] = add(std::make_unique<UGuiPreviewViewport>(theme));
    Views[i]->SetAngles(Yaw, Pitch);
    Views[i]->SetOnRotationChanged(
        [this](float yaw, float pitch)
        {
          Camera = 0;
          CameraButton->SetLabel(std::string("Cam: ") + Cameras[0]);
          Yaw = yaw;
          Pitch = pitch;
          Dirty = true;
          for (auto *v : Views)
            v->SetAngles(yaw, pitch);
        });
    Info[i] = add(std::make_unique<UGuiLabel>(theme, ""));
  }
  ModeButton = add(std::make_unique<UGuiButton>(theme, Modes[Mode]));
  ModeButton->SetOnClick([this] { SetMode((Mode + 1) % 7); });
  PoseButton = add(std::make_unique<UGuiButton>(theme, Poses[PosePreset]));
  PoseButton->SetOnClick([this] { SetPosePreset((PosePreset + 1) % 3); });
  LightButton = add(std::make_unique<UGuiButton>(theme, Lights[Light]));
  LightButton->SetOnClick(
      [this]
      {
        Light = (Light + 1) % 3;
        LightButton->SetLabel(Lights[Light]);
        Dirty = true;
      });
  SceneButton = add(
      std::make_unique<UGuiButton>(theme, std::string("3D: ") + Scenes[Scene]));
  SceneButton->SetOnClick([this] { SetScene((Scene + 1) % 8); });
  ActorButton = add(std::make_unique<UGuiButton>(theme, Actors[Actor]));
  ActorButton->SetOnClick([this] { SetActor((Actor + 1) % 4); });
  CameraButton = add(std::make_unique<UGuiButton>(theme, std::string("Cam: ") +
                                                             Cameras[Camera]));
  CameraButton->SetOnClick(
      [this]
      {
        Camera = (Camera + 1) % 5;
        CameraButton->SetLabel(std::string("Cam: ") + Cameras[Camera]);
        const float yaw[] = {35, 0, 180, 90, 35},
                    pitch[] = {25, 12, 12, 12, 75};
        Yaw = yaw[Camera];
        Pitch = pitch[Camera];
        for (auto *view : Views)
          view->SetAngles(Yaw, Pitch);
        Dirty = true;
      });
  MotionButton = add(std::make_unique<UGuiButton>(theme, Motions[Motion]));
  MotionButton->SetOnClick(
      [this]
      {
        Motion = (Motion + 1) % 3;
        MotionButton->SetLabel(Motions[Motion]);
        AnimationTime = 0;
        Dirty = true;
      });
  ReloadButton = add(std::make_unique<UGuiButton>(theme, "Reload"));
  ReloadButton->SetOnClick(
      [this]
      {
        Preview->Invalidate();
        MetricsCache.clear();
        ScenesRenderer->Invalidate();
        ClearItemSurfaceAssets();
        ItemGltfTextureCache::Instance().Clear();
        UItemDefinitionStorage::NotifyVisualAssetsChanged();
        Dirty = true;
      });
  ExportButton = add(std::make_unique<UGuiButton>(theme, "Export"));
  ExportButton->SetOnClick([this] { Export(); });
  CloseButton = add(std::make_unique<UGuiButton>(theme, "Close"));
  CloseButton->SetOnClick([this] { CloseRequested = true; });
  Status = add(std::make_unique<UGuiLabel>(
      theme, "Drag view to rotate. Export PNG/JSON."));
  Root = std::move(root);
  AllIds = Items->ListCatalogIds();
  std::sort(AllIds.begin(), AllIds.end());
  RebuildList();
  Layout();
}
void UItemLabScreen::SelectItem(const std::string &id)
{
  if (std::find(AllIds.begin(), AllIds.end(), id) == AllIds.end())
    return;
  Selected = id;
  const auto row = std::find(Filtered.begin(), Filtered.end(), id);
  if (List && row != Filtered.end())
    List->SetSelectedIndex(int(row - Filtered.begin()));
  Dirty = true;
}
void UItemLabScreen::SetScene(int scene)
{
  Scene = std::clamp(scene, 0, 7);
  if (SceneButton)
    SceneButton->SetLabel(Scene >= 6 ? Scenes[Scene]
                                     : std::string("3D: ") + Scenes[Scene]);
  if (CameraButton)
  {
    CameraButton->SetEnabled(Scene != 1 && Scene != 5 && Scene != 7);
    CameraButton->SetLabel(Scene == 1   ? "FP camera"
                           : Scene == 7 ? "UV sheet"
                           : Scene == 5
                               ? "Icon camera"
                               : std::string("Cam: ") + Cameras[Camera]);
  }
  if (ModeButton)
    ModeButton->SetEnabled(Scene != 7);
  if (ActorButton)
    ActorButton->SetEnabled(Scene != 7);
  if (PoseButton)
    PoseButton->SetEnabled(Scene != 7);
  if (MotionButton)
    MotionButton->SetEnabled(Scene != 7);
  if (LightButton)
    LightButton->SetEnabled(Scene != 7);
  for (auto *view : Views)
    if (view)
      view->SetEnabled(Scene != 7);
  Dirty = true;
}
void UItemLabScreen::SetActor(int actor)
{
  Actor = std::clamp(actor, 0, 3);
  if (ActorButton)
    ActorButton->SetLabel(Actors[Actor]);
  Dirty = true;
}
GuiRect UItemLabScreen::GetSceneBounds() const
{
  return SceneButton->GetBounds();
}
void UItemLabScreen::SetMode(int mode)
{
  Mode = std::clamp(mode, 0, 6);
  if (ModeButton)
    ModeButton->SetLabel(Modes[Mode]);
  Dirty = true;
}
void UItemLabScreen::SetPosePreset(int pose)
{
  PosePreset = std::clamp(pose, 0, 2);
  if (PoseButton)
    PoseButton->SetLabel(Poses[PosePreset]);
  Dirty = true;
}
GuiRect UItemLabScreen::GetPoseBounds() const
{
  return PoseButton->GetBounds();
}
void UItemLabScreen::RebuildList()
{
  Filtered.clear();
  const auto query = Lower(Search->GetText());
  for (const auto &id : AllIds)
    if (Lower(id).find(query) != std::string::npos)
      Filtered.push_back(id);
  List->SetItems(Filtered);
  const auto found = std::find(Filtered.begin(), Filtered.end(), Selected);
  const int index = found == Filtered.end() ? 0 : int(found - Filtered.begin());
  List->SetSelectedIndex(Filtered.empty() ? -1 : index);
  if (!Filtered.empty())
    SelectItem(Filtered[size_t(index)]);
}
void UItemLabScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  Layout();
}
void UItemLabScreen::Layout()
{
  if (!Root)
    return;
  const int m = Scaled(8), row = Scaled(32), x = ContentOffsetX,
            y = ContentOffsetY;
  const int left = std::clamp(Scaled(260), 200, std::max(200, ViewportW / 3));
  const int right = x + left + m, width = ViewportW - left - 2 * m;
  Root->SetBounds({x, y, ViewportW, ViewportH});
  Title->SetBounds({x + m, y + m, ViewportW - 2 * m, row});
  Search->SetBounds({x + m, y + row + 2 * m, left - 2 * m, row});
  const int controlsY = y + ViewportH - 5 * (row + m) - m;
  const int listY = y + 2 * row + 3 * m,
            listH = std::max(Scaled(24), controlsY - listY - m);
  List->SetVisibleRowCount(std::clamp(listH / std::max(1, Scaled(24)), 1, 10));
  List->SetBounds({x + m, listY, left - 2 * m, listH});
  List->SetSelectedIndex(List->GetSelectedIndex());
  UGuiButton *buttons[] = {
      ModeButton,   PoseButton,   LightButton,  SceneButton,  ActorButton,
      CameraButton, MotionButton, ReloadButton, ExportButton, CloseButton};
  const int column = (left - 3 * m) / 2;
  for (int i = 0; i < 10; ++i)
    buttons[i]->SetBounds({x + m + (i % 2) * (column + m),
                           controlsY + (i / 2) * (row + m), column, row});
  const int previewY = y + 2 * row + 3 * m,
            previewH = std::max(row, ViewportH - 5 * row - 6 * m);
  for (int i = 0; i < 2; ++i)
  {
    const int px = right + i * width / 2;
    Captions[i]->SetBounds({px, y + row + 2 * m, width / 2 - m, row});
    Views[i]->SetBounds({px, previewY, width / 2 - m, previewH});
    Info[i]->SetBounds({px, previewY + previewH + m, width / 2 - m, 2 * row});
  }
  Status->SetBounds({right, y + ViewportH - row - m, width, row});
}
void UItemLabScreen::Update(double dt)
{
  if (Motion == 2)
  {
    AnimationTime += float(dt);
    Dirty = true;
  }
  if (Dirty && Root)
  {
    Dirty = false;
    Refresh();
  }
}
void UItemLabScreen::Refresh()
{
  auto *platform = IUPlatformPaths::TryGet();
  if (!platform)
    return;
  const auto *definition = Items->Get(Selected);
  if (!definition)
    return;
  const auto assets = ResolveItemLabAssets(platform->AssetRoot(),
                                           definition->ModelPath, Selected);
  PublishedPath = assets.Published;
  Paths[0] = assets.Reference;
  Paths[1] = assets.Candidate;
  ArchivedReference = assets.Archived;
  StagedCandidate = assets.Staged;
  Captions[0]->SetText(ArchivedReference ? "Archived reference"
                                         : "Published reference");
  Captions[1]->SetText(StagedCandidate     ? "Staged candidate"
                       : ArchivedReference ? "Published candidate"
                                           : "No staged candidate");
  Title->SetText("Item Lab / " + Selected + " / Ctrl+F8 or Esc closes");
  for (int i = 0; i < 2; ++i)
  {
    try
    {
      unsigned next = 0;
      SceneDraws[i] = 0;
      SceneSources[i] = false;
      SceneBowContacts[i] = false;
      SceneDiagnostics[i] = 0;
      SceneDiagnosticsFramed[i] = true;
      PanelMetrics[i] = {};
      if (std::filesystem::is_regular_file(Paths[i]))
      {
        const auto asset = Preview->GetInspectionAsset(Paths[i]);
        auto metrics = MetricsCache.find(Paths[i]);
        if (metrics == MetricsCache.end())
          metrics =
              MetricsCache
                  .emplace(Paths[i],
                           MeasureItemLabResources(
                               *asset,
                               [](const auto &path)
                               {
                                 int width = 0, height = 0, channels = 0;
                                 if (!stbi_info(path.string().c_str(), &width,
                                                &height, &channels))
                                   throw std::runtime_error(
                                       "Cannot inspect texture: " +
                                       path.string());
                                 return glm::ivec2(width, height);
                               }))
                  .first;
        PanelMetrics[i] = metrics->second;
        ItemLabSceneParams params;
        params.Scene = Scene;
        params.Mode = Mode;
        params.Left = Actor % 2 != 0;
        params.Female = Actor >= 2;
        params.Walk = Motion > 0;
        params.Time = Motion == 2 ? AnimationTime : PosePreset * .35f;
        params.Yaw = Yaw;
        params.Pitch = Pitch;
        params.Ambient = Light == 0 ? .65f : Light == 1 ? .12f : .9f;
        const float charge = Motion == 2
                                 ? (std::sin(AnimationTime * 2.f) + 1.f) * .5f
                                 : PosePreset * .5f;
        params.Inputs.BowCharge = definition->Ranged.Enabled ? charge : 0.f;
        params.Inputs.UsePhase =
            Motion == 2 ? std::fmod(AnimationTime, 1.f) : PosePreset * .25f;
        params.Inputs.HasAmmo = definition->Ranged.Enabled && charge > 0;
        params.Inputs.HasWater = false;
        for (const auto &binding : asset->Bindings)
          if (binding.Channel == ItemVisualChannel::FillUnits)
            params.Inputs.ContentsUnits =
                std::max(params.Inputs.ContentsUnits,
                         int(std::lround(binding.Maximum * charge)));
        params.Inputs.HasWater = params.Inputs.ContentsUnits > 0;
        next =
            ScenesRenderer->Render(*Preview, Selected, Paths[i], asset, params);
        PanelInputs[i] = params.Inputs;
        SceneDraws[i] = ScenesRenderer->DrawCount();
        SceneSources[i] = ScenesRenderer->SourceMatches();
        SceneBowContacts[i] = ScenesRenderer->BowSupportContact();
        SceneDiagnostics[i] = ScenesRenderer->DiagnosticVertices();
        SceneDiagnosticsFramed[i] = ScenesRenderer->DiagnosticsFramed();
        const auto &cost = PanelMetrics[i];
        const auto memory =
            "\nMesh " + std::to_string((cost.GeometryBytes + 1023) / 1024) +
            "K | Tex " + std::to_string((cost.RgbaMipBytes + 1023) / 1024) +
            "K";
        if (Scene == 5)
          Info[i]->SetText("32 / 48 / 64 px" + memory);
        else
          Info[i]->SetText(std::to_string(cost.Triangles) + " tri | " +
                           std::to_string(cost.Materials) + " mat" + memory);
      }
      else
        Info[i]->SetText(i == 0 ? "Missing reference model"
                                : "Missing staged candidate");
      if (Textures[i])
        glDeleteTextures(1, &Textures[i]);
      Textures[i] = next;
      Views[i]->SetPreviewTexture(next);
    }
    catch (const std::exception &e)
    {
      if (Textures[i])
        glDeleteTextures(1, &Textures[i]);
      Textures[i] = 0;
      Views[i]->SetPreviewTexture(0);
      Info[i]->SetText("Cannot render asset");
      Status->SetText(e.what());
    }
  }
  if (Textures[0] && Textures[1])
    Status->SetText(Scene == 7   ? "UV: albedo + indexed triangles"
                    : Scene == 6 ? "R red | L cyan | pull gold | box purple"
                    : Scene == 5 ? "Icons: 32 / 48 / 64 native pixels"
                    : Scene == 1 ? "FP: gameplay camera, 16:9"
                                 : "Drag view to rotate. Export PNG/JSON.");
}
bool UItemLabScreen::LayoutValid() const
{
  if (!Root || List->GetRowHeight() < Scaled(20))
    return false;
  for (const auto &c : Root->GetChildren())
  {
    const auto b = c->GetBounds(), r = Root->GetBounds();
    if (b.W <= 0 || b.H <= 0 || b.X < r.X || b.Y < r.Y ||
        b.X + b.W > r.X + r.W || b.Y + b.H > r.Y + r.H)
      return false;
  }
  if (Context)
    for (const auto *label :
         {Info[0], Info[1], Captions[0], Captions[1], Status})
    {
      if (label->GetPreferredHeight() > label->GetBounds().H)
        return false;
      std::istringstream lines(label->GetText());
      std::string line;
      while (std::getline(lines, line))
        if (Context->GetRenderer().MeasureTextWidth(line) +
                2 * Context->GetTheme().Padding >
            label->GetBounds().W)
          return false;
    }
  return true;
}
GuiRect UItemLabScreen::GetModeBounds() const
{
  return ModeButton->GetBounds();
}
GuiRect UItemLabScreen::GetReloadBounds() const
{
  return ReloadButton->GetBounds();
}
GuiRect UItemLabScreen::GetExportBounds() const
{
  return ExportButton->GetBounds();
}
nlohmann::json UItemLabScreen::ExportPanelMetadata(int panel)
{
  if (!Textures[panel])
    return {{"available", false}};
  const auto asset = Preview->GetInspectionAsset(Paths[panel]);
  auto result = ItemLabMetricsJson(PanelMetrics[panel]);
  result["available"] = true;
  result["scene_draws"] = SceneDraws[panel];
  result["diagnostic_line_vertices"] = SceneDiagnostics[panel];
  result["diagnostics_framed"] = SceneDiagnosticsFramed[panel];
  const auto vector = [](glm::vec3 p)
  { return nlohmann::json::array({p.x, p.y, p.z}); };
  const auto &grip = asset->Grip;
  result["contacts"] = {
      {"space", "asset physical metres; UV origin top-left"},
      {"right", grip.HasRightGrip ? vector(grip.RightGrip) : nlohmann::json()},
      {"left", grip.HasLeftGrip ? vector(grip.LeftGrip) : nlohmann::json()},
      {"support_node", grip.SupportGripNode}};
  if (const auto *definition = Items->Get(Selected))
    result["physical"] = {{"size", vector(definition->Physical.size)},
                          {"mass", definition->Physical.mass}};
  const auto &inputs = PanelInputs[panel];
  result["visual_inputs"] = {{"bow_charge", inputs.BowCharge},
                             {"has_ammo", inputs.HasAmmo},
                             {"has_water", inputs.HasWater},
                             {"use_phase", inputs.UsePhase},
                             {"contents_units", inputs.ContentsUnits}};
  result["nodes"] = nlohmann::json::array();
  const auto nodes = ResolveItemNodeTransforms(
      *asset, EvaluateItemVisualState(*asset, inputs));
  for (size_t index = 0; index < asset->Nodes.size(); ++index)
  {
    auto matrix = nlohmann::json::array();
    for (int column = 0; column < 4; ++column)
      for (int row = 0; row < 4; ++row)
        matrix.push_back(nodes[index][column][row]);
    result["nodes"].push_back(
        {{"name", asset->Nodes[index].Name}, {"matrix_column_major", matrix}});
  }
  return result;
}
void UItemLabScreen::Export()
{
  auto *platform = IUPlatformPaths::TryGet();
  if (!platform)
    return;
  const auto folder = platform->AssetRoot() / "item_lab_exports";
  std::filesystem::create_directories(folder);
  nlohmann::json report = {{"schema_version", 4},
                           {"id", Selected},
                           {"mode", Modes[Mode]},
                           {"pose", Poses[PosePreset]},
                           {"published", PublishedPath.generic_string()},
                           {"reference", Paths[0].generic_string()},
                           {"candidate", Paths[1].generic_string()},
                           {"archived_reference", ArchivedReference},
                           {"staged_candidate", StagedCandidate},
                           {"scene", Scenes[Scene]},
                           {"actor", Actors[Actor]},
                           {"motion", Motions[Motion]},
                           {"camera", Cameras[Camera]},
                           {"animation_time", AnimationTime},
                           {"yaw", Yaw},
                           {"pitch", Pitch},
                           {"read_only", true}};
  report["panels"] = nlohmann::json::array();
  try
  {
    for (int panel = 0; panel < 2; ++panel)
      report["panels"].push_back(ExportPanelMetadata(panel));
  }
  catch (const std::exception &error)
  {
    Status->SetText(error.what());
    return;
  }
  const std::string stem =
      Selected + "-" + std::to_string(Mode) +
      (Scene ? "-" + std::string(Scenes[Scene]) + "-" + std::to_string(Actor)
             : "");
  UGlStateScope scope(kGlMaskIconFbo);
  for (int i = 0; i < 2; ++i)
  {
    if (!Textures[i])
      continue;
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           Textures[i], 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
      glDeleteFramebuffers(1, &fbo);
      Status->SetText("Cannot read preview framebuffer");
      return;
    }
    std::vector<unsigned char> pixels(512 * 512 * 4);
    glReadPixels(0, 0, 512, 512, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (int y = 0; y < 256; ++y)
      std::swap_ranges(pixels.begin() + y * 512 * 4,
                       pixels.begin() + (y + 1) * 512 * 4,
                       pixels.begin() + (511 - y) * 512 * 4);
    const auto file =
        folder / (stem + (i == 0 ? "-reference.png" : "-candidate.png"));
    const bool saved = stbi_write_png(file.string().c_str(), 512, 512, 4,
                                      pixels.data(), 512 * 4) != 0;
    glDeleteFramebuffers(1, &fbo);
    if (!saved)
    {
      Status->SetText("Cannot save PNG");
      return;
    }
  }
  std::ofstream output(folder / (stem + ".json"));
  output << report.dump(2) << '\n';
  Status->SetText(output ? "Exported PNG and JSON to item_lab_exports"
                         : "Cannot save report");
}
} // namespace cutum

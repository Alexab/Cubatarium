#include "Gui/Screens/ItemLabScreen.h"

#include "App/Platform/IUPlatformPaths.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Preview/ItemPreviewRenderer.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiListView.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiPreviewViewport.h"
#include "Gui/Widgets/GuiTextInput.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ItemGltfTextureCache.h"
#include "Items/ItemRenderAsset.h"
#include "Render/GlIncludes.h"
#include "Render/Pipeline/GlStateScope.h"
#include "ThirdParty/stb_image_write.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>

namespace cutum
{
namespace
{
const char *Modes[] = {"Lit",        "Silhouette", "Gray",     "Albedo",
                       "UV checker", "Normals",    "Wireframe"};
const char *Lights[] = {"Studio", "Dim", "Bright"};
std::string Lower(std::string value)
{
  for (auto &c : value)
    c = char(std::tolower(static_cast<unsigned char>(c)));
  return value;
}
} // namespace
UItemLabScreen::UItemLabScreen(std::shared_ptr<UItemDefinitionStorage> items,
                               std::shared_ptr<UShaderManager> shaders)
    : Items(std::move(items)),
      Preview(std::make_unique<UItemPreviewRenderer>(Items, std::move(shaders)))
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
  LightButton = add(std::make_unique<UGuiButton>(theme, Lights[Light]));
  LightButton->SetOnClick(
      [this]
      {
        Light = (Light + 1) % 3;
        LightButton->SetLabel(Lights[Light]);
        Dirty = true;
      });
  ReloadButton = add(std::make_unique<UGuiButton>(theme, "Reload"));
  ReloadButton->SetOnClick(
      [this]
      {
        Preview->Invalidate();
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
      theme, "Drag either preview to rotate. Export writes PNG and JSON."));
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
  Dirty = true;
}
void UItemLabScreen::SetMode(int mode)
{
  Mode = std::clamp(mode, 0, 6);
  if (ModeButton)
    ModeButton->SetLabel(Modes[Mode]);
  Dirty = true;
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
  const int left = std::clamp(Scaled(220), 180, std::max(180, ViewportW / 4));
  const int right = x + left + m, width = ViewportW - left - 2 * m;
  Root->SetBounds({x, y, ViewportW, ViewportH});
  Title->SetBounds({x + m, y + m, ViewportW - 2 * m, row});
  Search->SetBounds({x + m, y + row + 2 * m, left - 2 * m, row});
  List->SetVisibleRowCount(4);
  List->SetBounds({x + m, y + 2 * row + 3 * m, left - 2 * m,
                   std::max(row, ViewportH - 2 * row - 4 * m)});
  UGuiButton *buttons[] = {ModeButton, LightButton, ReloadButton, ExportButton,
                           CloseButton};
  for (int i = 0; i < 5; ++i)
    buttons[i]->SetBounds(
        {right + i * width / 5, y + row + 2 * m, width / 5 - m, row});
  const int previewY = y + 3 * row + 3 * m;
  const int previewH = std::max(row, ViewportH - 5 * row - 6 * m);
  for (int i = 0; i < 2; ++i)
  {
    const int px = right + i * width / 2;
    Captions[i]->SetBounds({px, y + 2 * row + 3 * m, width / 2 - m, row});
    Views[i]->SetBounds({px, previewY, width / 2 - m, previewH});
    Info[i]->SetBounds({px, previewY + previewH + m, width / 2 - m, row});
  }
  Status->SetBounds({right, y + ViewportH - row - m, width, row});
}
void UItemLabScreen::Update(double)
{
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
  Paths[0] = platform->AssetRoot() / definition->ModelPath;
  Paths[1] = platform->AssetRoot().parent_path() /
             "assetgen_output/items/redesign" / Selected / "model.gltf";
  Title->SetText("Item Lab / " + Selected + " / Ctrl+F8 or Esc closes");
  for (int i = 0; i < 2; ++i)
  {
    try
    {
      unsigned next = 0;
      if (std::filesystem::is_regular_file(Paths[i]))
      {
        next = Preview->RenderInspectionTexture(Selected, Paths[i], 512, Yaw,
                                                Pitch, Mode,
                                                Light == 0   ? .65f
                                                : Light == 1 ? .12f
                                                             : .9f);
        const auto asset = Preview->GetInspectionAsset(Paths[i]);
        size_t triangles = 0, vertices = 0;
        for (const auto &p : asset->Primitives)
        {
          triangles += p.Indices.size() / 3;
          vertices += p.Vertices.size() / 8;
        }
        Info[i]->SetText(std::to_string(triangles) + " tri / " +
                         std::to_string(vertices) + " v / " +
                         std::to_string(asset->Materials.size()) + " mat");
      }
      else
        Info[i]->SetText("No staged design");
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
void UItemLabScreen::Export()
{
  auto *platform = IUPlatformPaths::TryGet();
  if (!platform)
    return;
  const auto folder = platform->AssetRoot() / "item_lab_exports";
  std::filesystem::create_directories(folder);
  nlohmann::json report = {{"id", Selected},
                           {"mode", Modes[Mode]},
                           {"yaw", Yaw},
                           {"pitch", Pitch},
                           {"read_only", true}};
  UGlStateScope scope(kGlMaskIconFbo);
  for (int i = 0; i < 2; ++i)
  {
    report[i == 0 ? "published" : "candidate"] = Paths[i].generic_string();
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
    const auto file = folder / (Selected + "-" + std::to_string(Mode) +
                                (i == 0 ? "-published.png" : "-candidate.png"));
    const bool saved = stbi_write_png(file.string().c_str(), 512, 512, 4,
                                      pixels.data(), 512 * 4) != 0;
    glDeleteFramebuffers(1, &fbo);
    if (!saved)
    {
      Status->SetText("Cannot save PNG");
      return;
    }
  }
  std::ofstream output(folder /
                       (Selected + "-" + std::to_string(Mode) + ".json"));
  output << report.dump(2) << '\n';
  Status->SetText(output ? "Exported PNG and JSON to item_lab_exports"
                         : "Cannot save report");
}
} // namespace cutum

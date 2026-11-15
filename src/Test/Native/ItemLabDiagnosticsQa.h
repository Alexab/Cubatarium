#pragma once
#include "Gui/Core/GuiContext.h"
#include "Gui/Screens/ItemLabScreen.h"
#include "Items/ItemGltfTextureCache.h"
#include "Items/ItemLabDiagnostics.h"
#include "Test/Native/PlayerWindowCapture.h"
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>

namespace cutum
{
inline uint64_t
InspectItemLabAllocatedTexels(const ItemLabResourceMetrics &metrics)
{
  UGlStateScope scope(kGlMaskIconFbo);
  glActiveTexture(GL_TEXTURE0);
  uint64_t bytes = 0;
  for (const auto &image : metrics.Textures)
  {
    const auto texture =
        ItemGltfTextureCache::Instance().GetLinearAlbedo(image.Path);
    glBindTexture(GL_TEXTURE_2D, texture);
    for (int level = 0; level < 32; ++level)
    {
      GLint width = 0, height = 0, format = 0;
      glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH, &width);
      glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_HEIGHT,
                               &height);
      if (width <= 0 || height <= 0)
        break;
      glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_INTERNAL_FORMAT,
                               &format);
      if (format != GL_SRGB8_ALPHA8)
        return 0;
      bytes += uint64_t(width) * uint64_t(height) * 4;
    }
  }
  return bytes;
}
inline bool SaveItemQaTexture(unsigned int texture,
                              const std::filesystem::path &path, int width,
                              int height);
inline void RunItemLabDiagnosticsQa(UItemLabScreen &lab, UGuiContext &gui,
                                    const std::vector<std::string> &ids,
                                    const std::filesystem::path &frames,
                                    nlohmann::json &report)
{
  auto check = [&](const std::string &name, bool ok)
  {
    report["checks"].push_back({{"name", name}, {"passed", ok}});
    if (!ok)
      report["passed"] = false;
  };
  lab.SetMode(0);
  lab.SetActor(0);
  lab.SetPosePreset(2);
  for (const auto &id : ids)
    for (int scene : {6, 7})
    {
      lab.SelectItem(id);
      lab.SetScene(scene);
      lab.Update(0);
      const auto label = id + (scene == 6 ? " contacts" : " UV");
      const auto &metrics = lab.ResourceMetrics(1);
      check(label + " candidate source and measured resources",
            lab.HasCandidate() && lab.SceneSourceMatches(1) &&
                metrics.GeometryBytes > 0 && metrics.RgbaMipBytes > 0 &&
                !metrics.Textures.empty());
      check(label + " real diagnostic vertices",
            scene == 6
                ? lab.DiagnosticVertices(1) >= 24 && lab.DiagnosticsFramed(1)
                : lab.DiagnosticVertices(1) == metrics.Triangles * 6);
      const auto allocated = InspectItemLabAllocatedTexels(metrics);
      check(label + " costs agree with actual GL mip allocation",
            allocated > 0 && allocated == metrics.RgbaMipBytes);
      const auto texturePath =
          frames / (id + (scene == 6 ? "-contacts.png" : "-uv.png"));
      check(label + " capture real target",
            SaveItemQaTexture(lab.GetPreviewTexture(1), texturePath, 512, 512));
      check(label + " capture real reference",
            SaveItemQaTexture(lab.GetPreviewTexture(0),
                              frames /
                                  (id + (scene == 6 ? "-contacts-reference.png"
                                                    : "-uv-reference.png")),
                              512, 512));
      report["diagnostics"][id][std::to_string(scene)] = {
          {"line_vertices", lab.DiagnosticVertices(1)},
          {"framed", lab.DiagnosticsFramed(1)},
          {"allocated_rgba8_mip_bytes", allocated},
          {"reference_path", lab.GetReferencePath().generic_string()},
          {"candidate_path", lab.GetCandidatePath().generic_string()},
          {"archived_reference", lab.UsesArchivedReference()},
          {"staged_candidate", lab.UsesStagedCandidate()},
          {"reference_resources", ItemLabMetricsJson(lab.ResourceMetrics(0))},
          {"resources", ItemLabMetricsJson(metrics)}};
    }
  lab.SelectItem("bucket");
  lab.SetScene(0);
  std::vector<char> emptyWater, fullWater;
  for (int pose : {0, 1, 2})
  {
    lab.SetPosePreset(pose);
    lab.Update(0);
    const auto &inputs = lab.VisualInputs(1);
    check("bucket water state " + std::to_string(pose),
          lab.HasCandidate() && inputs.HasWater == (pose > 0) &&
              (inputs.ContentsUnits > 0) == (pose > 0));
    const auto path =
        frames / ("bucket-water-" + std::to_string(pose) + ".png");
    check("bucket water capture " + std::to_string(pose),
          SaveItemQaTexture(lab.GetPreviewTexture(1), path, 512, 512));
    std::ifstream file(path, std::ios::binary);
    if (pose == 0)
      emptyWater = std::vector<char>(std::istreambuf_iterator<char>(file), {});
    if (pose == 2)
      fullWater = std::vector<char>(std::istreambuf_iterator<char>(file), {});
  }
  check("filled bucket changes actual rendered water pixels",
        !emptyWater.empty() && !fullWater.empty() && emptyWater != fullWater);
  lab.SelectItem("waterskin");
  lab.SetPosePreset(0);
  lab.SetFillFraction(1);
  lab.SetUsePhase(.75f);
  lab.Update(0);
  const int fullUnits = lab.VisualInputs(1).ContentsUnits;
  check("independent full container and use phase",
        fullUnits > 0 && lab.VisualInputs(1).HasWater &&
            lab.VisualInputs(1).UsePhase == .75f &&
            lab.VisualInputs(1).BowCharge == 0);
  lab.SetStateAnimation(2);
  lab.Update(.25);
  check("animated use preserves full contents and idle draw",
        lab.VisualInputs(1).ContentsUnits == fullUnits &&
            lab.VisualInputs(1).UsePhase == .25f &&
            lab.VisualInputs(1).BowCharge == 0);
  lab.SetStateAnimation(3);
  lab.SetUsePhase(.75f);
  lab.Update(.5);
  check("animated fill preserves manual use",
        lab.VisualInputs(1).ContentsUnits > 0 &&
            lab.VisualInputs(1).ContentsUnits < fullUnits &&
            lab.VisualInputs(1).UsePhase == .75f);
  lab.SetFillFraction(1);
  lab.Update(.5);
  check("manual fill cancels fill animation and stays full",
        lab.VisualInputs(1).ContentsUnits == fullUnits);
  lab.SelectItem("wood_bow");
  lab.SetPosePreset(0);
  lab.SetDrawFraction(1);
  lab.SetUsePhase(.75f);
  std::vector<char> noArrow, withArrow;
  for (bool ammo : {false, true})
  {
    lab.SetAmmunition(ammo);
    lab.Update(0);
    const auto &inputs = lab.VisualInputs(1);
    check(std::string("full bow independent ammo ") + (ammo ? "on" : "off"),
          inputs.BowCharge == 1 && inputs.HasAmmo == ammo &&
              inputs.UsePhase == .75f);
    const auto path = frames / (ammo ? "bow-with-ammo.png" : "bow-no-ammo.png");
    check("capture independent bow ammunition",
          SaveItemQaTexture(lab.GetPreviewTexture(1), path, 512, 512));
    std::ifstream file(path, std::ios::binary);
    (ammo ? withArrow : noArrow) =
        std::vector<char>(std::istreambuf_iterator<char>(file), {});
  }
  check("independent ammunition changes actual arrow pixels",
        !noArrow.empty() && !withArrow.empty() && noArrow != withArrow);
  lab.SetStateAnimation(1);
  lab.Update(.5);
  check("draw animation preserves ammunition and use",
        lab.VisualInputs(1).BowCharge > 0 &&
            lab.VisualInputs(1).BowCharge < 1 && lab.VisualInputs(1).HasAmmo &&
            lab.VisualInputs(1).UsePhase == .75f);
  lab.SetStateAnimation(0);
  lab.SetDrawFraction(1);
  lab.SetScene(7);
  lab.Update(0);
  lab.SetScene(0);
  lab.Update(0);
  check("static UV view preserves independently selected states",
        lab.VisualInputs(1).BowCharge == 1 && lab.VisualInputs(1).HasAmmo &&
            lab.VisualInputs(1).UsePhase == .75f);
  lab.SetPosePreset(0);
  lab.Update(0);
  check("rest preset resets custom states and animation",
        lab.VisualInputs(1).BowCharge == 0 && !lab.VisualInputs(1).HasAmmo &&
            lab.VisualInputs(1).UsePhase == 0);
  lab.SetDrawFraction(1);
  lab.SetUsePhase(1);
  lab.SetFillFraction(1);
  const int originalW = lab.GetViewportWidth(),
            originalH = lab.GetViewportHeight();
  const float originalScale = gui.GetUiScale();
  for (const auto size : {glm::ivec2(1280, 720), glm::ivec2(1920, 1080)})
    for (float scale : {1.f, 1.5f, 2.f})
      for (int scene : {6, 7})
      {
        gui.ApplyUiScale(scale);
        lab.OnViewportChanged(size.x, size.y);
        lab.SelectItem("wood_bow");
        lab.SetScene(scene);
        lab.Update(0);
        const auto label = std::string(scene == 6 ? "contacts-" : "uv-") +
                           std::to_string(size.x) + "x" +
                           std::to_string(size.y) + "-" +
                           std::to_string(int(scale * 100));
        check(label + " labels and layout fit", lab.LayoutValid());
        check(label + " capture UI",
              CapturePlayerWindow(gui, *lab.GetRoot(), size.x, size.y,
                                  frames / (label + ".png")));
      }
  gui.ApplyUiScale(originalScale);
  lab.OnViewportChanged(originalW, originalH);
  lab.SetPosePreset(0);
  lab.SetScene(0);
  lab.SelectItem("bucket");
  lab.Update(0);
}
} // namespace cutum

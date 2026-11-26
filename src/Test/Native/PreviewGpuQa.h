#pragma once

#include "App/Application.h"
#include "Gui/Cache/InventoryIconService.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Gui/Preview/PreviewRenderResources.h"
#include "Gui/Screens/ItemLabScreen.h"
#include "Items/FpViewmodelRenderer.h"
#include "Render/Engine/RenderImageStorage.h"
#include "Render/Engine/TerrainGpuBufferReport.h"
#include <fstream>

namespace cutum
{
inline nlohmann::json ObservePreviewReadback(const RenderImageSnapshot &images, GLuint texture,
                                             const std::filesystem::path &root)
{
  const auto source = std::find_if(images.images.begin(), images.images.end(), [&](const auto &image) {
    return image.target == GL_TEXTURE_2D && image.name == texture && !image.levels.empty();
  });
  if (source == images.images.end())
    return {{"status", "FAIL"}};
  const int size = source->levels.front().width;
  auto service = UInventoryIconService::Create();
  if (!service->Initialize(root) ||
      !service->StoreIconTexture("item", "native-preview-a", "", "preview-gpu", size, texture) ||
      !service->StoreIconTexture("item", "native-preview-b", "", "preview-gpu", size, texture))
    return {{"status", "FAIL"}};
  TerrainGpuBufferCensus live;
  service->InspectReadbackGpuBuffers(live);
  const auto queued = live.Read();
  const auto owner = service->InspectOwnedStorage();
  service->CancelPendingItemReadbacks();
  const auto retired = live.Read();
  TerrainGpuBufferCensus current;
  service->InspectReadbackGpuBuffers(current);
  const bool pass = owner.jobs == 2 && queued.total.objects == owner.jobs &&
      queued.total.bytes == owner.packBufferBytes && queued.total.bytes == 2 * uint64_t(size) * size * 4 &&
      !queued.failedQueries && retired.namesWithoutObject == owner.jobs && !retired.total.bytes &&
      !current.Read().references && glIsTexture(texture);
  return {{"status", pass ? "PASS" : "FAIL"}, {"source_side", size},
          {"queued", DescribeIconReadbackGpuBuffers(queued)}, {"retired_names", retired.namesWithoutObject},
          {"pack_buffer_bytes_upper", owner.packBufferBytesUpper}};
}

inline nlohmann::json ObserveWorldViewmodel(UFpViewmodelRenderer *renderer)
{
  if (!renderer)
    return {{"status", "FAIL"}};
  const auto sample = [&] {
    TerrainGpuBufferCensus out;
    renderer->InspectActorGpuBuffers(out);
    return out.Read();
  };
  const auto before = sample();
  const UGlStateScope state(kGlMaskFpViewmodel3D);
  PreviewRenderTarget target;
  if (!target.Initialize(128))
    return {{"status", "FAIL"}};
  InventoryEntryRef item;
  item.empty = false;
  item.kind = InventoryEntryKind::Item;
  item.Id = "bucket";
  item.count = 1;
  FpViewmodelDrawParams params;
  params.Active = &item;
  params.FramebufferW = params.FramebufferH = 128;
  params.TargetFramebuffer = target.Framebuffer;
  renderer->DrawWorldOverlay(params);
  const auto after = sample();
  const auto &vertex = after.roles[static_cast<std::size_t>(TerrainGpuBufferRole::ViewmodelSurfaceVertices)];
  const auto &index = after.roles[static_cast<std::size_t>(TerrainGpuBufferRole::ViewmodelSurfaceIndices)];
  const bool passed = vertex.objects && index.objects == vertex.objects && vertex.bytes && index.bytes &&
      !before.failedQueries && !after.failedQueries && glGetError() == GL_NO_ERROR;
  return {{"status", passed ? "PASS" : "FAIL"}, {"item", item.Id}, {"before", DescribeActorGpuBuffers(before)},
          {"after", DescribeActorGpuBuffers(after)}};
}

inline void RunItemLabPreviewGpuQa(UApplication &app, const char *path, UFpViewmodelRenderer *viewmodel)
{
  const auto census = [&] {
    TerrainGpuBufferCensus out;
    app.InspectPreviewGpuBuffers(out);
    return out;
  };
  const auto baseline = census().Read();
  auto report = nlohmann::json{{"schema", "item-lab-preview-gpu.v1"}, {"passed", false},
                               {"baseline", DescribePreviewGpuBuffers(baseline)}};
  app.ToggleItemLab();
  auto *lab = dynamic_cast<UItemLabScreen *>(app.GetGui().GetScreen());
  if (lab)
  {
    const auto opened = census().Read();
    lab->OnViewportChanged(1280, 720);
    lab->SelectItem("bucket");
    lab->SetScene(0);
    lab->Update(0);
    const auto orbit = census().Read();
    lab->SetScene(6);
    lab->Update(0);
    const auto contactCensus = census();
    const auto contacts = contactCensus.Read();
    const auto vertices = lab->DiagnosticVertices(0) + lab->DiagnosticVertices(1);
    RenderImageCensus labImages;
    lab->InspectRenderImages(labImages);
    const auto images = labImages.Read();
    const GLuint preview = lab->GetPreviewTexture(0) ? lab->GetPreviewTexture(0) : lab->GetPreviewTexture(1);
    report["icon_readback"] = ObservePreviewReadback(images, preview,
        std::filesystem::path(path).parent_path() / "preview-readback-cache");
    app.ToggleItemLab();
    const auto closed = census().Read();
    const auto oldBuffers = contactCensus.Read();
    const auto oldImages = labImages.Read();
    const auto bytes = [](const TerrainGpuBufferSnapshot &s, TerrainGpuBufferRole role) {
      return s.roles[static_cast<size_t>(role)].bytes;
    };
    report["opened"] = DescribePreviewGpuBuffers(opened);
    report["orbit"] = DescribePreviewGpuBuffers(orbit);
    report["contacts"] = DescribePreviewGpuBuffers(contacts);
    report["closed"] = DescribePreviewGpuBuffers(closed);
    report["contact_vertices"] = vertices;
    report["lab_images"] = images.images.size();
    report["lab_image_bytes"] = images.logicalBytes;
    report["retired_buffers"] = oldBuffers.namesWithoutObject;
    report["retired_images"] = oldImages.namesWithoutObject;
    report["passed"] = opened.total.objects > baseline.total.objects &&
        bytes(orbit, TerrainGpuBufferRole::PreviewSurfaceVertices) >
            bytes(opened, TerrainGpuBufferRole::PreviewSurfaceVertices) &&
        vertices && bytes(contacts, TerrainGpuBufferRole::PreviewOverlay) > 0 &&
        report["baseline"] == report["closed"] && images.logicalBytes &&
        oldImages.namesWithoutObject == images.images.size() &&
        oldBuffers.namesWithoutObject == contacts.total.objects - baseline.total.objects &&
        report["icon_readback"]["status"] == "PASS" &&
        !contacts.failedQueries && !images.failedQueries && !oldBuffers.failedQueries &&
        !oldImages.failedQueries && glGetError() == GL_NO_ERROR;
  }
  report["actual_viewmodel"] = ObserveWorldViewmodel(viewmodel);
  std::ofstream out(path);
  out << report.dump(2) << '\n';
}
} // namespace cutum

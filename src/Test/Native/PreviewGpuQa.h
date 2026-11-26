#pragma once

#include "App/Application.h"
#include "Gui/Screens/ItemLabScreen.h"
#include "Render/Engine/RenderImageStorage.h"
#include "Render/Engine/TerrainGpuBufferReport.h"
#include <fstream>

namespace cutum
{
inline void RunItemLabPreviewGpuQa(UApplication &app, const char *path)
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
        !contacts.failedQueries && !images.failedQueries && !oldBuffers.failedQueries &&
        !oldImages.failedQueries && glGetError() == GL_NO_ERROR;
  }
  std::ofstream out(path);
  out << report.dump(2) << '\n';
}
} // namespace cutum

#pragma once

#include "Gui/Preview/PreviewRenderResources.h"
#include "Items/ItemSurfaceRenderer.h"

namespace cutum
{

struct PreviewRendererStorage
{
  ItemSurfaceRendererStorage surface;
  size_t requestBytes{}, tableBytes{}, cpuBytes{}, assetReferences{};
  size_t meshes{}, vertexBytes{}, indexBytes{}, scratchVertexBytes{}, scratchIndexBytes{};
  size_t targetTexels{}, depthSamples{}, fallbackColorTexels{};
  GLuint framebuffer{}, colorTexture{}, depthBuffer{}, resolveFramebuffer{};
  bool present{}, factoryOwner{}, supported{};
};

inline bool InspectPreviewTarget(PreviewRendererStorage &out, const PreviewRenderTarget &target,
                                  int size) noexcept
{
  out.framebuffer = target.Framebuffer;
  out.colorTexture = target.Color;
  out.depthBuffer = target.Depth;
  if (!size)
    return !target.Framebuffer && !target.Color && !target.Depth;
  if (size < 1 || size > 512 || !target.Framebuffer || !target.Color || !target.Depth)
    return false;
  out.targetTexels = out.depthSamples = size_t(size) * size_t(size);
  return true;
}

inline bool InspectPreviewCube(PreviewRendererStorage &out, const PreviewCubeMesh &mesh) noexcept
{
  if (!mesh.Vao)
    return !mesh.Vbo && !mesh.Ebo;
  if (!mesh.Vbo || !mesh.Ebo || mesh.Vbo == mesh.Ebo)
    return false;
  ++out.meshes;
  out.vertexBytes += PreviewCubeMesh::kCubeVertexBytes;
  out.indexBytes += PreviewCubeMesh::kCubeIndexBytes;
  return true;
}

} // namespace cutum

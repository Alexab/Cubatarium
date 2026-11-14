#pragma once
#include "Gui/Core/GuiContext.h"
#include "Gui/Widgets/GuiWidget.h"
#include "Render/GlIncludes.h"
#include "Render/Pipeline/GlStateScope.h"
#include "ThirdParty/stb_image_write.h"
#include <algorithm>
#include <filesystem>
#include <vector>
namespace cutum
{
inline bool CapturePlayerWindow(UGuiContext &context, UGuiWidget &root,
                                int width, int height,
                                const std::filesystem::path &path)
{
  UGlStateScope state(kGlMaskIconFbo);
  GLuint fbo = 0, texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture, 0);
  bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  if (ok)
  {
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(.08f, .09f, .12f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    context.RenderOverlay(root, width, height, false);
    std::vector<unsigned char> pixels(size_t(width) * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (int y = 0; y < height / 2; ++y)
      std::swap_ranges(pixels.begin() + size_t(y) * width * 4,
                       pixels.begin() + size_t(y + 1) * width * 4,
                       pixels.begin() + size_t(height - 1 - y) * width * 4);
    std::filesystem::create_directories(path.parent_path());
    ok = stbi_write_png(path.string().c_str(), width, height, 4, pixels.data(),
                        width * 4) != 0;
  }
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &texture);
  return ok;
}
} // namespace cutum

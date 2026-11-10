#include "Render/Backend/RenderBackendCaps.h"
#include "Render/GlIncludes.h"
#include "glog/logging.h"

#include <cctype>
#include <cstring>
#include <string>

namespace cutum
{
namespace
{

bool ParseMajorMinor(const char *s, int &major, int &minor)
{
  major = 0;
  minor = 0;
  if (!s || !*s)
  {
    return false;
  }
  // Skip "OpenGL ES " prefix if present.
  const char *p = s;
  if (std::strncmp(p, "OpenGL ES ", 10) == 0)
  {
    p += 10;
  }
  while (*p && !std::isdigit(static_cast<unsigned char>(*p)))
  {
    ++p;
  }
  if (!*p)
  {
    return false;
  }
  major = 0;
  while (*p && std::isdigit(static_cast<unsigned char>(*p)))
  {
    major = major * 10 + (*p - '0');
    ++p;
  }
  if (*p == '.')
  {
    ++p;
    minor = 0;
    while (*p && std::isdigit(static_cast<unsigned char>(*p)))
    {
      minor = minor * 10 + (*p - '0');
      ++p;
    }
  }
  return major > 0;
}

bool ExtensionPresent(const char *name)
{
  if (!name)
  {
    return false;
  }
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  GLint n = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &n);
  for (GLint i = 0; i < n; ++i)
  {
    const char *ext =
        reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, i));
    if (ext && std::strcmp(ext, name) == 0)
    {
      return true;
    }
  }
  return false;
#else
  return glewIsExtensionSupported(name) == GL_TRUE;
#endif
}

} // namespace

void ProbeOpenGLRenderBackendCaps(RenderBackendCaps &caps)
{
  const char *version =
      reinterpret_cast<const char *>(glGetString(GL_VERSION));
  const char *renderer =
      reinterpret_cast<const char *>(glGetString(GL_RENDERER));
  caps.GlVersion = version ? version : "";
  caps.GlRenderer = renderer ? renderer : "";

  int major = 0;
  int minor = 0;
  if (!ParseMajorMinor(version, major, minor))
  {
    LOG(WARNING) << "[RenderBackendCaps] failed to parse GL_VERSION=\""
                 << caps.GlVersion << "\"";
    caps.ProbeCompleted = true;
    return;
  }

  const bool is_gles = caps.Platform == RenderPlatformKind::Android ||
                       (version && std::strstr(version, "OpenGL ES") != nullptr);

  if (is_gles)
  {
    // GLES 3.1+ has compute + SSBO in core.
    const bool gles31 = major > 3 || (major == 3 && minor >= 1);
    caps.HasCompute = gles31;
    caps.HasSsbo = gles31;
    caps.HasGlMapBufferRange = major > 3 || (major == 3 && minor >= 0);
    caps.HasMultiDrawIndirect =
        ExtensionPresent("GL_EXT_multi_draw_indirect") ||
        ExtensionPresent("GL_ARB_multi_draw_indirect");
    caps.PreferSinglePassTransparent = true;
  }
  else
  {
    const bool gl43 = major > 4 || (major == 4 && minor >= 3);
    caps.HasCompute = gl43 || ExtensionPresent("GL_ARB_compute_shader");
    caps.HasSsbo =
        gl43 || ExtensionPresent("GL_ARB_shader_storage_buffer_object");
    caps.HasMultiDrawIndirect =
        ExtensionPresent("GL_ARB_multi_draw_indirect") ||
        (major > 4 || (major == 4 && minor >= 3));
    caps.HasGlMapBufferRange = true;
    caps.PreferSinglePassTransparent = false;
  }

  caps.ProbeCompleted = true;
  LOG(INFO) << "[RenderBackendCaps] probe version=\"" << caps.GlVersion
            << "\" renderer=\"" << caps.GlRenderer
            << "\" compute=" << caps.HasCompute << " ssbo=" << caps.HasSsbo
            << " mdi=" << caps.HasMultiDrawIndirect;
}

void RefreshRenderBackendCapsFromGl()
{
  RenderBackendCaps caps = DetectRenderBackendCaps();
  ProbeOpenGLRenderBackendCaps(caps);
  SetActiveRenderBackendCaps(caps);
}

} // namespace cutum

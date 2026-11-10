#include "Render/Backend/AndroidGpuPolicy.h"
#include "glog/logging.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace cutum
{
namespace
{

std::string ToLower(std::string s)
{
  for (char &c : s)
  {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

bool ParseMinGles(const std::string &s, int &major, int &minor)
{
  major = 3;
  minor = 1;
  if (s.empty())
  {
    return true;
  }
  const size_t dot = s.find('.');
  try
  {
    major = std::stoi(s.substr(0, dot));
    minor = (dot == std::string::npos) ? 0 : std::stoi(s.substr(dot + 1));
  }
  catch (...)
  {
    return false;
  }
  return true;
}

bool VersionAtLeast(const std::string &gl_version, int need_maj, int need_min)
{
  int maj = 0;
  int min = 0;
  const char *p = gl_version.c_str();
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
  maj = std::atoi(p);
  while (*p && std::isdigit(static_cast<unsigned char>(*p)))
  {
    ++p;
  }
  if (*p == '.')
  {
    ++p;
    min = std::atoi(p);
  }
  return maj > need_maj || (maj == need_maj && min >= need_min);
}

} // namespace

AndroidGpuAllowlistConfig LoadAndroidGpuAllowlist(const char *path)
{
  AndroidGpuAllowlistConfig cfg;
  if (!path || !*path)
  {
    return cfg;
  }
  std::ifstream in(path);
  if (!in)
  {
    return cfg;
  }
  // Minimal JSON-ish parse: look for known keys (avoid pulling nlohmann into
  // backend-only unit tests that may not link Core).
  std::stringstream buf;
  buf << in.rdbuf();
  const std::string text = buf.str();
  auto find_bool = [&](const char *key, bool def) {
    const std::string k = std::string("\"") + key + "\"";
    const size_t pos = text.find(k);
    if (pos == std::string::npos)
    {
      return def;
    }
    const size_t colon = text.find(':', pos);
    if (colon == std::string::npos)
    {
      return def;
    }
    const size_t t = text.find("true", colon);
    const size_t f = text.find("false", colon);
    if (t != std::string::npos && (f == std::string::npos || t < f))
    {
      return true;
    }
    if (f != std::string::npos && (t == std::string::npos || f < t))
    {
      return false;
    }
    return def;
  };
  cfg.AllowlistEnabled = find_bool("allowlist_enabled", true);
  {
    const std::string k = "\"min_gles\"";
    const size_t pos = text.find(k);
    if (pos != std::string::npos)
    {
      const size_t q1 = text.find('"', text.find(':', pos) + 1);
      const size_t q2 = text.find('"', q1 + 1);
      if (q1 != std::string::npos && q2 != std::string::npos)
      {
        cfg.MinGles = text.substr(q1 + 1, q2 - q1 - 1);
      }
    }
  }
  {
    const std::string k = "\"allow_renderers\"";
    const size_t pos = text.find(k);
    if (pos != std::string::npos)
    {
      const size_t lb = text.find('[', pos);
      const size_t rb = text.find(']', lb);
      if (lb != std::string::npos && rb != std::string::npos)
      {
        cfg.AllowRenderers.clear();
        size_t i = lb + 1;
        while (i < rb)
        {
          const size_t q1 = text.find('"', i);
          if (q1 == std::string::npos || q1 >= rb)
          {
            break;
          }
          const size_t q2 = text.find('"', q1 + 1);
          if (q2 == std::string::npos || q2 >= rb)
          {
            break;
          }
          cfg.AllowRenderers.push_back(text.substr(q1 + 1, q2 - q1 - 1));
          i = q2 + 1;
        }
      }
    }
  }
  return cfg;
}

bool MatchAndroidGpuAllowlist(const RenderBackendCaps &caps,
                              const AndroidGpuAllowlistConfig &cfg)
{
  if (!cfg.AllowlistEnabled)
  {
    return true;
  }
  int need_maj = 3;
  int need_min = 1;
  ParseMinGles(cfg.MinGles, need_maj, need_min);
  if (!caps.GlVersion.empty() &&
      !VersionAtLeast(caps.GlVersion, need_maj, need_min))
  {
    return false;
  }
  // Synthetic caps (unit tests) may omit GlVersion but set probe flags.
  if (caps.GlVersion.empty() && !(caps.HasCompute && caps.HasSsbo))
  {
    return false;
  }
  if (cfg.AllowRenderers.empty())
  {
    return true;
  }
  const std::string renderer_l = ToLower(caps.GlRenderer);
  for (const std::string &pat : cfg.AllowRenderers)
  {
    if (renderer_l.find(ToLower(pat)) != std::string::npos)
    {
      return true;
    }
  }
  return false;
}

void ApplyAndroidGpuEnvOverrides(RenderBackendCaps &caps,
                                 bool &android_gpu_enabled)
{
  const char *env = std::getenv("CUBATARIUM_ANDROID_GPU");
  if (!env || !*env)
  {
    return;
  }
  if (env[0] == '0')
  {
    android_gpu_enabled = false;
    caps.ForceCpuBackends = true;
  }
  else if (env[0] == '1')
  {
    android_gpu_enabled = true;
  }
}

void ApplyAndroidGpuPolicy(RenderBackendCaps &caps, bool android_gpu_enabled,
                           const AndroidGpuAllowlistConfig *allowlist)
{
  if (caps.Platform != RenderPlatformKind::Android)
  {
    caps.AllowAndroidGpu = false;
    caps.AndroidGpuDenyReason = "n/a";
    return;
  }

  ApplyAndroidGpuEnvOverrides(caps, android_gpu_enabled);

  if (caps.ForceCpuBackends)
  {
    caps.AllowAndroidGpu = false;
    caps.AndroidGpuDenyReason = "force_cpu";
    LOG(INFO) << "[AndroidGpu] user=" << android_gpu_enabled
              << " effective=0 reason=force_cpu";
    return;
  }
  if (!android_gpu_enabled)
  {
    caps.AllowAndroidGpu = false;
    caps.AndroidGpuDenyReason = "user_off";
    LOG(INFO) << "[AndroidGpu] user=0 effective=0 reason=user_off";
    return;
  }
  if (!caps.ProbeCompleted || !caps.HasCompute || !caps.HasSsbo)
  {
    caps.AllowAndroidGpu = false;
    caps.AndroidGpuDenyReason = "probe_fail";
    LOG(INFO) << "[AndroidGpu] user=1 effective=0 reason=probe_fail"
              << " compute=" << caps.HasCompute << " ssbo=" << caps.HasSsbo
              << " probe=" << caps.ProbeCompleted;
    return;
  }

  AndroidGpuAllowlistConfig local;
  const AndroidGpuAllowlistConfig &cfg =
      allowlist ? *allowlist : (local = LoadAndroidGpuAllowlist(nullptr));
  // Dev override CUBATARIUM_ANDROID_GPU=1 skips allowlist.
  const char *env = std::getenv("CUBATARIUM_ANDROID_GPU");
  const bool skip_allowlist = env && env[0] == '1';
  if (!skip_allowlist && !MatchAndroidGpuAllowlist(caps, cfg))
  {
    caps.AllowAndroidGpu = false;
    caps.AndroidGpuDenyReason = "allowlist";
    LOG(INFO) << "[AndroidGpu] user=1 effective=0 reason=allowlist renderer=\""
              << caps.GlRenderer << "\"";
    return;
  }

  caps.AllowAndroidGpu = true;
  caps.AndroidGpuDenyReason = "ok";
  LOG(INFO) << "[AndroidGpu] user=1 probe=1 allowlist=1 effective=1 renderer=\""
            << caps.GlRenderer << "\"";
}

} // namespace cutum

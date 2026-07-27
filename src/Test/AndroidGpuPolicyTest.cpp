#include "Render/Backend/AndroidGpuPolicy.h"
#include "Render/Backend/RenderBackendCaps.h"

#include <cstdlib>
#include <iostream>

namespace
{

int gFails = 0;

void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << msg << "\n";
    ++gFails;
  }
}

} // namespace

int main()
{
  using namespace cutum;

  {
    RenderBackendCaps caps = DetectRenderBackendCaps();
    caps.Platform = RenderPlatformKind::Desktop;
    caps.ProbeCompleted = true;
    caps.HasCompute = true;
    caps.HasSsbo = true;
    ApplyAndroidGpuPolicy(caps, true);
    Expect(!caps.AllowAndroidGpu, "desktop never sets AllowAndroidGpu");
    Expect(caps.AndroidGpuDenyReason == "n/a", "desktop deny=n/a");
  }

  {
    RenderBackendCaps caps;
    caps.Platform = RenderPlatformKind::Android;
    caps.ProbeCompleted = true;
    caps.HasCompute = true;
    caps.HasSsbo = true;
    caps.GlVersion = "OpenGL ES 3.2";
    caps.GlRenderer = "Adreno (TM) 650";
    AndroidGpuAllowlistConfig cfg;
    cfg.AllowlistEnabled = true;
    cfg.MinGles = "3.1";
    cfg.AllowRenderers = {"Adreno (TM)"};
    ApplyAndroidGpuPolicy(caps, true, &cfg);
    Expect(caps.AllowAndroidGpu, "android default pref + capable allowlisted");
    Expect(caps.AndroidGpuDenyReason == "ok", "deny=ok");
  }

  {
    RenderBackendCaps caps;
    caps.Platform = RenderPlatformKind::Android;
    caps.ProbeCompleted = true;
    caps.HasCompute = true;
    caps.HasSsbo = true;
    caps.GlVersion = "OpenGL ES 3.2";
    caps.GlRenderer = "Adreno (TM) 650";
    AndroidGpuAllowlistConfig cfg;
    cfg.AllowlistEnabled = true;
    cfg.AllowRenderers = {"Adreno (TM)"};
    ApplyAndroidGpuPolicy(caps, false, &cfg);
    Expect(!caps.AllowAndroidGpu, "user opt-out keeps CPU");
    Expect(caps.AndroidGpuDenyReason == "user_off", "deny=user_off");
  }

  {
    RenderBackendCaps caps;
    caps.Platform = RenderPlatformKind::Android;
    caps.ProbeCompleted = false;
    caps.HasCompute = false;
    caps.HasSsbo = false;
    ApplyAndroidGpuPolicy(caps, true);
    Expect(!caps.AllowAndroidGpu, "probe fail denies GPU");
    Expect(caps.AndroidGpuDenyReason == "probe_fail", "deny=probe_fail");
  }

  {
    RenderBackendCaps caps;
    caps.Platform = RenderPlatformKind::Android;
    caps.ProbeCompleted = true;
    caps.HasCompute = true;
    caps.HasSsbo = true;
    caps.GlVersion = "OpenGL ES 3.2";
    caps.GlRenderer = "Unknown GPU XYZ";
    AndroidGpuAllowlistConfig cfg;
    cfg.AllowlistEnabled = true;
    cfg.AllowRenderers = {"Adreno (TM)", "Mali-G"};
    ApplyAndroidGpuPolicy(caps, true, &cfg);
    Expect(!caps.AllowAndroidGpu, "denylist renderer");
    Expect(caps.AndroidGpuDenyReason == "allowlist", "deny=allowlist");
  }

  {
    Expect(MatchAndroidGpuAllowlist(
               [] {
                 RenderBackendCaps c;
                 c.GlVersion = "OpenGL ES 3.1";
                 c.GlRenderer = "Mali-G78";
                 c.HasCompute = true;
                 c.HasSsbo = true;
                 return c;
               }(),
               [] {
                 AndroidGpuAllowlistConfig cfg;
                 cfg.AllowRenderers = {"Mali-G"};
                 return cfg;
               }()),
           "mali allowlist match");
  }

  InvalidateRenderBackendCapsCache();
  Expect(!GetActiveRenderBackendCaps().ProbeCompleted ||
             GetActiveRenderBackendCaps().Platform ==
                 DetectRenderBackendCaps().Platform,
         "cache invalidate resets");

  if (gFails != 0)
  {
    std::cerr << "android_gpu_policy_test: " << gFails << " failures\n";
    return 1;
  }
  std::cout << "android_gpu_policy_test: ok\n";
  return 0;
}

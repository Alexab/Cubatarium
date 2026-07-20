#include "World/Core/RuntimeTuning.h"

#include "World/Physics/FluidTuning.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace cutum
{

URuntimeTuning &URuntimeTuning::Get()
{
  static URuntimeTuning instance;
  return instance;
}

void URuntimeTuning::ResetToDefaults()
{
  Get() = URuntimeTuning{};
}

void URuntimeTuning::LoadStreamingTuneFile(const char *path)
{
  if (!path || !*path)
  {
    return;
  }
  static std::string last_path;
  static std::filesystem::file_time_type last_mtime{};
  static bool have_mtime = false;
  std::error_code ec;
  const auto mtime = std::filesystem::last_write_time(path, ec);
  if (ec)
  {
    return;
  }
  if (have_mtime && path == last_path && mtime == last_mtime)
  {
    return;
  }
  std::ifstream in(path);
  if (!in)
  {
    return;
  }
  nlohmann::json j;
  try
  {
    in >> j;
  }
  catch (...)
  {
    return;
  }
  URuntimeTuning &t = Get();
  if (j.contains("mesh_forward_bias_k"))
  {
    t.MeshForwardBiasK = j.value("mesh_forward_bias_k", t.MeshForwardBiasK);
  }
  if (j.contains("relight_inflight_mult_high"))
  {
    t.RelightInflightMultHigh =
        j.value("relight_inflight_mult_high", t.RelightInflightMultHigh);
  }
  if (j.contains("relight_inflight_mult_holes"))
  {
    t.RelightInflightMultHoles =
        j.value("relight_inflight_mult_holes", t.RelightInflightMultHoles);
  }
  if (j.contains("mesh_fly_cap_yellow"))
  {
    t.MeshFlyCapYellow = j.value("mesh_fly_cap_yellow", t.MeshFlyCapYellow);
  }
  if (j.contains("mesh_fly_cap_red"))
  {
    t.MeshFlyCapRed = j.value("mesh_fly_cap_red", t.MeshFlyCapRed);
  }
  if (j.contains("recover_n_boost"))
  {
    t.RecoverNBoost = j.value("recover_n_boost", t.RecoverNBoost);
  }
  last_path = path;
  last_mtime = mtime;
  have_mtime = true;
}

} // namespace cutum

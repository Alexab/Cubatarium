#include "App/Settings/AudioSettings.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{

float ReadVolume(const nlohmann::json &audio, const char *key, float fallback)
{
  return std::clamp(audio.value(key, fallback), 0.f, 1.f);
}

void WriteVolume(nlohmann::json &audio, const char *key, float value)
{
  audio[key] = std::clamp(value, 0.f, 1.f);
}

} // namespace

void ReadAudioSettings(const nlohmann::json &audio, AudioSettings &out)
{
  out.Enabled = audio.value("enabled", out.Enabled);
  out.Master = ReadVolume(audio, "master", out.Master);
  out.Music = ReadVolume(audio, "music", out.Music);
  out.Ambient = ReadVolume(audio, "ambient", out.Ambient);
  out.Sfx = ReadVolume(audio, "sfx", out.Sfx);
  out.Ui = ReadVolume(audio, "ui", out.Ui);
}

void WriteAudioSettings(nlohmann::json &audio, const AudioSettings &settings)
{
  audio["enabled"] = settings.Enabled;
  WriteVolume(audio, "master", settings.Master);
  WriteVolume(audio, "music", settings.Music);
  WriteVolume(audio, "ambient", settings.Ambient);
  WriteVolume(audio, "sfx", settings.Sfx);
  WriteVolume(audio, "ui", settings.Ui);
}

AudioSettings ClampAudioSettings(const AudioSettings &settings)
{
  AudioSettings clamped = settings;
  clamped.Master = std::clamp(clamped.Master, 0.f, 1.f);
  clamped.Music = std::clamp(clamped.Music, 0.f, 1.f);
  clamped.Ambient = std::clamp(clamped.Ambient, 0.f, 1.f);
  clamped.Sfx = std::clamp(clamped.Sfx, 0.f, 1.f);
  clamped.Ui = std::clamp(clamped.Ui, 0.f, 1.f);
  return clamped;
}

} // namespace cutum

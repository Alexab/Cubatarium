#ifndef AUDIO_SETTINGS_H
#define AUDIO_SETTINGS_H

#include <nlohmann/json_fwd.hpp>

namespace cutum
{

/// User-facing audio settings. Volume fields are linear 0..1 gains per bus.
struct AudioSettings
{
  bool Enabled{true};
  float Master{0.8f};
  float Music{0.8f};
  float Ambient{0.8f};
  float Sfx{1.f};
  float Ui{0.8f};
};

/// Reads the "audio" config section (missing keys keep current values).
void ReadAudioSettings(const nlohmann::json &audio, AudioSettings &out);

/// Writes the "audio" config section.
void WriteAudioSettings(nlohmann::json &audio, const AudioSettings &settings);

/// Clamps volumes to the 0..1 range.
AudioSettings ClampAudioSettings(const AudioSettings &settings);

} // namespace cutum

#endif

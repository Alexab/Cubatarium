#include "App/Settings/AudioSettings.h"

#include <iostream>
#include <nlohmann/json.hpp>

namespace
{

constexpr const char *kTestName = "audio_settings_json_test";

void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << kTestName << " FAIL: " << msg << "\n";
    std::exit(1);
  }
}

void ExpectNear(float actual, float expected, const char *msg)
{
  if (actual < expected - 1e-4f || actual > expected + 1e-4f)
  {
    std::cerr << kTestName << " FAIL: " << msg << " (got " << actual
              << ", want " << expected << ")\n";
    std::exit(1);
  }
}

} // namespace

int main()
{
  cutum::AudioSettings settings;
  settings.Enabled = false;
  settings.Master = 0.5f;
  settings.Music = 0.25f;
  settings.Ambient = 0.75f;
  settings.Sfx = 1.f;
  settings.Ui = 0.f;

  nlohmann::json json;
  cutum::WriteAudioSettings(json, settings);
  Expect(json["enabled"] == false, "enabled round-trip");
  ExpectNear(json["master"].get<float>(), 0.5f, "master round-trip");

  // Round-trip through a parse of the dumped section (matches config.json).
  const nlohmann::json parsed = nlohmann::json::parse(json.dump());
  cutum::AudioSettings restored;
  cutum::ReadAudioSettings(parsed, restored);
  Expect(restored.Enabled == false, "read enabled");
  ExpectNear(restored.Master, 0.5f, "read master");
  ExpectNear(restored.Music, 0.25f, "read music");
  ExpectNear(restored.Ambient, 0.75f, "read ambient");
  ExpectNear(restored.Sfx, 1.f, "read sfx");
  ExpectNear(restored.Ui, 0.f, "read ui");

  // Missing keys keep defaults; out-of-range values clamp.
  cutum::AudioSettings defaults;
  nlohmann::json partial = nlohmann::json::parse(R"({"master": 1.7})");
  cutum::ReadAudioSettings(partial, defaults);
  Expect(defaults.Enabled == true, "missing enabled keeps default");
  ExpectNear(defaults.Master, 1.f, "clamped to 1");
  ExpectNear(defaults.Music, 0.8f, "missing music keeps default");

  std::cout << kTestName << " OK\n";
  return 0;
}

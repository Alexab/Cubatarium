#ifndef ENVIRONMENTCONFIG_H
#define ENVIRONMENTCONFIG_H

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace cutum
{

constexpr int kMaxEnvironmentCelestialBodies = 4;

struct EnvironmentCelestialBodySpec
{
  std::string Id{"sun"};
  std::string Type{"sun"};
  glm::vec3 Color{1.0f, 0.95f, 0.82f};
  float Intensity{1.0f};
  float AngularSizeDeg{0.53f};
  float OrbitInclinationDeg{23.0f};
  float OrbitPeriodDays{1.0f};
  float OrbitPhase{0.0f};
  float OrbitLongitudeDeg{0.0f};
};

struct WeatherAutoSettings
{
  bool AutoChange{true};
  float DryFraction{0.7f};
  float PrecipFraction{0.3f};
  float DryClearFraction{0.55f};
  float MinPrecipMinutes{2.0f};
  float MinDryMinutes{1.0f};
  float TransitionSeconds{45.0f};
  float EpisodeMinMinutes{3.0f};
  float EpisodeMaxMinutes{12.0f};
  float PrecipHeavyFraction{0.35f};
};

enum class WeatherAutoEpisodeMode
{
  None = 0,
  Dry = 1,
  Precip = 2,
};

struct WeatherAutoRuntime
{
  bool Enabled{true};
  bool ManualOverride{false};
  float EpisodeRemainingSec{0.0f};
  WeatherAutoEpisodeMode EpisodeMode{WeatherAutoEpisodeMode::None};
};

struct EnvironmentConfig
{
  float TimeOfDay{0.35f};
  float DayLengthMinutes{10.0f};
  float MinAmbient{0.12f};
  float CelestialHorizonFade{0.15f};
  std::vector<EnvironmentCelestialBodySpec> CelestialBodies;
  WeatherAutoSettings WeatherAuto;
  WeatherAutoRuntime WeatherRuntime;

  void Validate();
  static EnvironmentConfig FromJson(const nlohmann::json &root);
  nlohmann::json ToJson() const;
  void WriteWeatherAutoToJson(nlohmann::json &weather_json) const;
  void ReadWeatherAutoFromJson(const nlohmann::json &weather_json);
};

EnvironmentCelestialBodySpec
ParseCelestialBodySpec(const nlohmann::json &entry);
bool TryParseCelestialBodiesArray(const nlohmann::json &array,
                                  std::vector<EnvironmentCelestialBodySpec> &out);

} // namespace cutum

#endif // ENVIRONMENTCONFIG_H

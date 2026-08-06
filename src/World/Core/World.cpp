// #include <QPainter>
// #include <QJsonDocument>
// #include <QJsonObject>
// #include <QJsonValue>
// #include <QJsonArray>
// #include <QFile>
#include "World/Core/World.h"
#include <climits>
#include "Activity/WorldCreatureActivitySink.h"
#include "App/Settings/RenderSettings.h"
#include "Items/ToolCapabilities.h"
#include "Items/ItemDefinitionStorage.h"
#include "Game/ModePolicy.h"
#include "Game/Economy/ResourceEconomy.h"
#include "Creatures/Core/Creature.h"
#include "Core/Progress/IUProgressSink.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Player/Player.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "Creatures/Player/User.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/CreatureVisualFactory.h"
#include "Render/Camera/Camera.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "ResourcePacks/BlockNameUtil.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Core/FluidColumnSurfaceQuery.h"
#include "World/Core/RuntimeTuning.h"
#include "World/Core/WorldFluidFacade.h"
#include "World/Core/WorldViewBinding.h"
#include "World/Diagnostics/MovementDiagnosticsRecorder.h"
#include "World/Environment/WeatherAutoController.h"
#include "World/Environment/WeatherBiomeUtil.h"
#include "World/IO/ChunkStorageService.h"
#include "App/Settings/GraphicsQualityProfile.h"
#include "World/Lighting/AsyncRelightBuilder.h"
#include "World/Lighting/ChunkRelightSnapshot.h"
#include "World/Lighting/GpuSkylightColumnSeed.h"
#include "World/Lighting/ChunkLighting.h"
#include "World/Lighting/IULightingPipeline.h"
#include "World/Lighting/LightingPipelineFactory.h"
#include "Render/Backend/RenderBackendFactory.h"
#include "Render/Backend/RenderBackendCaps.h"
#include "World/Math/FluidCellState.h"
#include "World/Math/GridMath.h"
#include "World/Streaming/ColumnFlowExecutor.h"
#include "World/Mesh/WorldMeshDirtyPolicy.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Objects/ObjectLibrary.h"
#include "World/Objects/ObjectUtil.h"
#include "World/Persistence/WorldPersistence.h"
#include <chrono>
#include <filesystem>
#include "World/Physics/FluidReflowScan.h"
#include "World/Physics/FluidSpreadSystem.h"
#include "World/Physics/PhysicsProfileFactory.h"
#include "World/Physics/WorldBlockPhysicsService.h"
#include "World/Physics/WorldChunkDirtyService.h"
#include "World/Physics/WorldMovementPhysicsService.h"
#include "World/Physics/WorldPhysicsScheduler.h"
#include "World/Interaction/BlockBreakService.h"
#include "World/Interaction/BlockPlacementService.h"
#include "World/Raycast/BlockRaycast.h"
#include "World/Streaming/ChunkEmergeCoordinator.h"
#include "World/Streaming/ColumnRenderablePolicy.h"
#include "World/Streaming/WorldStreaming.h"
#include "WorldGen/Core/IUWorldGenPipeline.h"
#include "WorldGen/Core/ProceduralConfigIO.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Core/WorldGenSets.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

using json = nlohmann::json;

namespace cutum
{

namespace
{

constexpr float kMaxReasonablePlayerY = 512.0f;
constexpr float kMinReasonablePlayerY = -32.0f;
constexpr float kSecondsPerMinute = 60.0f;
constexpr float kMinDayLengthMinutes = 1.0f;
constexpr float kStarOrbitPeriodDays = 0.997f;
constexpr float kStarOrbitPhase = 0.25f;

float Clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }

float Smoothstep(float edge0, float edge1, float x)
{
  if (edge1 <= edge0)
  {
    return x >= edge1 ? 1.0f : 0.0f;
  }
  const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

float Wrap01(float value)
{
  if (!std::isfinite(value))
  {
    return 0.0f;
  }
  const float wrapped = std::fmod(value, 1.0f);
  return wrapped < 0.0f ? wrapped + 1.0f : wrapped;
}

std::string NormalizeToken(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
                 { return static_cast<char>(std::tolower(ch)); });
  return value;
}

UWorld::CelestialBodyType CelestialTypeFromId(const std::string &id)
{
  const std::string lower = NormalizeToken(id);
  if (lower.find("moon") != std::string::npos)
  {
    return UWorld::CelestialBodyType::Moon;
  }
  return UWorld::CelestialBodyType::Sun;
}

glm::mat3 BuildCelestialFrameRotation(float period_days, float phase,
                                      float inclination_deg, float longitude_deg,
                                      float time_norm)
{
  const float period = std::max(0.01f, period_days);
  const float theta = (time_norm / period + phase) * 6.28318530718f;
  const float c = std::cos(theta);
  const float s = std::sin(theta);
  // R_z(theta): XY phase (column-major)
  const glm::mat3 R_phase(glm::vec3(c, s, 0.0f), glm::vec3(-s, c, 0.0f),
                          glm::vec3(0.0f, 0.0f, 1.0f));
  const float inc = glm::radians(inclination_deg);
  const float cos_i = std::cos(inc);
  const float sin_i = std::sin(inc);
  // R_x(inclination)
  const glm::mat3 R_inc(glm::vec3(1.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, cos_i, sin_i),
                        glm::vec3(0.0f, -sin_i, cos_i));
  const float lon = glm::radians(longitude_deg);
  const float cos_l = std::cos(lon);
  const float sin_l = std::sin(lon);
  // R_y(longitude)
  const glm::mat3 R_lon(glm::vec3(cos_l, 0.0f, -sin_l),
                        glm::vec3(0.0f, 1.0f, 0.0f),
                        glm::vec3(sin_l, 0.0f, cos_l));
  return R_lon * R_inc * R_phase;
}

glm::vec3 ComputeCelestialDirection(const UWorld::UCelestialBodyVisual &body,
                                    float time_norm)
{
  const glm::mat3 R = BuildCelestialFrameRotation(
      body.OrbitPeriodDays, body.OrbitPhase, body.OrbitInclinationDeg,
      body.OrbitLongitudeDeg, time_norm);
  glm::vec3 out = R * glm::vec3(1.0f, 0.0f, 0.0f);
  if (!std::isfinite(out.x) || !std::isfinite(out.y) || !std::isfinite(out.z))
  {
    out = glm::vec3(0.0f, 1.0f, 0.0f);
  }
  const float len = glm::length(out);
  if (len <= 1e-5f)
  {
    return glm::vec3(0.0f, 1.0f, 0.0f);
  }
  return out / len;
}

} // namespace

UWorld::UWorld(std::shared_ptr<UTextureCubeStorage> texture_cube,
               std::shared_ptr<UViewEngine> views)
    : TextureCubeInstance(texture_cube),
      ViewBinding(std::make_unique<UWorldViewBinding>(std::move(views))),
      MeshService(std::make_unique<UWorldMeshService>()),
      Streaming(std::make_unique<UWorldStreaming>()),
      Persistence(std::make_unique<UWorldPersistence>()), Environment(*this),
      Collision(BlockWorld, &Environment),
      LightingPipeline(ULightingPipelineFactory::Create(LightingMode::Full))
{
  if (TextureCubeInstance)
  {
    BlockRegistry =
        std::make_unique<UBlockRegistry>(TextureCubeInstance, BlockDefinitions);
    Collision.SetBlockRegistry(BlockRegistry.get());
  }
  IsIntersectionExists = false;
  HasIntersectionBlock = false;
  Environment.Initialize();
  ConfigurePhysicsServices();
}

UWorld::~UWorld() = default;

std::string UWorld::WeatherTypeToString(WeatherType value)
{
  switch (value)
  {
  case WeatherType::Cloudy:
    return "cloudy";
  case WeatherType::Rain:
    return "rain";
  case WeatherType::Storm:
    return "storm";
  case WeatherType::Snow:
    return "snow";
  case WeatherType::Clear:
  default:
    return "clear";
  }
}

bool UWorld::WeatherTypeFromString(const std::string &value, WeatherType &out)
{
  const std::string normalized = NormalizeToken(value);
  if (normalized == "clear")
  {
    out = WeatherType::Clear;
    return true;
  }
  if (normalized == "cloudy")
  {
    out = WeatherType::Cloudy;
    return true;
  }
  if (normalized == "rain")
  {
    out = WeatherType::Rain;
    return true;
  }
  if (normalized == "storm")
  {
    out = WeatherType::Storm;
    return true;
  }
  if (normalized == "snow")
  {
    out = WeatherType::Snow;
    return true;
  }
  return false;
}

void UWorld::SetTimeOfDayNormalized(float value)
{
  EnvironmentStateData.TimeOfDayNormalized = Wrap01(value);
  UpdateCelestialLightingFactors();
}

void UWorld::UpdateCelestialLightingFactors()
{
  EnsureDefaultCelestialBodies();
  float sun_day = 0.0f;
  float moon_night = 0.0f;
  for (UCelestialBodyVisual &body : EnvironmentStateData.CelestialBodies)
  {
    if (body.Id.empty())
    {
      body.Id = body.Type == CelestialBodyType::Moon ? "moon_auto" : "sun_auto";
    }
    body.Type = CelestialTypeFromId(body.Id);
    body.DirectionWorld = ComputeCelestialDirection(
        body, EnvironmentStateData.TimeOfDayNormalized);
    const float elev = body.DirectionWorld.y;
    const float above_horizon =
        Smoothstep(0.0f, GetCelestialHorizonFade(), elev);
    const float lit = above_horizon * std::max(body.Intensity, 0.0f);
    if (body.Type == CelestialBodyType::Sun)
    {
      sun_day = std::max(sun_day, lit);
    }
    else
    {
      moon_night = std::max(moon_night, lit);
    }
  }
  EnvironmentStateData.DayNightFactor = Clamp01(sun_day);
  EnvironmentStateData.MoonNightFactor = Clamp01(moon_night);
  const float night = 1.0f - EnvironmentStateData.DayNightFactor;
  const float auto_star_visibility =
      Clamp01(night * (1.0f - EnvironmentStateData.CloudCoverage * 0.85f));
  EnvironmentStateData.StarVisibility =
      EnvironmentStateData.StarVisibilityOverride >= 0.0f
          ? Clamp01(EnvironmentStateData.StarVisibilityOverride)
          : auto_star_visibility;

  const UCelestialBodyVisual *primary_sun = nullptr;
  for (const UCelestialBodyVisual &body : EnvironmentStateData.CelestialBodies)
  {
    if (body.Type == CelestialBodyType::Sun)
    {
      primary_sun = &body;
      break;
    }
  }
  if (primary_sun != nullptr)
  {
    EnvironmentStateData.StarOrbitInclinationDeg =
        primary_sun->OrbitInclinationDeg;
    EnvironmentStateData.StarOrbitLongitudeDeg =
        primary_sun->OrbitLongitudeDeg;
  }
  EnvironmentStateData.StarOrbitPeriodDays = kStarOrbitPeriodDays;
  EnvironmentStateData.StarOrbitPhase = kStarOrbitPhase;
  const glm::mat3 star_frame = BuildCelestialFrameRotation(
      EnvironmentStateData.StarOrbitPeriodDays,
      EnvironmentStateData.StarOrbitPhase,
      EnvironmentStateData.StarOrbitInclinationDeg,
      EnvironmentStateData.StarOrbitLongitudeDeg,
      EnvironmentStateData.TimeOfDayNormalized);
  EnvironmentStateData.StarCelestialInv = glm::transpose(star_frame);
}

void UWorld::AddTimeOfDayNormalized(float delta)
{
  SetTimeOfDayNormalized(EnvironmentStateData.TimeOfDayNormalized + delta);
}

void UWorld::SetDayLengthMinutes(float minutes)
{
  EnvironmentStateData.DayLengthMinutes =
      std::max(kMinDayLengthMinutes, minutes);
}

void UWorld::SetWeather(WeatherType weather, float transitionSeconds)
{
  SetWeatherInternal(weather, transitionSeconds, true);
}

void UWorld::SetWeatherInternal(WeatherType weather, float transitionSeconds,
                                bool manual_override)
{
  if (manual_override)
  {
    EnvironmentSettingsData.WeatherRuntime.ManualOverride = true;
  }
  if (weather == WeatherType::Clear)
  {
    EnvironmentStateData.CloudCoverageOverride = -1.0f;
  }
  EnvironmentStateData.TargetWeather = weather;
  EnvironmentStateData.WeatherTransitionDurationSec =
      std::max(0.0f, transitionSeconds);
  EnvironmentStateData.WeatherTransitionSec = 0.0f;
  if (EnvironmentStateData.Weather == weather ||
      EnvironmentStateData.WeatherTransitionDurationSec <= 0.01f)
  {
    EnvironmentStateData.Weather = weather;
    EnvironmentStateData.TargetWeather = weather;
    EnvironmentStateData.WeatherTransitionSec =
        EnvironmentStateData.WeatherTransitionDurationSec;
  }
}

void UWorld::SetWeatherByName(const std::string &name, float transitionSeconds)
{
  WeatherType weather = WeatherType::Clear;
  if (WeatherTypeFromString(name, weather))
  {
    SetWeather(weather, transitionSeconds);
  }
}

std::string UWorld::GetWeatherName() const
{
  return WeatherTypeToString(EnvironmentStateData.Weather);
}

void UWorld::ApplyCelestialBodiesFromConfig()
{
  if (EnvironmentSettingsData.CelestialBodies.empty())
  {
    EnsureDefaultCelestialBodies();
    SyncDefaultCelestialBodiesToConfig();
    return;
  }
  EnvironmentStateData.CelestialBodies.clear();
  for (const EnvironmentCelestialBodySpec &spec :
       EnvironmentSettingsData.CelestialBodies)
  {
    UCelestialBodyVisual body;
    body.Id = spec.Id;
    const std::string type = NormalizeToken(spec.Type);
    body.Type =
        type == "moon" ? CelestialBodyType::Moon : CelestialBodyType::Sun;
    body.Color = spec.Color;
    body.Intensity = spec.Intensity;
    body.AngularSizeDeg = spec.AngularSizeDeg;
    body.OrbitInclinationDeg = spec.OrbitInclinationDeg;
    body.OrbitPeriodDays = spec.OrbitPeriodDays;
    body.OrbitPhase = spec.OrbitPhase;
    body.OrbitLongitudeDeg = spec.OrbitLongitudeDeg;
    EnvironmentStateData.CelestialBodies.push_back(body);
  }
  RefreshSkyVisualStateForRender();
}

void UWorld::SyncDefaultCelestialBodiesToConfig()
{
  EnvironmentSettingsData.CelestialBodies.clear();
  for (const UCelestialBodyVisual &body : EnvironmentStateData.CelestialBodies)
  {
    EnvironmentCelestialBodySpec spec;
    spec.Id = body.Id;
    spec.Type = body.Type == CelestialBodyType::Moon ? "moon" : "sun";
    spec.Color = body.Color;
    spec.Intensity = body.Intensity;
    spec.AngularSizeDeg = body.AngularSizeDeg;
    spec.OrbitInclinationDeg = body.OrbitInclinationDeg;
    spec.OrbitPeriodDays = body.OrbitPeriodDays;
    spec.OrbitPhase = body.OrbitPhase;
    spec.OrbitLongitudeDeg = body.OrbitLongitudeDeg;
    EnvironmentSettingsData.CelestialBodies.push_back(spec);
  }
}

void UWorld::ApplyEnvironmentConfig(const EnvironmentConfig &config,
                                    bool reset_weather_runtime)
{
  EnvironmentSettingsData = config;
  EnvironmentSettingsData.Validate();
  SetTimeOfDayNormalized(EnvironmentSettingsData.TimeOfDay);
  SetDayLengthMinutes(EnvironmentSettingsData.DayLengthMinutes);
  SetLightingMinAmbient(EnvironmentSettingsData.MinAmbient);
  ApplyCelestialBodiesFromConfig();
  if (reset_weather_runtime)
  {
    WeatherAutoRuntime runtime;
    runtime.Enabled = EnvironmentSettingsData.WeatherAuto.AutoChange;
    EnvironmentSettingsData.WeatherRuntime = runtime;
  }
}

float UWorld::GetCelestialHorizonFade() const
{
  return EnvironmentSettingsData.CelestialHorizonFade;
}

void UWorld::SetWeatherAutoEnabled(bool enabled)
{
  EnvironmentSettingsData.WeatherRuntime.Enabled = enabled;
  EnvironmentSettingsData.WeatherAuto.AutoChange = enabled;
  if (enabled)
  {
    EnvironmentSettingsData.WeatherRuntime.ManualOverride = false;
    EnvironmentSettingsData.WeatherRuntime.EpisodeRemainingSec = 0.0f;
  }
}

bool UWorld::IsWeatherAutoEnabled() const
{
  return EnvironmentSettingsData.WeatherAuto.AutoChange &&
         EnvironmentSettingsData.WeatherRuntime.Enabled;
}

void UWorld::ClearWeatherManualOverride()
{
  EnvironmentSettingsData.WeatherRuntime.ManualOverride = false;
  EnvironmentSettingsData.WeatherRuntime.EpisodeRemainingSec = 0.0f;
}

std::string UWorld::GetWeatherAutoStatusText() const
{
  const WeatherAutoSettings &settings = EnvironmentSettingsData.WeatherAuto;
  const WeatherAutoRuntime &runtime = EnvironmentSettingsData.WeatherRuntime;
  std::string mode = "none";
  if (runtime.EpisodeMode == WeatherAutoEpisodeMode::Dry)
  {
    mode = "dry";
  }
  else if (runtime.EpisodeMode == WeatherAutoEpisodeMode::Precip)
  {
    mode = "precip";
  }
  return "auto=" + std::string(IsWeatherAutoEnabled() ? "on" : "off") +
         " manual=" + (runtime.ManualOverride ? "yes" : "no") +
         " dry/precip=" + std::to_string(settings.DryFraction).substr(0, 4) +
         "/" + std::to_string(settings.PrecipFraction).substr(0, 4) +
         " episode=" + mode + " remaining=" +
         std::to_string(runtime.EpisodeRemainingSec).substr(0, 5) + "s" +
         " weather=" + GetWeatherName();
}

void UWorld::TickWeatherAuto(float dtSeconds)
{
  UWeatherAutoController::Tick(*this, dtSeconds);
}

void UWorld::EnsureDefaultCelestialBodies()
{
  if (!EnvironmentStateData.CelestialBodies.empty())
  {
    return;
  }
  UCelestialBodyVisual sun_main;
  sun_main.Id = "sun_main";
  sun_main.Type = CelestialBodyType::Sun;
  sun_main.Color = glm::vec3(1.0f, 0.94f, 0.82f);
  sun_main.Intensity = 1.0f;
  sun_main.AngularSizeDeg = 5.0f;
  sun_main.OrbitInclinationDeg = 23.0f;
  sun_main.OrbitPeriodDays = 1.0f;
  sun_main.OrbitPhase = 0.0f;
  sun_main.OrbitLongitudeDeg = 0.0f;

  UCelestialBodyVisual sun_secondary = sun_main;
  sun_secondary.Id = "sun_secondary";
  sun_secondary.Color = glm::vec3(1.0f, 0.8f, 0.62f);
  sun_secondary.Intensity = 0.52f;
  sun_secondary.AngularSizeDeg = 3.8f;
  sun_secondary.OrbitInclinationDeg = 37.0f;
  sun_secondary.OrbitPeriodDays = 1.6f;
  sun_secondary.OrbitPhase = 0.22f;
  sun_secondary.OrbitLongitudeDeg = 48.0f;

  UCelestialBodyVisual moon_main;
  moon_main.Id = "moon_main";
  moon_main.Type = CelestialBodyType::Moon;
  moon_main.Color = glm::vec3(0.72f, 0.78f, 0.9f);
  moon_main.Intensity = 0.35f;
  moon_main.AngularSizeDeg = 4.2f;
  moon_main.OrbitInclinationDeg = 18.0f;
  moon_main.OrbitPeriodDays = 1.0f;
  moon_main.OrbitPhase = 0.5f;
  moon_main.OrbitLongitudeDeg = 8.0f;

  UCelestialBodyVisual moon_secondary = moon_main;
  moon_secondary.Id = "moon_secondary";
  moon_secondary.Color = glm::vec3(0.64f, 0.72f, 0.88f);
  moon_secondary.Intensity = 0.22f;
  moon_secondary.AngularSizeDeg = 3.2f;
  moon_secondary.OrbitInclinationDeg = 29.0f;
  moon_secondary.OrbitPeriodDays = 1.9f;
  moon_secondary.OrbitPhase = 0.08f;
  moon_secondary.OrbitLongitudeDeg = -34.0f;

  EnvironmentStateData.CelestialBodies = {sun_main, sun_secondary, moon_main,
                                          moon_secondary};
  RefreshSkyVisualStateForRender();
}

void UWorld::RefreshSkyVisualStateForRender()
{
  UpdateCelestialLightingFactors();
  if (EnvironmentStateData.CloudCoverageOverride >= 0.0f)
  {
    EnvironmentStateData.CloudCoverage =
        Clamp01(EnvironmentStateData.CloudCoverageOverride);
  }
}

void UWorld::SetStarVisibility(float value)
{
  EnvironmentStateData.StarVisibilityOverride = Clamp01(value);
  EnvironmentStateData.StarVisibility =
      EnvironmentStateData.StarVisibilityOverride;
}

void UWorld::ResetCelestialBodies()
{
  EnvironmentStateData.CelestialBodies.clear();
  EnsureDefaultCelestialBodies();
}

void UWorld::SetCloudCoverage(float value)
{
  EnvironmentStateData.CloudCoverageOverride = Clamp01(value);
  EnvironmentStateData.CloudCoverage =
      EnvironmentStateData.CloudCoverageOverride;
}

void UWorld::TickEnvironment(float dtSeconds)
{
  if (dtSeconds <= 0.0f || !std::isfinite(dtSeconds))
  {
    return;
  }

  if (!EnvironmentStateData.TimeFrozen)
  {
    const float cycle_seconds =
        std::max(kMinDayLengthMinutes, EnvironmentStateData.DayLengthMinutes) *
        kSecondsPerMinute;
    AddTimeOfDayNormalized(dtSeconds / std::max(1.0f, cycle_seconds));
  }

  const auto weather_to_cloudiness = [](WeatherType weather) -> float
  {
    switch (weather)
    {
    case WeatherType::Cloudy:
      return 0.55f;
    case WeatherType::Rain:
      return 0.75f;
    case WeatherType::Storm:
      return 0.95f;
    case WeatherType::Snow:
      return 0.8f;
    case WeatherType::Clear:
    default:
      return 0.1f;
    }
  };
  const auto weather_to_precip = [](WeatherType weather) -> float
  {
    switch (weather)
    {
    case WeatherType::Rain:
      return 0.6f;
    case WeatherType::Storm:
      return 1.0f;
    case WeatherType::Snow:
      return 0.55f;
    case WeatherType::Cloudy:
    case WeatherType::Clear:
    default:
      return 0.0f;
    }
  };
  const auto weather_to_fog = [](WeatherType weather) -> float
  {
    switch (weather)
    {
    case WeatherType::Cloudy:
      return 1.05f;
    case WeatherType::Rain:
      return 1.2f;
    case WeatherType::Storm:
      return 1.35f;
    case WeatherType::Snow:
      return 1.25f;
    case WeatherType::Clear:
    default:
      return 1.0f;
    }
  };
  const auto weather_to_wind = [](WeatherType weather) -> float
  {
    switch (weather)
    {
    case WeatherType::Storm:
      return 1.0f;
    case WeatherType::Rain:
      return 0.65f;
    case WeatherType::Cloudy:
      return 0.45f;
    case WeatherType::Snow:
      return 0.35f;
    case WeatherType::Clear:
    default:
      return 0.2f;
    }
  };

  const bool transitioning =
      EnvironmentStateData.Weather != EnvironmentStateData.TargetWeather;
  if (transitioning &&
      EnvironmentStateData.WeatherTransitionDurationSec > 0.01f)
  {
    EnvironmentStateData.WeatherTransitionSec += dtSeconds;
    const float alpha =
        Clamp01(EnvironmentStateData.WeatherTransitionSec /
                EnvironmentStateData.WeatherTransitionDurationSec);
    if (alpha >= 1.0f)
    {
      EnvironmentStateData.Weather = EnvironmentStateData.TargetWeather;
      EnvironmentStateData.WeatherTransitionSec =
          EnvironmentStateData.WeatherTransitionDurationSec;
    }
    const float from_cloud =
        weather_to_cloudiness(EnvironmentStateData.Weather);
    const float to_cloud =
        weather_to_cloudiness(EnvironmentStateData.TargetWeather);
    const float from_precip = weather_to_precip(EnvironmentStateData.Weather);
    const float to_precip =
        weather_to_precip(EnvironmentStateData.TargetWeather);
    const float from_fog = weather_to_fog(EnvironmentStateData.Weather);
    const float to_fog = weather_to_fog(EnvironmentStateData.TargetWeather);
    const float from_wind = weather_to_wind(EnvironmentStateData.Weather);
    const float to_wind = weather_to_wind(EnvironmentStateData.TargetWeather);

    EnvironmentStateData.Cloudiness =
        from_cloud + (to_cloud - from_cloud) * alpha;
    EnvironmentStateData.PrecipitationIntensity =
        from_precip + (to_precip - from_precip) * alpha;
    EnvironmentStateData.WeatherFogMultiplier =
        from_fog + (to_fog - from_fog) * alpha;
    EnvironmentStateData.WindStrength =
        from_wind + (to_wind - from_wind) * alpha;
  }
  else
  {
    EnvironmentStateData.Weather = EnvironmentStateData.TargetWeather;
    EnvironmentStateData.Cloudiness =
        weather_to_cloudiness(EnvironmentStateData.Weather);
    EnvironmentStateData.PrecipitationIntensity =
        weather_to_precip(EnvironmentStateData.Weather);
    EnvironmentStateData.WeatherFogMultiplier =
        weather_to_fog(EnvironmentStateData.Weather);
    EnvironmentStateData.WindStrength =
        weather_to_wind(EnvironmentStateData.Weather);
  }

  EnvironmentStateData.WeatherSkyAttenuation =
      std::clamp(1.0f - EnvironmentStateData.Cloudiness * 0.28f, 0.65f, 1.0f);

  const bool precip_active =
      EnvironmentStateData.PrecipitationIntensity > 0.05f &&
      (EnvironmentStateData.Weather == WeatherType::Rain ||
       EnvironmentStateData.TargetWeather == WeatherType::Rain ||
       EnvironmentStateData.Weather == WeatherType::Storm ||
       EnvironmentStateData.TargetWeather == WeatherType::Storm ||
       EnvironmentStateData.Weather == WeatherType::Snow ||
       EnvironmentStateData.TargetWeather == WeatherType::Snow);
  const float target_wetness =
      precip_active ? EnvironmentStateData.PrecipitationIntensity * 0.85f
                    : 0.0f;
  const float wet_lerp =
      std::clamp(dtSeconds * (precip_active ? 0.35f : 0.12f), 0.0f, 1.0f);
  EnvironmentStateData.SurfaceWetness +=
      (target_wetness - EnvironmentStateData.SurfaceWetness) * wet_lerp;
  const float auto_cloud_coverage = std::clamp(
      EnvironmentStateData.Cloudiness * 0.9f + target_wetness * 0.25f, 0.0f,
      1.0f);
  EnvironmentStateData.CloudCoverage =
      EnvironmentStateData.CloudCoverageOverride >= 0.0f
          ? Clamp01(EnvironmentStateData.CloudCoverageOverride)
          : auto_cloud_coverage;
  EnsureDefaultCelestialBodies();
  UpdateCelestialLightingFactors();
  TickWeatherAuto(dtSeconds);
}

void UWorld::RebuildAllLightingDirtyMeshes()
{
  if (BlockRegistry)
  {
    GetLightingPipeline().RelightAllLoadedChunks(BlockWorld, *BlockRegistry);
  }
  InvalidateBlockMesh();
}

IULightingPipeline &UWorld::GetLightingPipeline()
{
  if (!LightingPipeline)
  {
    LightingMode mode =
        GraphicsQualityProfile::FromPreset(Render.Preset).GetLightingMode();
    const RenderBackendCaps caps = GetActiveRenderBackendCaps();
    const RenderBackendSelection sel = URenderBackendFactory::Select(caps);
    if (sel.Mesher == MesherBackendKind::GpuGreedy ||
        sel.Mesher == MesherBackendKind::AndroidHybridGpu)
    {
      mode = LightingMode::Full;
    }
    LightingPipeline = ULightingPipelineFactory::Create(mode);
  }
  return *LightingPipeline;
}

const IULightingPipeline &UWorld::GetLightingPipeline() const
{
  return const_cast<UWorld *>(this)->GetLightingPipeline();
}

bool UWorld::RequiresLightingLitGate() const
{
  return GetLightingPipeline().RequiresLitGate();
}

bool UWorld::AllowsAsyncLighting() const
{
  return GetLightingPipeline().AllowsAsyncRelight();
}

void UWorld::RelightTerrainColumn(int world_x, int world_z, int min_y,
                                  int max_y, bool priority_mesh,
                                  bool include_skylight, bool include_block_light)
{
  if (LightingRelightDeferred || !BlockRegistry)
  {
    return;
  }
  const auto t0 = std::chrono::high_resolution_clock::now();
  std::vector<glm::ivec3> relit_chunks;
  GetLightingPipeline().RelightColumnWithFrontier(
      BlockWorld, *BlockRegistry, world_x, world_z, min_y, max_y,
      include_block_light, include_skylight, &relit_chunks);
  const glm::ivec3 primary_chunk =
      UChunkManager::WorldToChunk(glm::ivec3(world_x, 0, world_z));
  MarkRelitChunksForMesh(relit_chunks, priority_mesh,
                         {glm::ivec2(primary_chunk.x, primary_chunk.z)});
  const auto t1 = std::chrono::high_resolution_clock::now();
  PhysicsTelemetryData.FullRelightMs =
      std::chrono::duration<double, std::milli>(t1 - t0).count();
}

void UWorld::RelightPlayerEdit(const std::vector<glm::ivec3> &block_positions,
                               int min_world_y)
{
  if (LightingRelightDeferred || !BlockRegistry || block_positions.empty())
  {
    return;
  }
  IULightingPipeline &lighting = GetLightingPipeline();
  if (!lighting.AllowsAsyncRelight())
  {
    const RelightFrontierOutcome outcome = lighting.RelightBlocksAroundAllEx(
        BlockWorld, *BlockRegistry, block_positions, min_world_y,
        ProceduralTemplate.MaxHeight, true, kRelightFrontierIterationsEdit);
    std::vector<glm::ivec2> primary_grounds;
    primary_grounds.reserve(block_positions.size());
    for (const glm::ivec3 &pos : block_positions)
    {
      const glm::ivec3 chunk = UChunkManager::WorldToChunk(pos);
      primary_grounds.push_back(glm::ivec2(chunk.x, chunk.z));
    }
    MarkRelitChunksForMesh(outcome.relit_chunks, true, primary_grounds);
    return;
  }
  if (ProceduralTemplate.AsyncRelight)
  {
    EnqueueAsyncPlayerRelight(block_positions, min_world_y);
    return;
  }
  const auto t0 = std::chrono::high_resolution_clock::now();
  const int max_y = ProceduralTemplate.MaxHeight;
  const RelightFrontierOutcome outcome = lighting.RelightBlocksAroundAllEx(
      BlockWorld, *BlockRegistry, block_positions, min_world_y, max_y, true,
      kRelightFrontierIterationsEdit);
  std::vector<glm::ivec2> primary_grounds;
  primary_grounds.reserve(block_positions.size());
  for (const glm::ivec3 &pos : block_positions)
  {
    const glm::ivec3 chunk = UChunkManager::WorldToChunk(pos);
    primary_grounds.push_back(glm::ivec2(chunk.x, chunk.z));
  }
  MarkRelitChunksForMesh(outcome.relit_chunks, true, primary_grounds);
  PlayerRelightMeshBurstFrames = 3;
  if (outcome.frontier_unfinished && Persistence)
  {
    for (const glm::ivec3 &pos : block_positions)
    {
      Persistence->EnqueueTerrainColumnRelight(pos.x, pos.z);
    }
  }
  const auto t1 = std::chrono::high_resolution_clock::now();
  PhysicsTelemetryData.FullRelightMs =
      std::chrono::duration<double, std::milli>(t1 - t0).count();
}

void UWorld::MarkRelitChunksForMesh(const std::vector<glm::ivec3> &relit_chunks,
                                    bool priority_mesh,
                                    const std::vector<glm::ivec2> &primary_grounds,
                                    bool finalize_pending_gate)
{
  if (relit_chunks.empty())
  {
    // Job finished with nothing to apply (empty capture / unloaded). Still
    // clear the mesh gate — leaving Pending forever made Promote spin and
    // pending_light_focus climb to 60–80 with relight_drain_ms≈0.
    for (const glm::ivec2 &g : primary_grounds)
    {
      AsyncRelightColumnsInFlight.erase(g);
      if (!finalize_pending_gate)
      {
        continue;
      }
      PendingLightBeforeMesh.erase(g);
      SetColumnEmergeState(glm::ivec3(g.x, 0, g.y), ColumnEmergeState::LitReady);
      // Empty apply used to leave Lighting columns with no Dirty forever if
      // commit skipped preview (trail wedge). Admit first mesh now.
      if (MeshService)
      {
        const glm::ivec3 ground(g.x, 0, g.y);
        const int sea = ProceduralTemplate.SeaLevel;
        const int max_y = ProceduralTemplate.MaxHeight;
        const int dirty_min = std::max(0, sea - CHUNK_SIZE);
        const int dirty_max = std::min(max_y, sea + CHUNK_SIZE * 2);
        MeshService->MarkTerrainChunkMeshDirtySeamedPriority(
            ground, dirty_min, dirty_max,
            /*include_horizontal_neighbors=*/false);
      }
    }
    return;
  }
  std::unordered_set<glm::ivec2, GroundColumnHash> primary_set;
  primary_set.reserve(primary_grounds.size() * 2 + 1);
  for (const glm::ivec2 &g : primary_grounds)
  {
    primary_set.insert(g);
  }
  // Per-column Y from chunks that were actually lit.
  struct YBand
  {
    int min_y{INT32_MAX};
    int max_y{INT32_MIN};
  };
  std::unordered_map<glm::ivec2, YBand, GroundColumnHash> bands;
  bands.reserve(relit_chunks.size());
  for (const glm::ivec3 &coord : relit_chunks)
  {
    YBand &band = bands[glm::ivec2(coord.x, coord.z)];
    const int chunk_base_y = coord.y * CHUNK_SIZE;
    band.min_y = std::min(band.min_y, chunk_base_y);
    band.max_y = std::max(band.max_y, chunk_base_y + CHUNK_SIZE - 1);
  }
  const int column_max_y = ProceduralTemplate.MaxHeight;
  const int sea = ProceduralTemplate.SeaLevel;
  for (auto &[key, band] : bands)
  {
    const glm::ivec3 ground(key.x, 0, key.y);
    const bool is_primary = primary_set.count(key) != 0;
    // Only the column whose light job finished may leave the mesh gate.
    // Neighbors remesh for seams only after they are already LitReady.
    if (is_primary)
    {
      // Dirty = lit cy ∪ narrow pending ∪ sea±CHUNK ∪ player band when near.
      // Ignore pending bands that span most of the world (legacy NotePending
      // used full height and flooded Dirty back to 1000+).
      int dirty_min = std::max(0, band.min_y - 1);
      int dirty_max = std::min(column_max_y, band.max_y + 1);
      if (const auto pit = PendingLightBeforeMesh.find(key);
          pit != PendingLightBeforeMesh.end())
      {
        const int span = pit->second.max_y - pit->second.min_y;
        if (span >= 0 && span <= CHUNK_SIZE * 4)
        {
          dirty_min = std::min(dirty_min, pit->second.min_y);
          dirty_max = std::max(dirty_max, pit->second.max_y);
        }
      }
      if (ProceduralTemplate.FillWater)
      {
        dirty_min =
            std::min(dirty_min, std::max(0, sea - CHUNK_SIZE));
        dirty_max = std::max(
            dirty_max, std::min(column_max_y, sea + CHUNK_SIZE * 2));
      }
      {
        const glm::ivec3 focus_block = GetPreferredLoadFocusBlock();
        const glm::ivec3 focus_chunk = UChunkManager::WorldToChunk(focus_block);
        const int horiz = std::max(std::abs(key.x - focus_chunk.x),
                                   std::abs(key.y - focus_chunk.z));
        if (horiz <= 1)
        {
          dirty_min =
              std::min(dirty_min, std::max(0, focus_block.y - CHUNK_SIZE));
          dirty_max = std::max(
              dirty_max,
              std::min(column_max_y, focus_block.y + CHUNK_SIZE * 2));
        }
      }
      // Partial relight results must not feed Dirty while the first-light gate
      // is still closed. Those remeshes are soft-deferred anyway and were a big
      // source of dirty backlog during flight.
      if (!finalize_pending_gate)
      {
        AsyncRelightColumnsInFlight.erase(key);
        continue;
      }
      bool had_mesh = false;
      const int cy0 = FloorDiv(dirty_min, CHUNK_SIZE);
      const int cy1 = FloorDiv(dirty_max, CHUNK_SIZE);
      for (int cy = cy0; cy <= cy1; ++cy)
      {
        if (MeshService->HasGreedyMesh(glm::ivec3(key.x, cy, key.y)))
        {
          had_mesh = true;
          break;
        }
      }
      if (finalize_pending_gate)
      {
        PendingLightBeforeMesh.erase(key);
      }
      if (finalize_pending_gate && had_mesh)
      {
        const glm::ivec3 focus_block = GetPreferredLoadFocusBlock();
        const glm::ivec3 focus_chunk = UChunkManager::WorldToChunk(focus_block);
        const int horiz = std::max(std::abs(key.x - focus_chunk.x),
                                   std::abs(key.y - focus_chunk.z));
        // Cruise: only track underfeet sticky. Idle: never expand sticky ring —
        // async-saturated insert of full focus made sticky 7→13 while Dirty
        // remesh thrash left not_ready climbing (manual 210341).
        const bool idle =
            LastMovementSpeed <= ProceduralTemplate.MovementPrefetchThreshold;
        const int pending_n =
            static_cast<int>(PendingLightBeforeMesh.size());
        const int sticky_r =
            idle ? 1
                 : (pending_n > 10 ? GetStreamingFocusRadius() : 1);
        if (horiz <= sticky_r)
        {
          StickyRemeshAfterLight.insert(key);
        }
      }
      AsyncRelightColumnsInFlight.erase(key);
      if (finalize_pending_gate)
      {
        SetColumnEmergeState(ground, ColumnEmergeState::LitReady);
      }
      if (dirty_max < dirty_min)
      {
        dirty_min = std::max(0, band.min_y - 1);
        dirty_max = std::min(column_max_y, band.max_y + 1);
      }
      const bool seam_ok =
          finalize_pending_gate && !SuppressRelightSeamDirty;
      // Standing Dirty churn suppresses global seam fanout, but empty-shell
      // border faces in the focus ring stick forever without a remesh. Keep
      // underfeet (±1) seam remesh even while SuppressRelightSeamDirty.
      bool focus_ring_seam = false;
      if (finalize_pending_gate && SuppressRelightSeamDirty)
      {
        const glm::ivec3 focus_block = GetPreferredLoadFocusBlock();
        const glm::ivec3 focus_chunk = UChunkManager::WorldToChunk(focus_block);
        const int focus_horiz =
            std::max(std::abs(key.x - focus_chunk.x),
                     std::abs(key.y - focus_chunk.z));
        focus_ring_seam = focus_horiz <= 1;
      }
      if (SuppressRelightSeamDirty && had_mesh && !focus_ring_seam)
      {
        // Idle remesh: do not MarkDirty Inflight (re-Dirty after Apply froze
        // focus_dirty). Request post-Apply remesh instead so Capture sees new
        // light without mid-flight thrash.
        for (int cy = cy0; cy <= cy1; ++cy)
        {
          const glm::ivec3 coord(key.x, cy, key.y);
          if (MeshService->IsChunkMeshDirty(coord))
          {
            continue;
          }
          if (MeshService->HasInflightMeshBuild(coord))
          {
            MeshService->RequestRemeshAfterApply(coord);
            continue;
          }
          if (priority_mesh)
          {
            MeshService->MarkDirtyPriority(coord);
          }
          else
          {
            MeshService->MarkDirty(coord);
          }
        }
      }
      else if (priority_mesh)
      {
        MeshService->MarkTerrainChunkMeshDirtySeamedPriority(
            ground, dirty_min, dirty_max, seam_ok || focus_ring_seam);
      }
      else
      {
        MeshService->MarkTerrainChunkMeshDirtySeamed(
            ground, dirty_min, dirty_max, seam_ok || focus_ring_seam);
      }
      if (finalize_pending_gate)
      {
        SetColumnEmergeState(ground, ColumnEmergeState::Meshing);
      }
      continue;
    }
    // Neighbor: skip Dirty while still awaiting own first light.
    // Idle lit-but-dirty catch-up: skip far neighbor Dirty — seam cascade
    // kept focus_dirty≈400 while async remeshed in place. Focus-ring (±1)
    // neighbors that actually received light still remesh (no further seam).
    if (SuppressRelightSeamDirty)
    {
      const glm::ivec3 focus_block = GetPreferredLoadFocusBlock();
      const glm::ivec3 focus_chunk = UChunkManager::WorldToChunk(focus_block);
      const int focus_horiz =
          std::max(std::abs(key.x - focus_chunk.x),
                   std::abs(key.y - focus_chunk.z));
      if (focus_horiz > 1)
      {
        continue;
      }
    }
    if (PendingLightBeforeMesh.count(key) != 0 ||
        !IsColumnLitReady(ground))
    {
      continue;
    }
    if (MeshService->GetDirtyCount() >= 350)
    {
      continue;
    }
    int dirty_min = std::max(0, band.min_y - 1);
    int dirty_max = std::min(column_max_y, band.max_y + 1);
    if (dirty_max < dirty_min)
    {
      continue;
    }
    // Under standing suppress: remesh this lit neighbor only (no seam fanout).
    const bool neighbor_seam = !SuppressRelightSeamDirty;
    if (priority_mesh)
    {
      MeshService->MarkTerrainChunkMeshDirtySeamedPriority(
          ground, dirty_min, dirty_max, neighbor_seam);
    }
    else
    {
      MeshService->MarkTerrainChunkMeshDirtySeamed(ground, dirty_min, dirty_max,
                                                   neighbor_seam);
    }
  }
  // Primary may be absent from relit_chunks (chunk unload / empty apply slice).
  // Still drop SoftDefer gate — otherwise PendingLight waits forever
  // (manual 220951: pf/fdm stuck at 1 while other columns drained).
  // TD-ARCH-015: warm Capture store after Dirty revisions so schedule skips
  // live GetChunk shell Capture in the hot tick.
  if (MeshService && !relit_chunks.empty())
  {
    for (const glm::ivec3 &coord : relit_chunks)
    {
      MeshService->PrefetchMeshCapture(GetBlockWorld(), coord);
    }
  }
  if (!finalize_pending_gate || !MeshService)
  {
    return;
  }
  for (const glm::ivec2 &g : primary_grounds)
  {
    if (bands.count(g) != 0)
    {
      continue;
    }
    AsyncRelightColumnsInFlight.erase(g);
    PendingLightBeforeMesh.erase(g);
    const glm::ivec3 ground(g.x, 0, g.y);
    SetColumnEmergeState(ground, ColumnEmergeState::LitReady);
    const int dirty_min = std::max(0, sea - CHUNK_SIZE);
    const int dirty_max = std::min(column_max_y, sea + CHUNK_SIZE * 2);
    if (priority_mesh)
    {
      MeshService->MarkTerrainChunkMeshDirtySeamedPriority(
          ground, dirty_min, dirty_max,
          /*include_horizontal_neighbors=*/false);
    }
    else
    {
      MeshService->MarkTerrainChunkMeshDirtySeamed(
          ground, dirty_min, dirty_max,
          /*include_horizontal_neighbors=*/false);
    }
    SetColumnEmergeState(ground, ColumnEmergeState::Meshing);
  }
}

void UWorld::AccumulateRelightMeshColumns(
    const std::vector<glm::ivec3> &relit_chunks)
{
  if (relit_chunks.empty())
  {
    return;
  }
  for (const glm::ivec3 &coord : relit_chunks)
  {
    const glm::ivec2 ground(coord.x, coord.z);
    const int chunk_base_y = coord.y * CHUNK_SIZE;
    const int chunk_top_y = chunk_base_y + CHUNK_SIZE - 1;
    auto [it, inserted] = PendingRelightMeshColumns.try_emplace(ground);
    if (inserted)
    {
      it->second.min_y = std::max(0, chunk_base_y - 1);
      it->second.max_y = chunk_top_y + 1;
    }
    else
    {
      it->second.min_y =
          std::min(it->second.min_y, std::max(0, chunk_base_y - 1));
      it->second.max_y = std::max(it->second.max_y, chunk_top_y + 1);
    }
  }
}

void UWorld::FlushPendingRelightMeshColumns(int max_columns_per_flush)
{
  if (PendingRelightMeshColumns.empty() || max_columns_per_flush <= 0)
  {
    return;
  }
  const glm::ivec3 focus =
      UChunkManager::WorldToChunk(GetPreferredLoadFocusBlock());
  std::vector<std::pair<int, glm::ivec2>> ordered;
  ordered.reserve(PendingRelightMeshColumns.size());
  for (const auto &entry : PendingRelightMeshColumns)
  {
    const int dist = std::max(std::abs(entry.first.x - focus.x),
                              std::abs(entry.first.y - focus.z));
    ordered.push_back({dist, entry.first});
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });
  int flushed = 0;
  for (const auto &item : ordered)
  {
    if (flushed >= max_columns_per_flush)
    {
      break;
    }
    const auto it = PendingRelightMeshColumns.find(item.second);
    if (it == PendingRelightMeshColumns.end())
    {
      continue;
    }
    const glm::ivec3 ground(it->first.x, 0, it->first.y);
    MeshService->MarkTerrainChunkMeshDirtySeamedPriority(
        ground, it->second.min_y, it->second.max_y, true);
    PendingRelightMeshColumns.erase(it);
    ++flushed;
  }
}

int UWorld::RecoverUnlitFocusMeshes(int max_columns,
                                    const glm::ivec2 *only_column)
{
  if (!MeshService || !Persistence || LightingRelightDeferred ||
      max_columns <= 0)
  {
    return 0;
  }
  const glm::ivec3 focus =
      UChunkManager::WorldToChunk(GetPreferredLoadFocusBlock());
  const int radius = GetStreamingFocusRadius();
  const int max_y = ProceduralTemplate.MaxHeight;
  const int sea = ProceduralTemplate.SeaLevel;
  // Playerв€Єsea band, plus deeper ocean floor (sea-4 chunks).
  int band_min = std::max(0, focus.y * CHUNK_SIZE - CHUNK_SIZE);
  int band_max = std::min(max_y, focus.y * CHUNK_SIZE + CHUNK_SIZE * 3 - 1);
  if (ProceduralTemplate.FillWater)
  {
    band_min = std::min(band_min, std::max(0, sea - CHUNK_SIZE * 4));
    band_max = std::max(band_max, std::min(max_y, sea + CHUNK_SIZE * 2));
  }
  const int cy0 = FloorDiv(band_min, CHUNK_SIZE);
  const int cy1 = FloorDiv(band_max, CHUNK_SIZE);
  int repaired = 0;
  for (int r = 0; r <= radius && repaired < max_columns; ++r)
  {
    for (int dz = -r; dz <= r && repaired < max_columns; ++dz)
    {
      for (int dx = -r; dx <= r && repaired < max_columns; ++dx)
      {
        if (r > 0 && std::max(std::abs(dx), std::abs(dz)) != r)
        {
          continue;
        }
        const glm::ivec2 key(focus.x + dx, focus.z + dz);
        if (only_column && key != *only_column)
        {
          continue;
        }
        const glm::ivec3 ground(key.x, 0, key.y);
        bool has_mesh = false;
        bool missing_mesh = false;
        bool any_sky = false;
        bool any_solid = false;
        for (int cy = cy0; cy <= cy1; ++cy)
        {
          const glm::ivec3 coord(ground.x, cy, ground.z);
          const UChunk *chunk = BlockWorld.GetChunkManager().GetChunk(coord);
          if (!chunk)
          {
            continue;
          }
          if (MeshService->HasGreedyMesh(coord) ||
              MeshService->IsPendingGpuApply(coord))
          {
            has_mesh = true;
          }
          else
          {
            // Only count missing when the slice has something to draw.
            bool slice_solid = false;
            for (int z = 0; z < CHUNK_SIZE && !slice_solid; z += 4)
            {
              for (int x = 0; x < CHUNK_SIZE && !slice_solid; x += 4)
              {
                for (int y = 0; y < CHUNK_SIZE && !slice_solid; y += 4)
                {
                  if (chunk->GetBlockLocal(glm::ivec3(x, y, z)) != BLOCK_AIR)
                  {
                    slice_solid = true;
                  }
                }
              }
            }
            if (slice_solid)
            {
              missing_mesh = true;
            }
          }
          for (int z = 0; z < CHUNK_SIZE && (!any_sky || !any_solid); z += 4)
          {
            for (int x = 0; x < CHUNK_SIZE && (!any_sky || !any_solid); x += 4)
            {
              for (int y = CHUNK_SIZE - 1; y >= 0 && (!any_sky || !any_solid);
                   y -= 4)
              {
                const glm::ivec3 local(x, y, z);
                if (chunk->GetBlockLocal(local) != BLOCK_AIR)
                {
                  any_solid = true;
                }
                if (chunk->GetSkyLightLocal(local) > 0)
                {
                  any_sky = true;
                }
              }
            }
          }
        }
        if (!any_solid)
        {
          continue;
        }
        const bool pending = IsPendingLightBeforeMesh(key);
        int remesh_min = band_min;
        int remesh_max = band_max;
        if (const auto pit = PendingLightBeforeMesh.find(key);
            pit != PendingLightBeforeMesh.end())
        {
          const int span = pit->second.max_y - pit->second.min_y;
          if (span >= 0 && span <= CHUNK_SIZE * 4)
          {
            remesh_min = std::min(remesh_min, pit->second.min_y);
            remesh_max = std::max(remesh_max, pit->second.max_y);
          }
        }
        const bool underfeet = r <= 1;
        // Pending + dark greedy preview: hole beats black squares (async race /
        // pre-pending bake). Drop slices; MarkRelit rebuilds when lit.
        if (pending && has_mesh)
        {
          bool dropped = false;
          for (int cy = cy0; cy <= cy1; ++cy)
          {
            const glm::ivec3 coord(ground.x, cy, ground.z);
            if (!MeshService->HasGreedyMesh(coord))
            {
              continue;
            }
            if (MeshService->GetCache().ChunkHasFullyDarkFace(coord))
            {
              MeshService->RemoveChunk(coord);
              dropped = true;
            }
          }
          if (dropped)
          {
            ++repaired;
            continue;
          }
        }
        // Focus ring: enqueue relight and unlock ring via LitReady. Keep
        // PendingLight until MarkRelit so soft-defer blocks light=0 remesh of
        // an already-built mesh (ClearPending here caused frequent dark chunks).
        if (pending)
        {
          // Already has sky in chunk data but gate never cleared (async MarkRelit
          // starved / discarded). Clear gate + remesh вЂ” idle west-strip plateau
          // at focus edge had pending=36 forever with relight_drainв‰€0.
          if (any_sky)
          {
            PendingLightBeforeMesh.erase(key);
            AsyncRelightColumnsInFlight.erase(key);
            SetColumnEmergeState(ground, ColumnEmergeState::LitReady);
            MeshService->MarkTerrainChunkMeshDirtySeamedPriority(
                ground, remesh_min, remesh_max,
                /*include_horizontal_neighbors=*/false);
            SetColumnEmergeState(ground, ColumnEmergeState::Meshing);
            ++repaired;
            continue;
          }
          int enqueue_min = remesh_min;
          int enqueue_max = remesh_max;
          if (const auto pit = PendingLightBeforeMesh.find(key);
              pit != PendingLightBeforeMesh.end())
          {
            enqueue_min = pit->second.min_y;
            enqueue_max = pit->second.max_y;
          }
          Persistence->EnqueueTerrainColumnRelight(
              key.x * CHUNK_SIZE, key.y * CHUNK_SIZE, /*priority=*/true,
              enqueue_min, enqueue_max);
          SetColumnEmergeState(ground, ColumnEmergeState::LitReady);
          (void)missing_mesh;
          // Do NOT MarkDirty pending columns here. Re-admitting missing slices
          // before light lands only floods Dirty with work that the soft-defer
          // gate will reject again.
          ++repaired;
          continue;
        }
        // Gate cleared but GreedyCache still incomplete.
        if (missing_mesh)
        {
          SetColumnEmergeState(ground, ColumnEmergeState::Meshing);
          MeshService->MarkTerrainChunkMeshDirtySeamedPriority(
              ground, remesh_min, remesh_max, /*include_horizontal_neighbors=*/
              !underfeet);
          ++repaired;
          continue;
        }
        // Stuck black mesh on lit-ready columns: remesh when baked side/top
        // faces are dark or world light outran the mesh (stale bake).
        bool bad_mesh = false;
        if (has_mesh)
        {
          for (int cy = cy0; cy <= cy1; ++cy)
          {
            const glm::ivec3 coord(ground.x, cy, ground.z);
            if (!MeshService->HasGreedyMesh(coord))
            {
              continue;
            }
            if (MeshService->GetCache().ChunkHasFullyDarkFace(coord) ||
                MeshService->GetCache().ChunkHasStaleDarkFaces(coord,
                                                              BlockWorld))
            {
              bad_mesh = true;
              break;
            }
          }
        }
        if (has_mesh && bad_mesh)
        {
          if (!any_sky)
          {
            Persistence->EnqueueTerrainColumnRelight(
                ground.x * CHUNK_SIZE, ground.z * CHUNK_SIZE, /*priority=*/true,
                remesh_min, remesh_max);
          }
          MeshService->MarkTerrainChunkMeshDirtySeamedPriority(
              ground, remesh_min, remesh_max,
              /*include_horizontal_neighbors=*/true);
          ++repaired;
        }
      }
    }
  }
  return repaired;
}

int UWorld::AdmitFocusMeshIngress(int max_columns)
{
  if (!MeshService || !Persistence || LightingRelightDeferred ||
      max_columns <= 0)
  {
    return 0;
  }
  const glm::ivec3 focus =
      UChunkManager::WorldToChunk(GetPreferredLoadFocusBlock());
  const int radius = GetStreamingFocusRadius();
  const int max_y = ProceduralTemplate.MaxHeight;
  const int sea = ProceduralTemplate.SeaLevel;
  int remesh_min = std::max(0, sea - CHUNK_SIZE);
  int remesh_max = std::min(max_y, sea + CHUNK_SIZE * 2);
  const int player_min = std::max(0, focus.y * CHUNK_SIZE - CHUNK_SIZE);
  const int player_max =
      std::min(max_y, focus.y * CHUNK_SIZE + CHUNK_SIZE * 3 - 1);
  remesh_min = std::min(remesh_min, player_min);
  remesh_max = std::max(remesh_max, player_max);
  const int cy0 = FloorDiv(remesh_min, CHUNK_SIZE);
  const int cy1 = FloorDiv(remesh_max, CHUNK_SIZE);
  int admitted = 0;
  // Cheap path: only columns already in PendingLightBeforeMesh near focus.
  for (const auto &entry : PendingLightBeforeMesh)
  {
    if (admitted >= max_columns)
    {
      break;
    }
    const glm::ivec2 key = entry.first;
    const int dist =
        std::max(std::abs(key.x - focus.x), std::abs(key.y - focus.z));
    if (dist > radius)
    {
      continue;
    }
    const glm::ivec3 ground(key.x, 0, key.y);
    bool missing = false;
    for (int cy = cy0; cy <= cy1 && !missing; ++cy)
    {
      const glm::ivec3 coord(ground.x, cy, ground.z);
      const UChunk *chunk = BlockWorld.GetChunkManager().GetChunk(coord);
      if (!chunk || MeshService->HasMeshSatisfyingColumnReady(coord) ||
          MeshService->IsPendingGpuApply(coord))
      {
        continue;
      }
      for (int z = 0; z < CHUNK_SIZE && !missing; z += 4)
      {
        for (int x = 0; x < CHUNK_SIZE && !missing; x += 4)
        {
          for (int y = 0; y < CHUNK_SIZE && !missing; y += 4)
          {
            if (chunk->GetBlockLocal(glm::ivec3(x, y, z)) != BLOCK_AIR)
            {
              missing = true;
            }
          }
        }
      }
    }
    if (!missing)
    {
      continue;
    }
    int enqueue_min = remesh_min;
    int enqueue_max = remesh_max;
    if (const auto pit = PendingLightBeforeMesh.find(glm::ivec2(ground.x, ground.z));
        pit != PendingLightBeforeMesh.end())
    {
      enqueue_min = pit->second.min_y;
      enqueue_max = pit->second.max_y;
    }
    Persistence->EnqueueTerrainColumnRelight(
        ground.x * CHUNK_SIZE, ground.z * CHUNK_SIZE, /*priority=*/true,
        enqueue_min, enqueue_max);
    if (IsColumnLitReady(ground))
    {
      MeshService->MarkTerrainChunkMeshDirtySeamedPriority(
          ground, remesh_min, remesh_max,
          /*include_horizontal_neighbors=*/false);
      SetColumnEmergeState(ground, ColumnEmergeState::Meshing);
    }
    ++admitted;
  }
  return admitted;
}

int UWorld::AdmitFocusVisibleMissing(int max_columns, glm::vec2 forward_xz,
                                      const glm::ivec2 *only_column,
                                      int only_cy)
{
  if (!MeshService || max_columns <= 0)
  {
    return 0;
  }
  const glm::ivec3 focus =
      UChunkManager::WorldToChunk(GetPreferredLoadFocusBlock());
  const int radius = GetStreamingFocusRadius();
  const int max_y = ProceduralTemplate.MaxHeight;
  const int sea = ProceduralTemplate.SeaLevel;
  int remesh_min = std::max(0, sea - CHUNK_SIZE);
  int remesh_max = std::min(max_y, sea + CHUNK_SIZE * 2);
  const int player_min = std::max(0, focus.y * CHUNK_SIZE - CHUNK_SIZE);
  const int player_max =
      std::min(max_y, focus.y * CHUNK_SIZE + CHUNK_SIZE * 3 - 1);
  remesh_min = std::min(remesh_min, player_min);
  remesh_max = std::max(remesh_max, player_max);
  const int cy0 = FloorDiv(remesh_min, CHUNK_SIZE);
  const int cy1 = FloorDiv(remesh_max, CHUNK_SIZE);
  const glm::vec2 fwd_norm =
      glm::length(forward_xz) > 0.01f ? glm::normalize(forward_xz) : glm::vec2(0.0f);

  struct Candidate
  {
    glm::ivec2 key;
    int horiz;
    float forward_score;
  };
  std::vector<Candidate> candidates;
  candidates.reserve(static_cast<size_t>((radius + 1) * (radius + 1)));

  for (int r = 0; r <= radius; ++r)
  {
    for (int dz = -r; dz <= r; ++dz)
    {
      for (int dx = -r; dx <= r; ++dx)
      {
        if (r > 0 && std::max(std::abs(dx), std::abs(dz)) != r)
        {
          continue;
        }
        const glm::ivec2 key(focus.x + dx, focus.z + dz);
        if (only_column && key != *only_column)
        {
          continue;
        }
        const int horiz = std::max(std::abs(dx), std::abs(dz));
        const glm::ivec3 ground(key.x, 0, key.y);
        bool missing = false;
        for (int cy = cy0; cy <= cy1 && !missing; ++cy)
        {
          const glm::ivec3 coord(ground.x, cy, ground.z);
          const UChunk *chunk = BlockWorld.GetChunkManager().GetChunk(coord);
          if (!chunk || MeshService->HasMeshSatisfyingColumnReady(coord) ||
              MeshService->IsPendingGpuApply(coord) ||
              MeshService->HasInflightMeshBuild(coord))
          {
            continue;
          }
          for (int z = 0; z < CHUNK_SIZE && !missing; z += 4)
          {
            for (int x = 0; x < CHUNK_SIZE && !missing; x += 4)
            {
              for (int y = 0; y < CHUNK_SIZE && !missing; y += 4)
              {
                if (chunk->GetBlockLocal(glm::ivec3(x, y, z)) != BLOCK_AIR)
                {
                  missing = true;
                }
              }
            }
          }
        }
        if (!missing)
        {
          continue;
        }
        float forward_score = 0.0f;
        if (fwd_norm.x != 0.0f || fwd_norm.y != 0.0f)
        {
          const glm::vec2 to_col(static_cast<float>(dx), static_cast<float>(dz));
          if (glm::length(to_col) > 0.01f)
          {
            forward_score = glm::dot(glm::normalize(to_col), fwd_norm);
          }
        }
        candidates.push_back({key, horiz, forward_score});
      }
    }
  }

  // P1: idle FOV fill — prefer look direction more strongly than ring alone
  // (k≥1.5 vs MeshForwardBiasK 0.75). Horiz still primary via score.
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate &a, const Candidate &b)
            {
              const float score_a = static_cast<float>(a.horiz) -
                                   1.5f * std::max(0.0f, a.forward_score);
              const float score_b = static_cast<float>(b.horiz) -
                                   1.5f * std::max(0.0f, b.forward_score);
              if (score_a != score_b)
              {
                return score_a < score_b;
              }
              if (a.forward_score != b.forward_score)
              {
                return a.forward_score > b.forward_score;
              }
              return a.horiz < b.horiz;
            });

  int admitted = 0;
  for (const Candidate &candidate : candidates)
  {
    if (admitted >= max_columns)
    {
      break;
    }
    const glm::ivec3 ground(candidate.key.x, 0, candidate.key.y);
    const bool pending = IsPendingLightBeforeMesh(candidate.key);
    // Accelerate Capture for PendingLight, but do NOT skip Dirty — that left
    // forever-holes (manual 170154 miss_end=1): SoftDefer already allows
    // UnlitFirstMesh while Admit refused MarkDirty (SoTA should_mesh).
    if (pending && Persistence)
    {
      int enqueue_min = remesh_min;
      int enqueue_max = remesh_max;
      if (const auto pit = PendingLightBeforeMesh.find(candidate.key);
          pit != PendingLightBeforeMesh.end())
      {
        enqueue_min = pit->second.min_y;
        enqueue_max = pit->second.max_y;
      }
      Persistence->EnqueueTerrainColumnRelight(
          ground.x * CHUNK_SIZE, ground.z * CHUNK_SIZE, /*priority=*/true,
          enqueue_min, enqueue_max);
    }
    if (only_cy >= 0)
    {
      const int y0 = only_cy * CHUNK_SIZE;
      const int y1 = y0 + CHUNK_SIZE - 1;
      MeshService->MarkMissingSlicesDirtyPriority(BlockWorld, ground, y0, y1);
    }
    else
    {
      MeshService->MarkMissingSlicesDirtyPriority(BlockWorld, ground, remesh_min,
                                                  remesh_max);
    }
    SetColumnEmergeState(ground, ColumnEmergeState::Meshing);
    ++admitted;
  }
  return admitted;
}

void UWorld::NotePendingLightBeforeMesh(glm::ivec3 ground, int min_y, int max_y)
{
  if (!RequiresLightingLitGate())
  {
    return;
  }
  if (ground.y != 0)
  {
    ground.y = 0;
  }
  const glm::ivec2 key(ground.x, ground.z);
  SetColumnEmergeState(ground, ColumnEmergeState::Lighting);
  auto [it, inserted] = PendingLightBeforeMesh.try_emplace(key);
  if (inserted)
  {
    it->second.min_y = std::max(0, min_y);
    it->second.max_y = max_y;
    return;
  }
  it->second.min_y = std::min(it->second.min_y, std::max(0, min_y));
  it->second.max_y = std::max(it->second.max_y, max_y);
}

void UWorld::ClearPendingLightBeforeMesh(glm::ivec2 ground_xz)
{
  PendingLightBeforeMesh.erase(ground_xz);
  StickyRemeshAfterLight.erase(ground_xz);
}

int UWorld::TrimPendingLightBeforeMesh(glm::ivec3 focus_ground_horiz,
                                       int soft_cap)
{
  if (soft_cap <= 0 ||
      static_cast<int>(PendingLightBeforeMesh.size()) <= soft_cap ||
      !MeshService)
  {
    return 0;
  }
  struct Cand
  {
    glm::ivec2 key{};
    int dist{0};
  };
  std::vector<Cand> droppable;
  droppable.reserve(PendingLightBeforeMesh.size());
  for (const auto &entry : PendingLightBeforeMesh)
  {
    const glm::ivec2 &key = entry.first;
    const int dist = std::max(std::abs(key.x - focus_ground_horiz.x),
                              std::abs(key.y - focus_ground_horiz.z));
    if (dist <= 1)
    {
      continue;
    }
    bool has_mesh = false;
    const int max_cy =
        std::max(0, (ProceduralTemplate.MaxHeight - 1) / CHUNK_SIZE);
    for (int cy = 0; cy <= max_cy; ++cy)
    {
      if (MeshService->HasGreedyMesh(glm::ivec3(key.x, cy, key.y)))
      {
        has_mesh = true;
        break;
      }
    }
    // Never erase cold holes (no mesh) — would leave permanent darkness.
    // Also require LitReady (or later): Pending on Lighting/VoxelsReady stays.
    if (!has_mesh ||
        !IsColumnLitReady(glm::ivec3(key.x, 0, key.y)))
    {
      continue;
    }
    droppable.push_back(Cand{key, dist});
  }
  std::sort(droppable.begin(), droppable.end(),
            [](const Cand &a, const Cand &b) { return a.dist > b.dist; });
  int dropped = 0;
  for (const Cand &c : droppable)
  {
    if (static_cast<int>(PendingLightBeforeMesh.size()) <= soft_cap)
    {
      break;
    }
    ClearPendingLightBeforeMesh(c.key);
    ++dropped;
  }
  return dropped;
}

int UWorld::TrimFarRelightFifoFarthest(glm::ivec3 focus_ground_horiz,
                                       int soft_cap)
{
  if (!Persistence)
  {
    return 0;
  }
  return Persistence->TrimFarRelightFifoFarthest(focus_ground_horiz, soft_cap);
}

bool UWorld::IsPendingLightBeforeMesh(glm::ivec2 ground_xz) const
{
  return PendingLightBeforeMesh.find(ground_xz) != PendingLightBeforeMesh.end();
}

void UWorld::SetColumnEmergeState(glm::ivec3 ground, ColumnEmergeState state)
{
  if (ground.y != 0)
  {
    ground.y = 0;
  }
  ColumnEmergeStates[glm::ivec2(ground.x, ground.z)] = state;
}

ColumnEmergeState UWorld::GetColumnEmergeState(glm::ivec3 ground) const
{
  if (ground.y != 0)
  {
    ground.y = 0;
  }
  const auto it = ColumnEmergeStates.find(glm::ivec2(ground.x, ground.z));
  if (it == ColumnEmergeStates.end())
  {
    return ColumnEmergeState::Empty;
  }
  return it->second;
}

void UWorld::ClearColumnEmergeState(glm::ivec2 ground_xz)
{
  ColumnEmergeStates.erase(ground_xz);
}

bool UWorld::IsColumnLitReady(glm::ivec3 ground) const
{
  if (ground.y != 0)
  {
    ground.y = 0;
  }
  // LitReady/Meshing unlocks the ring even while PendingLight remains — Recover
  // keeps the gate until MarkRelit so soft-defer can block light=0 remesh.
  const ColumnEmergeState state = GetColumnEmergeState(ground);
  switch (state)
  {
  case ColumnEmergeState::LitReady:
  case ColumnEmergeState::Meshing:
  case ColumnEmergeState::RenderReady:
  case ColumnEmergeState::Empty:
    return true;
  default:
    break;
  }
  if (IsPendingLightBeforeMesh(glm::ivec2(ground.x, ground.z)))
  {
    return false;
  }
  return false;
}

bool UWorld::IsColumnVisualReadyForRing(glm::ivec3 ground) const
{
  // V5 visual/keep SLA — NOT terrain RingPrerequisitesMet (voxels-only).
  // Keep ring: best-effort once first-light gate is passed.
  if (!IsColumnLitReady(ground))
  {
    return false;
  }
  // Strict ring: only while stopped (TimeSinceMotionSec>0) for first 8s.
  // Moving keeps TimeSinceMotionSec==0 — must NOT treat that as strict
  // (was forcing RenderReady scans on every ring-gate column → hang).
  const glm::ivec3 focus_ground = UChunkManager::WorldToChunk(
      GetPreferredLoadFocusBlock());
  const int cheb = std::max(std::abs(ground.x - focus_ground.x),
                            std::abs(ground.z - focus_ground.z));
  const bool in_strict_focus_ring = cheb <= GetStreamingFocusRadius();
  const double since_stop = GetTimeSinceMotionSec();
  const bool strict_window = since_stop > 0.0 && since_stop <= 8.0;
  if (in_strict_focus_ring && strict_window)
  {
    return IsColumnRenderReady(ground);
  }
  return true;
}

void UWorld::UpdateMotionState(float speed, float dt_sec)
{
  LastMovementSpeed = speed;
  const float stop_threshold =
      std::max(0.01f, ProceduralTemplate.MovementPrefetchThreshold * 0.5f);
  if (speed <= stop_threshold)
  {
    TimeSinceMotionSec =
        std::min(30.0, TimeSinceMotionSec + std::max(0.0f, dt_sec));
  }
  else
  {
    TimeSinceMotionSec = 0.0;
  }
}

bool UWorld::IsColumnRenderReady(glm::ivec3 ground) const
{
  if (ground.y != 0)
  {
    ground.y = 0;
  }
  return GetColumnRenderableState(glm::ivec2(ground.x, ground.z)).draw_ok;
}

bool UWorld::IsChunkSliceRenderReady(glm::ivec3 chunk_coord) const
{
  if (!MeshService)
  {
    return false;
  }
  if (MeshService->HasMeshSatisfyingColumnReady(chunk_coord) ||
      MeshService->IsPendingGpuApply(chunk_coord) ||
      MeshService->IsGpuExtractInFlight(chunk_coord))
  {
    return true;
  }
  const UChunk *chunk = BlockWorld.GetChunkManager().GetChunk(chunk_coord);
  if (!chunk)
  {
    return false;
  }
  for (const BlockId block : chunk->GetData())
  {
    if (block != BLOCK_AIR)
    {
      return false; // solid without mesh — not ready
    }
  }
  return true; // air-only slice: nothing to draw
}

ColumnRenderableState UWorld::GetColumnRenderableState(glm::ivec2 ground_xz) const
{
  ColumnRenderableState out;
  if (!MeshService)
  {
    out.reason = ColumnRenderableState::BlockReason::NotLoaded;
    return out;
  }
  const glm::ivec3 ground(ground_xz.x, 0, ground_xz.y);
  out.stage = GetColumnEmergeState(ground);
  // Live ColumnFlow ticket OR sticky set (NoteColumnRepairNeeded).
  out.has_repair_ticket =
      GetColumnFlowExecutor().HasRepairTicket(ground_xz) ||
      IsColumnStickyRemesh(ground_xz);

  const int max_cy =
      std::max(0, FloorDiv(std::max(0, ProceduralTemplate.MaxHeight), CHUNK_SIZE));
  const glm::ivec3 focus_block = GetPreferredLoadFocusBlock();
  const glm::ivec3 focus_chunk = UChunkManager::WorldToChunk(focus_block);
  const int horiz_from_focus =
      std::max(std::abs(ground.x - focus_chunk.x),
               std::abs(ground.z - focus_chunk.z));
  int band_min = std::max(0, focus_block.y - CHUNK_SIZE);
  int band_max =
      std::min(ProceduralTemplate.MaxHeight, focus_block.y + CHUNK_SIZE * 2);
  if (ProceduralTemplate.FillWater)
  {
    const int sea = ProceduralTemplate.SeaLevel;
    band_min = std::min(band_min, std::max(0, sea - CHUNK_SIZE * 4));
    band_max =
        std::max(band_max, std::min(ProceduralTemplate.MaxHeight,
                                    sea + CHUNK_SIZE * 2));
  }
  const int cy0 = std::max(0, FloorDiv(band_min, CHUNK_SIZE));
  const int cy1 = std::min(max_cy, FloorDiv(band_max, CHUNK_SIZE));

  if (IsPendingLightBeforeMesh(ground_xz))
  {
    for (int cy = cy0; cy <= cy1; ++cy)
    {
      const glm::ivec3 coord(ground.x, cy, ground.z);
      // SoT draw_ok: drawable mesh OR queued GPU apply. Empty SoftDefer
      // placeholders (HasGreedy, !Drawable) must not look ready (manual 101824).
      if (MeshService->HasMeshSatisfyingColumnReady(coord))
      {
        out.draw_ok = true;
        out.reason = ColumnRenderableState::BlockReason::None;
        return out;
      }
      if (MeshService->IsPendingGpuApply(coord) ||
          MeshService->IsGpuExtractInFlight(coord))
      {
        out.draw_ok = true;
        out.reason = ColumnRenderableState::BlockReason::GpuInFlight;
        return out;
      }
    }
    out.reason = ColumnRenderableState::BlockReason::PendingLight;
    return out;
  }

  if (horiz_from_focus > 1)
  {
    bool has_mesh_or_gpu = false;
    bool stale_dark_with_mesh = false;
    for (int cy = cy0; cy <= cy1; ++cy)
    {
      const glm::ivec3 coord(ground.x, cy, ground.z);
      if (MeshService->HasMeshSatisfyingColumnReady(coord) ||
          MeshService->IsPendingGpuApply(coord) ||
          MeshService->IsGpuExtractInFlight(coord))
      {
        has_mesh_or_gpu = true;
      }
      if (MeshService->HasDrawableGreedyMesh(coord) &&
          MeshService->ChunkHasStaleDarkFaces(coord, BlockWorld))
      {
        stale_dark_with_mesh = true;
      }
    }
    const bool sticky = IsColumnStickyRemesh(ground_xz);
    const ColumnSoTDecision sot = ClassifyStickyStaleDarkSoT(
        has_mesh_or_gpu, sticky, stale_dark_with_mesh, horiz_from_focus);
    if (sot.kind == ColumnSoTKind::StickyRemesh)
    {
      out.reason = ColumnRenderableState::BlockReason::StickyRemesh;
      out.has_repair_ticket =
          GetColumnFlowExecutor().HasRepairTicket(ground_xz) ||
          IsColumnStickyRemesh(ground_xz) || sot.has_repair_ticket;
      out.draw_ok = sot.draw_ok;
      return out;
    }
    if (sot.kind == ColumnSoTKind::StaleDark)
    {
      out.reason = ColumnRenderableState::BlockReason::StaleDark;
      out.has_repair_ticket =
          GetColumnFlowExecutor().HasRepairTicket(ground_xz) ||
          sot.has_repair_ticket;
      out.draw_ok = sot.draw_ok;
      return out;
    }
  }

  // SoT: draw-when-drawable even if ColumnEmerge FSM is mid-transition
  // (Generating/etc). Empty SoftDefer (HasGreedy, !Drawable) is NOT ready —
  // HasGreedy-only left rim undrawn while fog/unfinished looked healed
  // (manual 101824 / 222446 trade: prefer visible fill over false draw_ok).
  bool has_mesh_or_gpu = false;
  bool saw_gpu_inflight_early = false;
  for (int cy = cy0; cy <= cy1; ++cy)
  {
    const glm::ivec3 coord(ground.x, cy, ground.z);
    if (MeshService->HasMeshSatisfyingColumnReady(coord))
    {
      has_mesh_or_gpu = true;
    }
    if (MeshService->IsPendingGpuApply(coord) ||
        MeshService->IsGpuExtractInFlight(coord))
    {
      has_mesh_or_gpu = true;
      saw_gpu_inflight_early = true;
    }
  }
  if (out.stage != ColumnEmergeState::RenderReady &&
      out.stage != ColumnEmergeState::LitReady &&
      out.stage != ColumnEmergeState::Meshing &&
      out.stage != ColumnEmergeState::Empty)
  {
    if (has_mesh_or_gpu)
    {
      out.draw_ok = true;
      out.reason = saw_gpu_inflight_early
                       ? ColumnRenderableState::BlockReason::GpuInFlight
                       : ColumnRenderableState::BlockReason::None;
      return out;
    }
    out.reason = ColumnRenderableState::BlockReason::NotReadyState;
    return out;
  }
  bool saw_loaded_meshable = false;
  bool saw_gpu_inflight = false;
  bool saw_missing_solid = false;
  for (int cy = cy0; cy <= cy1; ++cy)
  {
    const glm::ivec3 coord(ground.x, cy, ground.z);
    const UChunk *chunk = BlockWorld.GetChunkManager().GetChunk(coord);
    if (!chunk)
    {
      continue;
    }
    if (MeshService->HasMeshSatisfyingColumnReady(coord))
    {
      saw_loaded_meshable = true;
      continue;
    }
    if (MeshService->IsPendingGpuApply(coord) ||
        MeshService->IsGpuExtractInFlight(coord))
    {
      saw_loaded_meshable = true;
      saw_gpu_inflight = true;
      continue;
    }
    bool any_solid = false;
    for (const BlockId block : chunk->GetData())
    {
      if (block != BLOCK_AIR)
      {
        any_solid = true;
        break;
      }
    }
    if (!any_solid)
    {
      continue;
    }
    // P2: missing solid cy does not block drawing ready sibling slices
    // (JE-like section progressive reveal). HasMissing* telemetry unchanged.
    saw_missing_solid = true;
  }
  if (saw_loaded_meshable || out.stage == ColumnEmergeState::Empty)
  {
    out.draw_ok = true;
    out.reason = saw_gpu_inflight
                     ? ColumnRenderableState::BlockReason::GpuInFlight
                     : (saw_missing_solid
                            ? ColumnRenderableState::BlockReason::MissingMesh
                            : ColumnRenderableState::BlockReason::None);
    // draw_ok true even with MissingMesh reason — partial column visible.
    return out;
  }
  if (saw_missing_solid)
  {
    out.reason = ColumnRenderableState::BlockReason::MissingMesh;
    return out;
  }
  out.reason = ColumnRenderableState::BlockReason::NotLoaded;
  return out;
}

int UWorld::CountUnfinishedVisualNear(glm::ivec3 focus_ground_chunk,
                                      int radius_chunks) const
{
  if (radius_chunks < 0)
  {
    return 0;
  }
  int unfinished = 0;
  for (int dz = -radius_chunks; dz <= radius_chunks; ++dz)
  {
    for (int dx = -radius_chunks; dx <= radius_chunks; ++dx)
    {
      const glm::ivec3 ground(focus_ground_chunk.x + dx, 0,
                              focus_ground_chunk.z + dz);
      if (!IsTerrainChunkComplete(BlockWorld, ground, ProceduralTemplate.MaxHeight))
      {
        continue;
      }
      if (!IsColumnRenderReady(ground))
      {
        ++unfinished;
      }
    }
  }
  return unfinished;
}

void UWorld::CountUnfinishedVisualByFacing(glm::ivec3 focus_ground_chunk,
                                           int radius_chunks,
                                           glm::vec2 forward_xz, int &out_ahead,
                                           int &out_behind) const
{
  out_ahead = 0;
  out_behind = 0;
  if (radius_chunks < 0)
  {
    return;
  }
  const float flen =
      std::sqrt(forward_xz.x * forward_xz.x + forward_xz.y * forward_xz.y);
  const bool have_fwd = flen >= 0.01f;
  const float fx = have_fwd ? forward_xz.x / flen : 0.0f;
  const float fz = have_fwd ? forward_xz.y / flen : 0.0f;
  for (int dz = -radius_chunks; dz <= radius_chunks; ++dz)
  {
    for (int dx = -radius_chunks; dx <= radius_chunks; ++dx)
    {
      const glm::ivec3 ground(focus_ground_chunk.x + dx, 0,
                              focus_ground_chunk.z + dz);
      if (!IsTerrainChunkComplete(BlockWorld, ground,
                                  ProceduralTemplate.MaxHeight))
      {
        continue;
      }
      if (IsColumnRenderReady(ground))
      {
        continue;
      }
      if (!have_fwd || (dx == 0 && dz == 0))
      {
        ++out_ahead;
        continue;
      }
      const float tlen =
          std::sqrt(static_cast<float>(dx * dx + dz * dz));
      const float dot =
          (static_cast<float>(dx) / tlen) * fx +
          (static_cast<float>(dz) / tlen) * fz;
      if (dot >= 0.0f)
      {
        ++out_ahead;
      }
      else
      {
        ++out_behind;
      }
    }
  }
}

bool UWorld::MayMeshColumn(glm::ivec3 ground, bool underfeet_preview) const
{
  if (ground.y != 0)
  {
    ground.y = 0;
  }
  const ColumnEmergeState state = GetColumnEmergeState(ground);
  switch (state)
  {
  case ColumnEmergeState::LitReady:
  case ColumnEmergeState::Meshing:
  case ColumnEmergeState::RenderReady:
    return true;
  default:
    break;
  }
  // Preview while Lighting: underfeet OR any caller that already scoped focus
  // (soft-defer allows focus first-mesh separately).
  if (underfeet_preview &&
      IsPendingLightBeforeMesh(glm::ivec2(ground.x, ground.z)))
  {
    return true;
  }
  if (IsPendingLightBeforeMesh(glm::ivec2(ground.x, ground.z)))
  {
    return false;
  }
  return state == ColumnEmergeState::Empty ||
         state == ColumnEmergeState::VoxelsReady ||
         state == ColumnEmergeState::Lighting;
}

bool UWorld::HasPendingLightBeforeMeshNear(glm::ivec3 focus_ground_horiz,
                                           int radius_chunks) const
{
  return CountPendingLightBeforeMeshNear(focus_ground_horiz, radius_chunks) > 0;
}

int UWorld::DrainFocusVisualWork(glm::ivec3 focus_ground_horiz, int radius_chunks,
                                 int clear_pending_budget)
{
  int drained = 0;
  drained += PromotePendingLightRelightsNear(focus_ground_horiz, radius_chunks);
  drained += ClearPendingLightAfterMeshCommitted(clear_pending_budget);
  return drained;
}

void UWorld::DrainRelightQueuesBudget(int max_player_jobs, int max_bg_columns)
{
  if (!Persistence || (max_player_jobs <= 0 && max_bg_columns <= 0))
  {
    return;
  }
  const auto t0 = std::chrono::high_resolution_clock::now();
  Persistence->DrainRelightQueues(*this, max_player_jobs, max_bg_columns);
  PhysicsTelemetryData.RelightDrainMs +=
      std::chrono::duration<double, std::milli>(
          std::chrono::high_resolution_clock::now() - t0)
          .count();
}

bool UWorld::CanSeedSkylightAtCommit(glm::ivec3 ground) const
{
  if (ground.y != 0)
  {
    ground.y = 0;
  }
  if (!IsTerrainChunkComplete(BlockWorld, ground, ProceduralTemplate.MaxHeight))
  {
    return false;
  }
  int ring_complete = 0;
  int cardinals_complete = 0;
  for (int dz = -1; dz <= 1; ++dz)
  {
    for (int dx = -1; dx <= 1; ++dx)
    {
      const glm::ivec3 neighbor(ground.x + dx, 0, ground.z + dz);
      if (!IsTerrainChunkComplete(BlockWorld, neighbor,
                                  ProceduralTemplate.MaxHeight))
      {
        continue;
      }
      ++ring_complete;
      if (dx == 0 || dz == 0)
      {
        ++cardinals_complete;
      }
    }
  }
  // Full 3x3 neighborhood: safest fast path.
  if (ring_complete >= 9)
  {
    return true;
  }
  // Frontier ingress: center + most cardinals loaded (missing unload wedge OK).
  return cardinals_complete >= 3;
}

int UWorld::DrainIdleFocusPendingLight(glm::ivec3 focus_ground_horiz,
                                       int radius_chunks, int max_columns)
{
  if (LightingRelightDeferred || !MeshService || !BlockRegistry ||
      max_columns <= 0 || radius_chunks < 0 ||
      PendingLightBeforeMesh.empty())
  {
    return 0;
  }
  if (LastMovementSpeed > ProceduralTemplate.MovementPrefetchThreshold)
  {
    return 0;
  }
  const int max_y = ProceduralTemplate.MaxHeight;
  const int sea = ProceduralTemplate.SeaLevel;
  int band_min =
      std::max(0, focus_ground_horiz.y * CHUNK_SIZE - CHUNK_SIZE);
  int band_max = std::min(max_y, focus_ground_horiz.y * CHUNK_SIZE +
                                      CHUNK_SIZE * 3 - 1);
  if (ProceduralTemplate.FillWater)
  {
    band_min = std::min(band_min, std::max(0, sea - CHUNK_SIZE * 4));
    band_max = std::max(band_max, std::min(max_y, sea + CHUNK_SIZE * 2));
  }
  const int cy0 = FloorDiv(band_min, CHUNK_SIZE);
  const int cy1 = FloorDiv(band_max, CHUNK_SIZE);

  struct Candidate
  {
    int dist;
    bool has_mesh;
    glm::ivec2 key;
    int min_y;
    int max_y;
  };
  std::vector<Candidate> candidates;
  candidates.reserve(PendingLightBeforeMesh.size());
  for (const auto &entry : PendingLightBeforeMesh)
  {
    const glm::ivec2 key = entry.first;
    const int dist = std::max(std::abs(key.x - focus_ground_horiz.x),
                              std::abs(key.y - focus_ground_horiz.z));
    if (dist > radius_chunks)
    {
      continue;
    }
    bool has_mesh = false;
    for (int cy = cy0; cy <= cy1; ++cy)
    {
      if (MeshService->HasGreedyMesh(glm::ivec3(key.x, cy, key.y)))
      {
        has_mesh = true;
        break;
      }
    }
    // V2a: first light has no mesh yet — still must requeue (old filter
    // skipped the whole idle debt → pending plateau / relight_drain≈0).
    int remesh_min = band_min;
    int remesh_max = band_max;
    const int span = entry.second.max_y - entry.second.min_y;
    if (span >= 0 && span <= CHUNK_SIZE * 4)
    {
      remesh_min = std::min(remesh_min, entry.second.min_y);
      remesh_max = std::max(remesh_max, entry.second.max_y);
    }
    candidates.push_back({dist, has_mesh, key, remesh_min, remesh_max});
  }
  if (candidates.empty())
  {
    return 0;
  }
  // First-light (no mesh): inner/focus first so underfeet+view clear.
  // Lit remesh (has mesh): outer ring first — ingress wedge used to starve.
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate &a, const Candidate &b)
            {
              if (a.has_mesh != b.has_mesh)
              {
                return !a.has_mesh && b.has_mesh;
              }
              if (!a.has_mesh)
              {
                return a.dist < b.dist;
              }
              return a.dist > b.dist;
            });
  int drained = 0;
  for (const Candidate &c : candidates)
  {
    if (drained >= max_columns)
    {
      break;
    }
    // Only clear ghost InFlight marks. Erasing a live mark let Drain start a
    // duplicate while the first job still ran — and skipped_inflight thrash
    // left pending frozen with relight_drain≈0 (autofly stop plateau).
    if (AsyncRelightColumnsInFlight.count(c.key) != 0)
    {
      if (GetAsyncRelightInFlightCount() == 0)
      {
        AsyncRelightColumnsInFlight.erase(c.key);
      }
      else
      {
        continue;
      }
    }
    if (!Persistence)
    {
      continue;
    }
    Persistence->EnqueueTerrainColumnRelight(
        c.key.x * CHUNK_SIZE, c.key.y * CHUNK_SIZE, /*priority=*/true,
        c.min_y, c.max_y);
    ++drained;
  }
  return drained;
}

int UWorld::DrainIdleFocusPendingLightSync(glm::ivec3 focus_ground_horiz,
                                           int radius_chunks, int max_columns)
{
  // Manual 164613: sync RelightTerrainColumn here was 1–3s wall
  // (mesh_emerge_prep≈relight_drain, stream_ms small, holes=1). With
  // AsyncRelight, break-glass only priority-enqueues into the FIFO; paced
  // DrainRelightQueues owns Capture / Y-band (RelightCaptureBandCy).
  if (ProceduralTemplate.AsyncRelight)
  {
    return DrainIdleFocusPendingLight(focus_ground_horiz, radius_chunks,
                                      max_columns);
  }
  if (LightingRelightDeferred || !MeshService || !BlockRegistry ||
      max_columns <= 0 || radius_chunks < 0 ||
      PendingLightBeforeMesh.empty())
  {
    return 0;
  }
  if (LastMovementSpeed > ProceduralTemplate.MovementPrefetchThreshold)
  {
    return 0;
  }
  const int max_y = ProceduralTemplate.MaxHeight;
  const int sea = ProceduralTemplate.SeaLevel;
  int band_min =
      std::max(0, focus_ground_horiz.y * CHUNK_SIZE - CHUNK_SIZE);
  int band_max = std::min(max_y, focus_ground_horiz.y * CHUNK_SIZE +
                                      CHUNK_SIZE * 3 - 1);
  if (ProceduralTemplate.FillWater)
  {
    band_min = std::min(band_min, std::max(0, sea - CHUNK_SIZE * 4));
    band_max = std::max(band_max, std::min(max_y, sea + CHUNK_SIZE * 2));
  }
  const int cy0 = FloorDiv(band_min, CHUNK_SIZE);
  const int cy1 = FloorDiv(band_max, CHUNK_SIZE);

  struct Candidate
  {
    int dist;
    glm::ivec2 key;
    int min_y;
    int max_y;
  };
  std::vector<Candidate> candidates;
  candidates.reserve(PendingLightBeforeMesh.size());
  for (const auto &entry : PendingLightBeforeMesh)
  {
    const glm::ivec2 key = entry.first;
    const int dist = std::max(std::abs(key.x - focus_ground_horiz.x),
                              std::abs(key.y - focus_ground_horiz.z));
    if (dist > radius_chunks)
    {
      continue;
    }
    // V2a: underfeet may have no mesh yet — still sync-seed (was skipped).
    bool has_mesh = false;
    for (int cy = cy0; cy <= cy1; ++cy)
    {
      if (MeshService->HasGreedyMesh(glm::ivec3(key.x, cy, key.y)))
      {
        has_mesh = true;
        break;
      }
    }
    if (!has_mesh && dist > 1)
    {
      continue;
    }
    int remesh_min = band_min;
    int remesh_max = band_max;
    const int span = entry.second.max_y - entry.second.min_y;
    if (span >= 0 && span <= CHUNK_SIZE * 4)
    {
      remesh_min = std::min(remesh_min, entry.second.min_y);
      remesh_max = std::max(remesh_max, entry.second.max_y);
    }
    candidates.push_back({dist, key, remesh_min, remesh_max});
  }
  if (candidates.empty())
  {
    return 0;
  }
  // Outer ring first on sync break-glass — underfeet usually already seeded;
  // idle plateaus were west-strip ingress (dist≈focus_radius).
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate &a, const Candidate &b)
            { return a.dist > b.dist; });
  int drained = 0;
  for (const Candidate &c : candidates)
  {
    if (drained >= max_columns)
    {
      break;
    }
    AsyncRelightColumnsInFlight.erase(c.key);
    // Legacy path when AsyncRelight is off — full-column sync on main.
    RelightTerrainColumn(c.key.x * CHUNK_SIZE, c.key.y * CHUNK_SIZE, c.min_y,
                         c.max_y, /*priority_mesh=*/true,
                         /*include_skylight=*/true,
                         /*include_block_light=*/true);
    ++drained;
  }
  return drained;
}

int UWorld::CountPendingLightBeforeMeshNear(glm::ivec3 focus_ground_horiz,
                                            int radius_chunks) const
{
  if (PendingLightBeforeMesh.empty() || radius_chunks < 0)
  {
    return 0;
  }
  int count = 0;
  for (const auto &entry : PendingLightBeforeMesh)
  {
    const int dx = std::abs(entry.first.x - focus_ground_horiz.x);
    const int dz = std::abs(entry.first.y - focus_ground_horiz.z);
    if (std::max(dx, dz) <= radius_chunks)
    {
      ++count;
    }
  }
  return count;
}

int UWorld::CollectPendingLightFocusColumns(glm::ivec3 focus_ground_horiz,
                                            int radius_chunks,
                                            std::vector<glm::ivec2> &out,
                                            int max_cols) const
{
  out.clear();
  if (PendingLightBeforeMesh.empty() || radius_chunks < 0 || max_cols <= 0)
  {
    return 0;
  }
  struct Entry
  {
    int dist;
    glm::ivec2 key;
  };
  std::vector<Entry> entries;
  entries.reserve(PendingLightBeforeMesh.size());
  for (const auto &entry : PendingLightBeforeMesh)
  {
    const int dist = std::max(std::abs(entry.first.x - focus_ground_horiz.x),
                              std::abs(entry.first.y - focus_ground_horiz.z));
    if (dist > radius_chunks)
    {
      continue;
    }
    entries.push_back({dist, entry.first});
  }
  std::sort(entries.begin(), entries.end(),
            [](const Entry &a, const Entry &b) { return a.dist < b.dist; });
  const int n = std::min(max_cols, static_cast<int>(entries.size()));
  out.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i)
  {
    out.push_back(entries[static_cast<size_t>(i)].key);
  }
  return static_cast<int>(out.size());
}

int UWorld::CollectStickyRemeshFocusColumns(glm::ivec3 focus_ground_horiz,
                                            int radius_chunks,
                                            std::vector<glm::ivec2> &out,
                                            int max_cols) const
{
  out.clear();
  if (StickyRemeshAfterLight.empty() || radius_chunks < 0 || max_cols <= 0)
  {
    return 0;
  }
  struct Entry
  {
    int dist;
    glm::ivec2 key;
  };
  std::vector<Entry> entries;
  entries.reserve(StickyRemeshAfterLight.size());
  for (const glm::ivec2 &key : StickyRemeshAfterLight)
  {
    const int dist = std::max(std::abs(key.x - focus_ground_horiz.x),
                              std::abs(key.y - focus_ground_horiz.z));
    if (dist > radius_chunks)
    {
      continue;
    }
    entries.push_back({dist, key});
  }
  std::sort(entries.begin(), entries.end(),
            [](const Entry &a, const Entry &b) { return a.dist < b.dist; });
  const int n = std::min(max_cols, static_cast<int>(entries.size()));
  out.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i)
  {
    out.push_back(entries[static_cast<size_t>(i)].key);
  }
  return static_cast<int>(out.size());
}

int UWorld::CollectStaleDarkFocusColumns(glm::ivec3 focus_ground_horiz,
                                         int radius_chunks,
                                         std::vector<glm::ivec2> &out,
                                         int max_cols) const
{
  out.clear();
  if (!MeshService || radius_chunks < 0 || max_cols <= 0)
  {
    return 0;
  }
  const int max_cy =
      std::max(0, FloorDiv(ProceduralTemplate.MaxHeight, CHUNK_SIZE));
  struct Entry
  {
    int dist;
    glm::ivec2 key;
  };
  std::vector<Entry> entries;
  entries.reserve(64);
  for (int dx = -radius_chunks; dx <= radius_chunks; ++dx)
  {
    for (int dz = -radius_chunks; dz <= radius_chunks; ++dz)
    {
      const int dist = std::max(std::abs(dx), std::abs(dz));
      // Include horiz 1–2 (edge/near ring). Only skip camera column (dist 0)
      // so underfeet dark preview is not force-ticketed every frame.
      if (dist < 1)
      {
        continue;
      }
      const glm::ivec2 key(focus_ground_horiz.x + dx, focus_ground_horiz.z + dz);
      if (IsColumnStickyRemesh(key))
      {
        continue; // sticky path already tickets RemeshSeam
      }
      bool stale = false;
      for (int cy = 0; cy <= max_cy; ++cy)
      {
        const glm::ivec3 coord(key.x, cy, key.y);
        if (MeshService->HasGreedyMesh(coord) &&
            MeshService->ChunkHasStaleDarkFaces(coord, BlockWorld))
        {
          stale = true;
          break;
        }
      }
      if (stale)
      {
        entries.push_back({dist, key});
      }
    }
  }
  std::sort(entries.begin(), entries.end(),
            [](const Entry &a, const Entry &b) { return a.dist < b.dist; });
  const int n = std::min(max_cols, static_cast<int>(entries.size()));
  out.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i)
  {
    out.push_back(entries[static_cast<size_t>(i)].key);
  }
  return static_cast<int>(out.size());
}

int UWorld::CollectFullyDarkFocusColumns(glm::ivec3 focus_ground_horiz,
                                         int radius_chunks,
                                         std::vector<glm::ivec2> &out,
                                         int max_cols) const
{
  out.clear();
  if (!MeshService || radius_chunks < 0 || max_cols <= 0)
  {
    return 0;
  }
  const int max_cy =
      std::max(0, FloorDiv(ProceduralTemplate.MaxHeight, CHUNK_SIZE));
  struct Entry
  {
    int dist;
    glm::ivec2 key;
  };
  std::vector<Entry> entries;
  entries.reserve(64);
  for (int dx = -radius_chunks; dx <= radius_chunks; ++dx)
  {
    for (int dz = -radius_chunks; dz <= radius_chunks; ++dz)
    {
      const int dist = std::max(std::abs(dx), std::abs(dz));
      if (dist < 1 || dist > 2)
      {
        continue; // near-ring only (void Relight thrash guard)
      }
      const glm::ivec2 key(focus_ground_horiz.x + dx, focus_ground_horiz.z + dz);
      if (IsColumnStickyRemesh(key))
      {
        continue;
      }
      bool fully_dark = false;
      for (int cy = 0; cy <= max_cy; ++cy)
      {
        const glm::ivec3 coord(key.x, cy, key.y);
        if (MeshService->HasGreedyMesh(coord) &&
            MeshService->GetCache().ChunkHasFullyDarkFace(coord))
        {
          fully_dark = true;
          break;
        }
      }
      if (fully_dark)
      {
        entries.push_back({dist, key});
      }
    }
  }
  std::sort(entries.begin(), entries.end(),
            [](const Entry &a, const Entry &b) { return a.dist < b.dist; });
  const int n = std::min(max_cols, static_cast<int>(entries.size()));
  out.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i)
  {
    out.push_back(entries[static_cast<size_t>(i)].key);
  }
  return static_cast<int>(out.size());
}

void UWorld::NoteColumnRepairNeeded(glm::ivec2 ground_xz)
{
  StickyRemeshAfterLight.insert(ground_xz);
}

bool UWorld::NeedsSpawnRingCatchUp() const
{
  return CountPostLoadRingNotReady() > 0;
}

std::string UWorld::FormatPendingLightFocusColumns(
    glm::ivec3 focus_ground_horiz, int radius_chunks, int max_cols) const
{
  if (PendingLightBeforeMesh.empty() || radius_chunks < 0 || max_cols <= 0)
  {
    return {};
  }
  struct Entry
  {
    int dist;
    glm::ivec2 key;
  };
  std::vector<Entry> entries;
  entries.reserve(PendingLightBeforeMesh.size());
  for (const auto &entry : PendingLightBeforeMesh)
  {
    const int dist = std::max(std::abs(entry.first.x - focus_ground_horiz.x),
                              std::abs(entry.first.y - focus_ground_horiz.z));
    if (dist > radius_chunks)
    {
      continue;
    }
    entries.push_back({dist, entry.first});
  }
  if (entries.empty())
  {
    return {};
  }
  std::sort(entries.begin(), entries.end(),
            [](const Entry &a, const Entry &b)
            {
              if (a.dist != b.dist)
              {
                return a.dist > b.dist;
              }
              if (a.key.x != b.key.x)
              {
                return a.key.x < b.key.x;
              }
              return a.key.y < b.key.y;
            });
  std::string out;
  const int n = std::min(max_cols, static_cast<int>(entries.size()));
  for (int i = 0; i < n; ++i)
  {
    if (!out.empty())
    {
      out += ',';
    }
    out += '(' + std::to_string(entries[static_cast<size_t>(i)].key.x) + ',' +
           std::to_string(entries[static_cast<size_t>(i)].key.y) + ')';
  }
  return out;
}

int UWorld::CountBlackStickyFocusMeshes(glm::ivec3 focus_ground_chunk,
                                        int radius_chunks) const
{
  if (!MeshService || radius_chunks < 0 || StickyRemeshAfterLight.empty())
  {
    return 0;
  }
  const int max_y = ProceduralTemplate.MaxHeight;
  const int sea = ProceduralTemplate.SeaLevel;
  int band_min =
      std::max(0, focus_ground_chunk.y * CHUNK_SIZE - CHUNK_SIZE);
  int band_max = std::min(max_y, focus_ground_chunk.y * CHUNK_SIZE +
                                      CHUNK_SIZE * 3 - 1);
  if (ProceduralTemplate.FillWater)
  {
    band_min = std::min(band_min, std::max(0, sea - CHUNK_SIZE * 4));
    band_max = std::max(band_max, std::min(max_y, sea + CHUNK_SIZE * 2));
  }
  const int cy0 = FloorDiv(band_min, CHUNK_SIZE);
  const int cy1 = FloorDiv(band_max, CHUNK_SIZE);
  int sticky = 0;
  for (const glm::ivec2 &key : StickyRemeshAfterLight)
  {
    const int dist =
        std::max(std::abs(key.x - focus_ground_chunk.x),
                 std::abs(key.y - focus_ground_chunk.z));
    if (dist > radius_chunks)
    {
      continue;
    }
    for (int cy = cy0; cy <= cy1; ++cy)
    {
      const glm::ivec3 coord(key.x, cy, key.y);
      // I6: sticky set can linger after lit remesh; only count black/stale debt.
      if (MeshService->HasGreedyMesh(coord) &&
          MeshService->ChunkHasStaleDarkFaces(coord, BlockWorld))
      {
        ++sticky;
        break;
      }
    }
  }
  return sticky;
}

int UWorld::CountPendingDarkFocusMeshes(glm::ivec3 focus_ground_chunk,
                                        int radius_chunks) const
{
  // Soft-defer first mesh under PendingLight: terrain exists but is black.
  // Separate from StickyRemeshAfterLight so SyncIdle is not spuriously opened.
  if (!MeshService || radius_chunks < 0 || PendingLightBeforeMesh.empty())
  {
    return 0;
  }
  const int max_y = ProceduralTemplate.MaxHeight;
  const int sea = ProceduralTemplate.SeaLevel;
  int band_min =
      std::max(0, focus_ground_chunk.y * CHUNK_SIZE - CHUNK_SIZE);
  int band_max = std::min(max_y, focus_ground_chunk.y * CHUNK_SIZE +
                                      CHUNK_SIZE * 3 - 1);
  if (ProceduralTemplate.FillWater)
  {
    band_min = std::min(band_min, std::max(0, sea - CHUNK_SIZE * 4));
    band_max = std::max(band_max, std::min(max_y, sea + CHUNK_SIZE * 2));
  }
  const int cy0 = FloorDiv(band_min, CHUNK_SIZE);
  const int cy1 = FloorDiv(band_max, CHUNK_SIZE);
  int dark = 0;
  for (const auto &entry : PendingLightBeforeMesh)
  {
    const glm::ivec2 &key = entry.first;
    const int dist =
        std::max(std::abs(key.x - focus_ground_chunk.x),
                 std::abs(key.y - focus_ground_chunk.z));
    if (dist > radius_chunks)
    {
      continue;
    }
    for (int cy = cy0; cy <= cy1; ++cy)
    {
      if (MeshService->HasGreedyMesh(glm::ivec3(key.x, cy, key.y)))
      {
        ++dark;
        break;
      }
    }
  }
  return dark;
}

bool UWorld::IsColumnStickyRemesh(glm::ivec2 ground_xz) const
{
  return StickyRemeshAfterLight.find(ground_xz) != StickyRemeshAfterLight.end();
}

int UWorld::SyncIdleFocusGreedyRemesh(int max_columns)
{
  if (!MeshService || !BlockRegistry || max_columns <= 0)
  {
    return 0;
  }
  const glm::ivec3 focus =
      UChunkManager::WorldToChunk(GetPreferredLoadFocusBlock());
  const int radius = GetStreamingFocusRadius();
  const int max_y = ProceduralTemplate.MaxHeight;
  // Narrow player band only — full sea±4*CHUNK caused mesh_emerge 500–740ms
  // spikes on manual stop (perf_20260720-194911).
  const int band_min =
      std::max(0, focus.y * CHUNK_SIZE - CHUNK_SIZE);
  const int band_max =
      std::min(max_y, focus.y * CHUNK_SIZE + CHUNK_SIZE * 2 - 1);
  const int preferred_cy = focus.y;
  struct Candidate
  {
    int dist;
    glm::ivec2 key;
    int min_y;
    int max_y;
  };
  std::vector<Candidate> candidates;
  candidates.reserve(StickyRemeshAfterLight.size() + 8);
  auto try_add = [&](const glm::ivec2 &key, int dist)
  {
    const glm::ivec3 ground(key.x, 0, key.y);
    if (!IsColumnLitReady(ground) &&
        GetColumnEmergeState(ground) != ColumnEmergeState::Meshing)
    {
      return;
    }
    int remesh_min = band_min;
    int remesh_max = band_max;
    if (const auto pit = PendingLightBeforeMesh.find(key);
        pit != PendingLightBeforeMesh.end())
    {
      const int span = pit->second.max_y - pit->second.min_y;
      if (span >= 0 && span <= CHUNK_SIZE * 3)
      {
        remesh_min = std::min(remesh_min, pit->second.min_y);
        remesh_max = std::max(remesh_max, pit->second.max_y);
      }
    }
    // Clamp to ±1 cy around player — at most 3 sync rebuilds per column.
    remesh_min = std::max(remesh_min, (preferred_cy - 1) * CHUNK_SIZE);
    remesh_max = std::min(remesh_max, (preferred_cy + 1) * CHUNK_SIZE + CHUNK_SIZE - 1);
    bool has_mesh = false;
    for (int cy = FloorDiv(remesh_min, CHUNK_SIZE);
         cy <= FloorDiv(remesh_max, CHUNK_SIZE); ++cy)
    {
      if (MeshService->HasGreedyMesh(glm::ivec3(key.x, cy, key.y)))
      {
        has_mesh = true;
        break;
      }
    }
    if (!has_mesh)
    {
      return;
    }
    candidates.push_back({dist, key, remesh_min, remesh_max});
  };
  for (const glm::ivec2 &key : StickyRemeshAfterLight)
  {
    const int dist =
        std::max(std::abs(key.x - focus.x), std::abs(key.y - focus.z));
    if (dist > radius)
    {
      continue;
    }
    try_add(key, dist);
  }
  if (candidates.empty())
  {
    for (int dz = -radius; dz <= radius; ++dz)
    {
      for (int dx = -radius; dx <= radius; ++dx)
      {
        try_add(glm::ivec2(focus.x + dx, focus.z + dz),
                std::max(std::abs(dx), std::abs(dz)));
      }
    }
  }
  if (candidates.empty())
  {
    return 0;
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate &a, const Candidate &b)
            { return a.dist < b.dist; });
  int synced = 0;
  std::vector<glm::ivec2> synced_keys;
  synced_keys.reserve(static_cast<size_t>(max_columns));
  for (const Candidate &c : candidates)
  {
    if (synced >= max_columns)
    {
      break;
    }
    const glm::ivec3 ground(c.key.x, 0, c.key.y);
    MeshService->MarkTerrainChunkMeshDirtySeamedPriority(
        ground, c.min_y, c.max_y, /*include_horizontal_neighbors=*/false);
    SetColumnEmergeState(ground, ColumnEmergeState::Meshing);
    synced_keys.push_back(c.key);
    ++synced;
  }
  if (synced <= 0)
  {
    return 0;
  }
  for (const glm::ivec2 &key : synced_keys)
  {
    const Candidate *found = nullptr;
    for (const Candidate &c : candidates)
    {
      if (c.key == key)
      {
        found = &c;
        break;
      }
    }
    if (!found)
    {
      continue;
    }
    const int cy0 = FloorDiv(found->min_y, CHUNK_SIZE);
    const int cy1 = FloorDiv(found->max_y, CHUNK_SIZE);
    for (int cy = cy0; cy <= cy1; ++cy)
    {
      // MarkDirty→async only. RebuildChunkImmediate here caused seconds-scale
      // mesh_emerge (manual 190126: emerge~3.7s with imm wiped by later Reset).
      MeshService->MarkDirtyPriority(glm::ivec3(key.x, cy, key.y));
    }
    StickyRemeshAfterLight.erase(key);
    PendingLightBeforeMesh.erase(key);
    SetColumnEmergeState(glm::ivec3(key.x, 0, key.y),
                         ColumnEmergeState::Meshing);
  }
  return synced;
}

int UWorld::ClearPendingLightAfterMeshCommitted(int max_columns)
{
  if (!MeshService || max_columns <= 0 ||
      (PendingLightBeforeMesh.empty() && StickyRemeshAfterLight.empty()))
  {
    return 0;
  }
  const glm::ivec3 focus =
      UChunkManager::WorldToChunk(GetPreferredLoadFocusBlock());
  const int radius = GetStreamingFocusRadius();
  const int max_y = ProceduralTemplate.MaxHeight;
  const int sea = ProceduralTemplate.SeaLevel;
  int band_min =
      std::max(0, focus.y * CHUNK_SIZE - CHUNK_SIZE);
  int band_max =
      std::min(max_y, focus.y * CHUNK_SIZE + CHUNK_SIZE * 3 - 1);
  if (ProceduralTemplate.FillWater)
  {
    band_min = std::min(band_min, std::max(0, sea - CHUNK_SIZE * 4));
    band_max = std::max(band_max, std::min(max_y, sea + CHUNK_SIZE * 2));
  }
  const int cy0 = FloorDiv(band_min, CHUNK_SIZE);
  const int cy1 = FloorDiv(band_max, CHUNK_SIZE);
  int cleared = 0;
  for (auto it = PendingLightBeforeMesh.begin();
       it != PendingLightBeforeMesh.end() && cleared < max_columns;)
  {
    const glm::ivec2 key = it->first;
    const int dist =
        std::max(std::abs(key.x - focus.x), std::abs(key.y - focus.z));
    if (dist > radius)
    {
      ++it;
      continue;
    }
    const glm::ivec3 ground(key.x, 0, key.y);
    if (!IsColumnLitReady(ground))
    {
      ++it;
      continue;
    }
    bool has_mesh = false;
    for (int cy = cy0; cy <= cy1; ++cy)
    {
      if (MeshService->HasGreedyMesh(glm::ivec3(key.x, cy, key.y)))
      {
        has_mesh = true;
        break;
      }
    }
    if (!has_mesh)
    {
      ++it;
      continue;
    }
    if (MeshService->HasDirtyInColumnBand(key, it->second.min_y, it->second.max_y))
    {
      ++it;
      continue;
    }
    AsyncRelightColumnsInFlight.erase(key);
    it = PendingLightBeforeMesh.erase(it);
    StickyRemeshAfterLight.erase(key);
    SetColumnEmergeState(ground, ColumnEmergeState::RenderReady);
    ++cleared;
  }
  for (auto it = StickyRemeshAfterLight.begin();
       it != StickyRemeshAfterLight.end() && cleared < max_columns;)
  {
    const glm::ivec2 key = *it;
    if (PendingLightBeforeMesh.count(key) != 0)
    {
      ++it;
      continue;
    }
    const int dist =
        std::max(std::abs(key.x - focus.x), std::abs(key.y - focus.z));
    if (dist > radius)
    {
      it = StickyRemeshAfterLight.erase(it);
      continue;
    }
    const glm::ivec3 ground(key.x, 0, key.y);
    if (!IsColumnLitReady(ground))
    {
      ++it;
      continue;
    }
    bool has_mesh = false;
    for (int cy = cy0; cy <= cy1; ++cy)
    {
      if (MeshService->HasGreedyMesh(glm::ivec3(key.x, cy, key.y)))
      {
        has_mesh = true;
        break;
      }
    }
    if (!has_mesh)
    {
      ++it;
      continue;
    }
    // I6: lit + mesh + no stale-dark → clear sticky even if Dirty still queues
    // remesh. Holding sticky for Dirty pinned autofly post_stop sticky 4–6 while
    // manual calm clears to 0 (async remesh does not need sticky SoT).
    bool still_stale = false;
    for (int cy = cy0; cy <= cy1; ++cy)
    {
      const glm::ivec3 coord(key.x, cy, key.y);
      if (MeshService->HasGreedyMesh(coord) &&
          MeshService->ChunkHasStaleDarkFaces(coord, BlockWorld))
      {
        still_stale = true;
        break;
      }
    }
    if (still_stale)
    {
      ++it;
      continue;
    }
    if (MeshService->HasDirtyInColumnBand(key, band_min, band_max))
    {
      // Keep draining remesh, but sticky gate is for black/stale faces only.
      it = StickyRemeshAfterLight.erase(it);
      SetColumnEmergeState(ground, ColumnEmergeState::RenderReady);
      ++cleared;
      continue;
    }
    // Keep sticky only while stale-dark remesh debt remains. Void-edge faces
    // are Relight-owned — holding sticky forced RemeshSeam thrash (201621).
    it = StickyRemeshAfterLight.erase(it);
    SetColumnEmergeState(ground, ColumnEmergeState::RenderReady);
    ++cleared;
  }
  return cleared;
}

int UWorld::PruneStickyRemeshOutside(glm::ivec3 focus_ground_chunk,
                                     int radius_chunks)
{
  if (StickyRemeshAfterLight.empty() || radius_chunks < 0)
  {
    return 0;
  }
  int pruned = 0;
  for (auto it = StickyRemeshAfterLight.begin();
       it != StickyRemeshAfterLight.end();)
  {
    const int dist = std::max(std::abs(it->x - focus_ground_chunk.x),
                              std::abs(it->y - focus_ground_chunk.z));
    if (dist > radius_chunks)
    {
      it = StickyRemeshAfterLight.erase(it);
      ++pruned;
    }
    else
    {
      ++it;
    }
  }
  return pruned;
}

int UWorld::PromoteNearTerrainColumnRelights(glm::ivec3 focus_ground_horiz,
                                             int radius_chunks)
{
  if (!Persistence || radius_chunks < 0)
  {
    return 0;
  }
  return Persistence->PromoteNearTerrainColumnRelights(focus_ground_horiz,
                                                       radius_chunks);
}

int UWorld::PromotePendingLightRelightsNear(glm::ivec3 focus_ground_horiz,
                                            int radius_chunks)
{
  if (!Persistence || PendingLightBeforeMesh.empty() || radius_chunks < 0)
  {
    return 0;
  }
  glm::ivec3 nearest_hole{};
  const bool nearest_missing_hole =
      MeshService &&
      MeshService->FindNearestMissingGreedyMesh(
          BlockWorld, focus_ground_horiz, radius_chunks, nearest_hole);
  int promoted = 0;
  for (const auto &entry : PendingLightBeforeMesh)
  {
    const int dx = std::abs(entry.first.x - focus_ground_horiz.x);
    const int dz = std::abs(entry.first.y - focus_ground_horiz.z);
    if (std::max(dx, dz) > radius_chunks)
    {
      continue;
    }
    // Job already running — do not requeue while flying (Keys-ghost risk).
    // Idle: clear InFlight for the whole focus. Underfeet-only clear left
    // pendf~40 stuck with relight_drain≈0 while wall~22ms (manual 083042):
    // DrainRelightQueues drops FIFO entries that still look "in flight".
    // SoftDefer hole: clear InFlight only for the nearest missing column —
    // clearing every missing pending column thrashed live jobs (P0_no_dark).
    if (AsyncRelightColumnsInFlight.count(entry.first) != 0)
    {
      const bool idle =
          LastMovementSpeed <= ProceduralTemplate.MovementPrefetchThreshold;
      const int pending_focus =
          CountPendingLightBeforeMeshNear(focus_ground_horiz, radius_chunks);
      const bool nearest_hole_col =
          nearest_missing_hole && entry.first.x == nearest_hole.x &&
          entry.first.y == nearest_hole.z;
      // Cruise with high focus debt: stale InFlight blocked re-enqueue and left
      // outer-ring preview black (manual 161327: z=34 strip, pend~28).
      if (GetAsyncRelightInFlightCount() == 0 || idle || pending_focus > 8 ||
          nearest_hole_col)
      {
        AsyncRelightColumnsInFlight.erase(entry.first);
      }
      else
      {
        continue;
      }
    }
    Persistence->EnqueueTerrainColumnRelight(
        entry.first.x * CHUNK_SIZE, entry.first.y * CHUNK_SIZE,
        /*priority=*/true, entry.second.min_y, entry.second.max_y);
    ++promoted;
  }
  return promoted;
}

void UWorld::PromotePendingLightBeforeMesh(
    const std::vector<glm::ivec3> &relit_chunks, bool priority_mesh)
{
  if (relit_chunks.empty() || PendingLightBeforeMesh.empty())
  {
    return;
  }
  std::unordered_set<glm::ivec2, GroundColumnHash> grounds;
  grounds.reserve(relit_chunks.size());
  for (const glm::ivec3 &coord : relit_chunks)
  {
    grounds.insert(glm::ivec2(coord.x, coord.z));
  }
  for (const glm::ivec2 &ground_xz : grounds)
  {
    const auto it = PendingLightBeforeMesh.find(ground_xz);
    if (it == PendingLightBeforeMesh.end())
    {
      continue;
    }
    const glm::ivec3 ground(ground_xz.x, 0, ground_xz.y);
    if (priority_mesh)
    {
      MeshService->MarkTerrainChunkMeshDirtySeamedPriority(
          ground, it->second.min_y, it->second.max_y, true);
    }
    else
    {
      MeshService->MarkTerrainChunkMeshDirtySeamed(
          ground, it->second.min_y, it->second.max_y, true);
    }
    SetColumnEmergeState(ground, ColumnEmergeState::Meshing);
  }
}

void UWorld::ApplyEditFastRelight(
    const std::vector<glm::ivec3> &block_positions)
{
  if (LightingRelightDeferred || !BlockRegistry || block_positions.empty())
  {
    return;
  }
  // Drop dig-era inflight meshes before light mutate — otherwise a late Apply
  // can paint dig faces dark after place Immediate (manual 092611).
  if (MeshService)
  {
    MeshService->InvalidateEditMeshNeighborhood(block_positions);
  }
  const auto t0 = std::chrono::high_resolution_clock::now();
  // Incremental remove+flood (no chunk Clear). Immediate after this samples
  // correct sky/block light day/night/torch; SoftDefer must not freeze a
  // wrong bake until MarkRelit (manual 222250 sticky ~48s; night torch stale).
  GetLightingPipeline().RelightBlocksAroundEdit(BlockWorld, *BlockRegistry,
                                                block_positions);
  const auto t1 = std::chrono::high_resolution_clock::now();
  PhysicsTelemetryData.FastRelightMs =
      std::chrono::duration<double, std::milli>(t1 - t0).count();
}

void UWorld::ApplyEditLighting(const std::vector<glm::ivec3> &block_positions)
{
  if (block_positions.empty())
  {
    return;
  }
  // Incremental RelightBlocksAroundEdit already updated local light. Async
  // player-relight Clears the neighborhood and MarkRelit remeshed dig sites
  // dark until later bands finished — dig→place temporary blackness
  // (manual 092611). Do not enqueue Clear refinement after edit.
  (void)block_positions;
  if (Persistence)
  {
    PhysicsTelemetryData.PendingPlayerRelights =
        static_cast<uint64_t>(Persistence->GetPendingPlayerRelightCount());
    PhysicsTelemetryData.PendingBackgroundRelights = static_cast<uint64_t>(
        Persistence->GetPendingTerrainColumnRelightCount());
  }
}

void UWorld::TickPlayerRelightMeshBurst()
{
  if (PlayerRelightMeshBurstFrames > 0)
  {
    --PlayerRelightMeshBurstFrames;
  }
}

void UWorld::EnsureAsyncRelightBuilder()
{
  if (!AsyncRelight)
  {
    const std::size_t threads = static_cast<std::size_t>(
        std::clamp(ProceduralTemplate.RelightThreadCount, 1, 8));
    AsyncRelight = std::make_unique<UAsyncRelightBuilder>(threads);
  }
}

void UWorld::EnqueueAsyncPlayerRelight(
    const std::vector<glm::ivec3> &block_positions, int min_world_y)
{
  if (!BlockRegistry)
  {
    return;
  }
  EnsureAsyncRelightBuilder();
  RelightJobSpec spec;
  spec.block_positions = block_positions;
  spec.min_world_y = min_world_y;
  spec.max_world_y = ProceduralTemplate.MaxHeight;
  spec.include_skylight = true;
  spec.include_block_light = true;
  spec.frontier_iterations = kRelightFrontierIterationsEdit;
  spec.job_id = ++NextAsyncRelightJobId;
  AsyncRelight->EnqueueJob(BlockWorld, std::move(spec), *BlockRegistry);
}

void UWorld::EnqueueAsyncTerrainColumnRelight(int world_x, int world_z,
                                              int min_y, int max_y,
                                              bool include_skylight,
                                              bool include_block_light,
                                              bool finalize_pending_gate)
{
  if (!BlockRegistry)
  {
    return;
  }
  const glm::ivec3 chunk = UChunkManager::WorldToChunk(
      glm::ivec3(world_x, min_y, world_z));
  const glm::ivec2 col(chunk.x, chunk.z);
  if (AsyncRelightColumnsInFlight.count(col) != 0)
  {
    if (GetAsyncRelightInFlightCount() > 0)
    {
      return;
    }
    AsyncRelightColumnsInFlight.erase(col);
  }
  EnsureAsyncRelightBuilder();
  AsyncRelightColumnsInFlight.insert(col);
  RelightJobSpec spec;
  spec.block_positions = {glm::ivec3(world_x, min_y, world_z)};
  spec.min_world_y = min_y;
  spec.max_world_y = max_y;
  spec.include_skylight = include_skylight;
  spec.include_block_light = include_block_light;
  spec.frontier_iterations = kRelightFrontierIterationsFull;
  spec.job_id = ++NextAsyncRelightJobId;
  spec.finalize_pending_gate = finalize_pending_gate;
  AsyncRelight->EnqueueJob(BlockWorld, std::move(spec), *BlockRegistry);
}

void UWorld::EnqueueAsyncChunkSkylightRelight(glm::ivec3 chunk_coord,
                                              int frontier_iterations)
{
  EnqueueAsyncChunkRelight(chunk_coord, true, false, frontier_iterations);
}

void UWorld::EnqueueAsyncChunkRelight(glm::ivec3 chunk_coord,
                                      bool include_skylight,
                                      bool include_block_light,
                                      int frontier_iterations)
{
  if (!BlockRegistry)
  {
    return;
  }
  if (!include_skylight && !include_block_light)
  {
    return;
  }
  EnsureAsyncRelightBuilder();
  const glm::ivec3 origin = chunk_coord * CHUNK_SIZE;
  RelightJobSpec spec;
  spec.block_positions = {origin};
  spec.min_world_y = origin.y;
  spec.max_world_y =
      std::min(ProceduralTemplate.MaxHeight, origin.y + CHUNK_SIZE - 1);
  spec.include_skylight = include_skylight;
  spec.include_block_light = include_block_light;
  spec.frontier_iterations = frontier_iterations;
  spec.job_id = ++NextAsyncRelightJobId;
  AsyncRelight->EnqueueJob(BlockWorld, std::move(spec), *BlockRegistry);
}

int UWorld::DrainAsyncRelightResults(int max_per_frame, bool priority_mesh,
                                     bool enqueue_background_frontier)
{
  if (!AsyncRelight || !BlockRegistry)
  {
    return 0;
  }
  const auto t0 = std::chrono::high_resolution_clock::now();
  int applied = 0;
  if (Persistence)
  {
    for (const glm::ivec3 &pos : AsyncRelight->TakeOverflowSourcePositions())
    {
      Persistence->EnqueueTerrainColumnRelight(pos.x, pos.z);
      const glm::ivec3 chunk = UChunkManager::WorldToChunk(pos);
      AsyncRelightColumnsInFlight.erase(glm::ivec2(chunk.x, chunk.z));
    }
  }
  for (RelightComputeResult &result :
       AsyncRelight->DrainCompleted(max_per_frame))
  {
    ++applied;
    std::vector<glm::ivec3> relit_coords;
    relit_coords.reserve(result.chunks.size());
    for (const RelightChunkLightData &chunk_data : result.chunks)
    {
      if (UChunk *chunk =
              BlockWorld.GetChunkManager().GetChunk(chunk_data.coord))
      {
        if (result.include_skylight &&
            ApplyGpuSkylightSeedToChunk(*chunk, *BlockRegistry))
        {
          if (result.include_block_light)
          {
            MergeBlockLightKeepingGpuSky(*chunk, chunk_data.light_packed);
          }
        }
        else
        {
          chunk->GetLightDataMutable() = chunk_data.light_packed;
        }
        relit_coords.push_back(chunk_data.coord);
      }
    }
    // Always remesh after light apply. Deferring via Accumulate/Flush left
    // light=0 meshes stuck black under Dirty backlog (especially while flying).
    std::vector<glm::ivec2> primary_grounds;
    primary_grounds.reserve(result.source_block_positions.size());
    for (const glm::ivec3 &pos : result.source_block_positions)
    {
      const glm::ivec3 chunk = UChunkManager::WorldToChunk(pos);
      primary_grounds.push_back(glm::ivec2(chunk.x, chunk.z));
    }
    MarkRelitChunksForMesh(relit_coords, /*priority_mesh=*/true, primary_grounds,
                           result.finalize_pending_gate);
    // Ensure inflight tracking clears even when MarkRelit only remeshed
    // neighbors (primary already erased inside MarkRelit).
    for (const glm::ivec2 &g : primary_grounds)
    {
      AsyncRelightColumnsInFlight.erase(g);
    }
    if (priority_mesh)
    {
      PlayerRelightMeshBurstFrames = 3;
    }
    if (enqueue_background_frontier && result.frontier_unfinished &&
        Persistence)
    {
      for (const glm::ivec3 &pos : result.source_block_positions)
      {
        Persistence->EnqueueTerrainColumnRelight(pos.x, pos.z);
      }
    }
  }
  const auto t1 = std::chrono::high_resolution_clock::now();
  if (applied > 0 || AsyncRelight->HasPendingWork())
  {
    PhysicsTelemetryData.FullRelightMs =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
  }
  PhysicsTelemetryData.AsyncRelightInflight =
      static_cast<uint64_t>(AsyncRelight->GetInFlightCount());
  PhysicsTelemetryData.RelightDiscardedLate =
      AsyncRelight->GetDiscardedLateCount();
  if (Persistence)
  {
    for (const glm::ivec3 &pos : AsyncRelight->TakeDiscardedSourcePositions())
    {
      const glm::ivec3 chunk = UChunkManager::WorldToChunk(pos);
      AsyncRelightColumnsInFlight.erase(glm::ivec2(chunk.x, chunk.z));
      Persistence->EnqueueTerrainColumnRelight(pos.x, pos.z);
    }
  }
  return applied;
}

void UWorld::DrainAsyncRelightResults()
{
  DrainAsyncRelightResults(4, true, true);
}

bool UWorld::HasPendingAsyncRelightWork() const
{
  return AsyncRelight && AsyncRelight->HasPendingWork();
}

int UWorld::GetAsyncRelightInFlightCount() const
{
  return AsyncRelight ? AsyncRelight->GetInFlightCount() : 0;
}

size_t UWorld::GetRelightCompletedSize() const
{
  return AsyncRelight ? AsyncRelight->GetCompletedSize() : 0;
}

size_t UWorld::GetRelightCompletedCapacity() const
{
  return AsyncRelight ? AsyncRelight->GetCompletedCapacity() : 0;
}

uint64_t UWorld::GetRelightCompletedDiscardedOverflow() const
{
  return AsyncRelight ? AsyncRelight->GetCompletedDiscardedOverflow() : 0;
}

void UWorld::SetRelightCompletedCapacity(size_t cap)
{
  EnsureAsyncRelightBuilder();
  if (AsyncRelight)
  {
    AsyncRelight->SetCompletedCapacity(cap);
  }
}

bool UWorld::IsAsyncRelightColumnInFlight(glm::ivec2 ground_xz) const
{
  return AsyncRelightColumnsInFlight.count(ground_xz) != 0;
}

void UWorld::ReconcileAsyncRelightColumnInFlight()
{
  if (GetAsyncRelightInFlightCount() == 0 && !AsyncRelightColumnsInFlight.empty())
  {
    AsyncRelightColumnsInFlight.clear();
    return;
  }
  // More InFlight keys than workers can explain — stale keys from completed jobs.
  if (GetAsyncRelightInFlightCount() > 0 &&
      AsyncRelightColumnsInFlight.size() >
          static_cast<size_t>(GetAsyncRelightInFlightCount()) + 4u)
  {
    AsyncRelightColumnsInFlight.clear();
  }
}

uint64_t UWorld::GetRelightDiscardedLateCount() const
{
  return AsyncRelight ? AsyncRelight->GetDiscardedLateCount() : 0;
}

uint64_t UWorld::GetMeshDiscardedLateCount() const
{
  return MeshService ? MeshService->GetMeshDiscardedLateCount() : 0;
}

bool UWorld::HasPersistedTerrainOnDisk(const std::string &world_folder_path)
{
  return UWorldPersistence::HasPersistedTerrainOnDisk(world_folder_path);
}

UChunkStorageService &UWorld::GetChunkStorage()
{
  return Persistence->GetChunkStorage();
}

const UChunkStorageService &UWorld::GetChunkStorage() const
{
  return Persistence->GetChunkStorage();
}

const std::string &UWorld::GetWorldFolderPath() const
{
  return Persistence->GetWorldFolderPath();
}

void UWorld::SetWorldFolderPath(const std::string &path)
{
  Persistence->SetWorldFolderPath(path);
}

void UWorld::GenerateUsers()
{
  AddUser("Username");
  ApplySpawnToCamera();
}

std::string UWorld::GetWorldName() const { return WorldName; }

void UWorld::SetWorldName(const std::string &value) { WorldName = value; }

glm::vec3 UWorld::GetSpawnPoint() const { return SpawnPoint; }

glm::ivec3 UWorld::GetPreferredLoadFocusBlock() const
{
  if (auto user = GetCurrentUser())
  {
    return WorldPosToBlock(user->GetPosition());
  }
  if (!CurrentUserName.empty())
  {
    if (auto user = const_cast<UWorld *>(this)->GetUser(CurrentUserName))
    {
      return WorldPosToBlock(user->GetPosition());
    }
  }
  for (const auto &entry : Users)
  {
    if (entry.second)
    {
      return WorldPosToBlock(entry.second->GetPosition());
    }
  }
  return WorldPosToBlock(SpawnPoint);
}

void UWorld::SetSpawnPoint(glm::vec3 value) { SpawnPoint = value; }

void UWorld::SetTerrainParams(uint32_t Seed, const std::string &terrainType)
{
  WorldSeed = Seed;
  TerrainType = terrainType;
  ProceduralSettings settings;
  settings.Seed = Seed;
  settings.Generator = ProceduralGeneratorFromString(terrainType);
  ResolveProceduralDefaults(settings);
  ApplyGeneratorTierDefaults(settings);
  SetProceduralSettings(settings);
  if (BlockRegistry)
  {
    Streaming->EnsureStreamer(BlockWorld, *BlockRegistry, WorldSeed,
                              ProceduralTemplate);
  }
}

void UWorld::SetProceduralSettings(const ProceduralSettings &settings,
                                   bool rebuildPipeline)
{
  ProceduralTemplate = settings;
  WorldSeed = settings.Seed;
  TerrainType = ProceduralGeneratorToString(settings.Generator);
  MaxLoadOpsPerFrame = settings.MaxLoadOpsPerFrame;
  MaxUnloadOpsPerFrame = settings.MaxUnloadOpsPerFrame;
  if (BlockRegistry)
  {
    Streaming->EnsureStreamer(BlockWorld, *BlockRegistry, WorldSeed,
                              ProceduralTemplate);
  }
  if (rebuildPipeline)
  {
    RebuildWorldGenPipeline();
  }
}

void UWorld::SetWorldGenSets(WorldGenSets sets)
{
  WorldGenSetsData = std::move(sets);
  RebuildResolvedObjectFeatures();
  RebuildWorldGenPipeline();
}

void UWorld::SaveWorldGenSetsToDisk()
{
  if (GetWorldFolderPath().empty())
  {
    return;
  }
  Persistence->SaveWorldData(*this, GetWorldFolderPath() + "/world_data.json");
}

void UWorld::RebuildResolvedObjectFeatures()
{
  if (UObjectFeatureConfigStorage::IsLoaded())
  {
    ResolvedObjectFeatures = ResolveObjectFeatures(
        WorldGenSetsData, UObjectFeatureConfigStorage::Get());
  }
}

void UWorld::RebuildWorldGenPipeline()
{
  if (!BlockRegistry)
  {
    WorldGen.reset();
    return;
  }
  RebuildResolvedObjectFeatures();
  WorldGenContext ctx{BlockWorld, *BlockRegistry, ProceduralTemplate,
                      ObjectLibrary};
  ctx.WorldgenOwnerPackId = WorldgenOwnerPackId;
  if (!ResolvedObjectFeatures.Vegetation.empty() ||
      !ResolvedObjectFeatures.GroundCover.empty() ||
      !ResolvedObjectFeatures.Decoration.empty() ||
      !ResolvedObjectFeatures.Structures.empty())
  {
    ctx.ObjectFeatures = &ResolvedObjectFeatures;
  }
  ctx.OnColumnMeshDirty = [this](int world_x, int world_z, int min_y, int max_y)
  {
    if (CooperativeBulkGenerating)
    {
      return;
    }
    if (!LightingRelightDeferred)
    {
      RelightTerrainColumn(world_x, world_z, min_y, max_y);
    }
    MeshService->MarkColumnMeshDirty(world_x, world_z, min_y, max_y);
  };
  WorldGen = UProceduralWorldGenFactory::Create(ctx);
}

void UWorld::SetRenderDistanceChunks(int distance)
{
  RenderDistanceChunks = distance;
  EffectiveRenderDistance = distance;
  if (Streaming->HasStreamer())
  {
    Streaming->SetRenderDistance(distance);
  }
  MeshService->SetRenderDistanceChunks(distance);
}

void UWorld::SetChunkWriteFormat(ChunkWriteFormat format)
{
  Persistence->SetChunkWriteFormat(format);
}

ChunkWriteFormat UWorld::GetChunkWriteFormat() const
{
  return Persistence->GetChunkWriteFormat();
}

void UWorld::InitStreamerCallbacks()
{
  Streaming->InitStreamerCallbacks(*this);
}

void UWorld::TickAsyncChunkSystems()
{
  if (CoopSession && CoopSession->Active && CoopSession->BlocksStreamingTick())
  {
    return;
  }
  const int pending_bg =
      Persistence ? Persistence->GetPendingTerrainColumnRelightCount() : 0;
  const int pending_player =
      Persistence ? Persistence->GetPendingPlayerRelightCount() : 0;
  const int drain_budget_base = std::clamp(
      4 + (pending_bg + GetAsyncRelightInFlightCount()) / 4, 4, 16);
  const glm::ivec3 focus_ground =
      UChunkManager::WorldToChunk(GetPreferredLoadFocusBlock());
  const int focus_radius = GetStreamingFocusRadius();
  const bool near_pending_light =
      HasPendingLightBeforeMeshNear(glm::ivec3(focus_ground.x, 0, focus_ground.z),
                                    focus_radius);
  const bool underfeet_pending_light =
      HasPendingLightBeforeMeshNear(glm::ivec3(focus_ground.x, 0, focus_ground.z),
                                    /*radius=*/1);
  // Near awaiting first light: apply results faster so mesh gate clears.
  // Underfeet: even more aggressive — empty feet at high FPS was relight_drain≈0.
  // Apply-result budget (cheap) — not Capture count. Keep moderate so MarkRelit
  // cannot flood Dirty in one frame after a Capture hitch (manual 220018).
  int drain_budget =
      near_pending_light ? std::max(drain_budget_base, 8) : drain_budget_base;
  if (underfeet_pending_light)
  {
    drain_budget = std::max(drain_budget, 12);
  }
  const int pending_light_focus_n = CountPendingLightBeforeMeshNear(
      glm::ivec3(focus_ground.x, 0, focus_ground.z), focus_radius);
  if (pending_light_focus_n > 30)
  {
    drain_budget = std::max(drain_budget, 16);
  }
  else if (pending_light_focus_n > 15)
  {
    drain_budget = std::max(drain_budget, 12);
  }
  else if (pending_light_focus_n > 8)
  {
    drain_budget = std::max(drain_budget, 10);
  }
  else if (pending_light_focus_n > 0)
  {
    drain_budget = std::max(drain_budget, 8);
  }
  // Player edits and near first-light columns remesh immediately.
  const bool priority_mesh =
      pending_player > 0 || near_pending_light || underfeet_pending_light ||
      (MeshService && MeshService->GetDirtyCount() < 48);
  const auto relight_t0 = std::chrono::high_resolution_clock::now();
  const int applied =
      DrainAsyncRelightResults(drain_budget, priority_mesh, true);
  PhysicsTelemetryData.RelightDrainMs +=
      std::chrono::duration<double, std::milli>(
          std::chrono::high_resolution_clock::now() - relight_t0)
          .count();
  if (applied > 0)
  {
    const double dt = WallFrameDeltaSec > 0.0 ? WallFrameDeltaSec : (1.0 / 60.0);
    PhysicsTelemetryData.RelightCompletedPerSec = applied / dt;
  }
  else
  {
    PhysicsTelemetryData.RelightCompletedPerSec = 0.0;
  }
  Streaming->TickAsyncChunkSystems(*this);
}

void UWorld::TickMeshEmerge()
{
  if (!BlockRegistry)
  {
    return;
  }
  Streaming->TickMeshEmerge(*this);
  TickPlayerRelightMeshBurst();
}

void UWorld::RefreshStreamerSettings()
{
  Streaming->RefreshStreamerSettings(ProceduralTemplate, MaxLoadOpsPerFrame,
                                     MaxUnloadOpsPerFrame);
}

void UWorld::SetStreamingEnabled(bool enabled)
{
  Streaming->SetStreamingEnabled(enabled);
}

bool UWorld::IsStreamingEnabled() const
{
  return Streaming->IsStreamingEnabled();
}

void UWorld::LoadInitialStreamingChunks()
{
  Persistence->LoadInitialTerrainColumns(*this, SpawnPoint,
                                         RenderDistanceChunks);
  CachedBlockCount = BlockWorld.CountNonAir();
  BlockWorld.GetChunkManager().ForEachChunk(
      [this](const UChunk &chunk)
      { MeshService->MarkDirty(chunk.GetCoord()); });
}

void UWorld::GenerateWorldBlocks()
{
  if (!BlockRegistry)
  {
    return;
  }
  if (HasPersistedSave || LoadedFromChunkSave)
  {
    std::cerr << "GenerateWorldBlocks: skipped (persisted world on disk)"
              << std::endl;
    return;
  }
  if (!WorldGen)
  {
    RebuildWorldGenPipeline();
  }
  if (!WorldGen)
  {
    return;
  }

  const int patchRadiusBlocks = std::max(1, RenderDistanceChunks) * CHUNK_SIZE;
  if (IsStreamingEnabled())
  {
    WorldGen->GenerateSpawnPatch(0, 0, patchRadiusBlocks);
  }
  else
  {
    WorldGen->GenerateFullPatch(0, 0, patchRadiusBlocks);
  }
  if (BlockRegistry)
  {
    SpawnPoint =
        WorldGen->ResolvePlayerSpawnPosition(BlockWorld, *BlockRegistry);
  }
  else
  {
    SpawnPoint = WorldGen->DefaultSpawnPosition(0, 0);
  }

  RebuildBlockMesh();
}

void UWorld::SetBlockDefinitionStorage(
    std::shared_ptr<UBlockDefinitionStorage> definitions)
{
  BlockDefinitions = std::move(definitions);
  if (BlockRegistry)
  {
    BlockRegistry->SetDefinitions(BlockDefinitions);
  }
  else if (TextureCubeInstance)
  {
    BlockRegistry =
        std::make_unique<UBlockRegistry>(TextureCubeInstance, BlockDefinitions);
    if (BlockMergeRegistry)
    {
      BlockRegistry->SetMergeRegistry(BlockMergeRegistry);
    }
    Collision.SetBlockRegistry(BlockRegistry.get());
  }
}

void UWorld::SetBlockMergeRegistry(
    std::shared_ptr<UBlockMergeRegistry> merge_registry)
{
  BlockMergeRegistry = std::move(merge_registry);
  if (BlockRegistry)
  {
    BlockRegistry->SetMergeRegistry(BlockMergeRegistry);
  }
}

void UWorld::OnBlockRegistryChanged()
{
  RefreshBlockRegistry();
  RebuildWorldGenPipeline();
  RebuildBlockMesh();
  if (OnBlockRegistryChangedCallback)
  {
    OnBlockRegistryChangedCallback();
  }
}

void UWorld::OnBlockRegistryRuntimeOverlayChanged(
    const RuntimeOverlayFlushResult *flush)
{
  RefreshBlockRegistry();
  RebuildWorldGenPipeline();
  std::vector<BlockId> affected_ids;
  if (flush)
  {
    affected_ids = flush->RemovedBlockIds;
    affected_ids.reserve(affected_ids.size() +
                         flush->PatchedDescriptors.size());
    for (const MergedCubeDesc &desc : flush->PatchedDescriptors)
    {
      affected_ids.push_back(desc.Id);
    }
  }
  WorldMeshDirtyPolicy::MarkRuntimeOverlayMeshDirty(*this, BlockWorld,
                                                    *MeshService, affected_ids);
  if (OnBlockRegistryChangedCallback)
  {
    OnBlockRegistryChangedCallback();
  }
}

void UWorld::OnCreatureCatalogChanged()
{
  ReloadAllCreatureVisuals();
  if (OnCreatureCatalogChangedCallback)
  {
    OnCreatureCatalogChangedCallback();
  }
}

void UWorld::ReloadAllCreatureVisuals()
{
  Environment.ReloadAllCreatureVisuals();
}

void UWorld::WaitForPendingMeshJobs() { MeshService->WaitForAsyncMeshIdle(); }

void UWorld::WaitForPendingRelightJobs()
{
  if (AsyncRelight)
  {
    AsyncRelight->WaitIdle();
  }
}

bool UWorld::WaitForPendingRelightJobsFor(
    const std::chrono::milliseconds timeout)
{
  if (!AsyncRelight)
  {
    return true;
  }
  if (timeout.count() <= 0)
  {
    // Cancel-only / zero-budget: never block unboundedly on WaitIdle().
    return !AsyncRelight->HasPendingWork();
  }
  return AsyncRelight->WaitIdleFor(timeout);
}

void UWorld::CancelAsyncRelightWork()
{
  if (!AsyncRelight)
  {
    return;
  }
  AsyncRelight->CancelPending();
  AsyncRelightColumnsInFlight.clear();
  // During shutdown / quiesce Persistence may clear queues next; still re-queue
  // discarded sources when Persistence is alive so mid-game cancels recover.
  if (Persistence && !ShutdownPrepared)
  {
    for (const glm::ivec3 &pos : AsyncRelight->TakeDiscardedSourcePositions())
    {
      Persistence->EnqueueTerrainColumnRelight(pos.x, pos.z);
    }
  }
}

void UWorld::QuiesceBackgroundWork(const std::chrono::milliseconds async_timeout)
{
  if (BackgroundQuiesceFinished)
  {
    return;
  }
  AllowProceduralFill = false;
  if (Streaming)
  {
    Streaming->QuiesceBackgroundWork(*this, async_timeout);
  }
  CancelAsyncRelightWork();
  if (MeshService)
  {
    MeshService->CancelAsyncInFlightKeepDirty();
  }
  (void)WaitForPendingRelightJobsFor(async_timeout);
  if (MeshService)
  {
    (void)MeshService->WaitForAsyncMeshIdleFor(async_timeout);
  }
  BackgroundQuiesceFinished = true;
}

void UWorld::PrepareForShutdown()
{
  PrepareForShutdownWithBudgets(std::chrono::milliseconds(2000),
                                std::chrono::milliseconds(150),
                                std::chrono::milliseconds(50));
}

void UWorld::PrepareForShutdownFast()
{
  // Harness / flight-sim: skip Quiesce waits entirely — cancel + abandon only.
  if (ShutdownPrepared)
  {
    return;
  }
  ShutdownPrepared = true;
  if (CoopSession && CoopSession->Active)
  {
    CoopSession->Cancel();
  }
  AllowProceduralFill = false;
  if (Streaming && Streaming->HasStreamer())
  {
    Streaming->GetStreamer()->SetEnabled(false);
  }
  if (BlockPhysicsService)
  {
    BlockPhysicsService->ClearFluidQueue();
  }
  CancelAsyncRelightWork();
  if (Streaming)
  {
    Streaming->AbandonWorkersForProcessExit(std::chrono::milliseconds(0));
  }
  if (MeshService)
  {
    MeshService->CancelAsyncMeshWork();
    (void)MeshService->WaitForAsyncMeshIdleFor(std::chrono::milliseconds(50));
    MeshService->CancelAsyncMeshWork();
  }
  BackgroundQuiesceFinished = true;
  std::cerr << "[Shutdown] PrepareForShutdownFast: done (no join waits)"
            << std::endl;
}

void UWorld::PrepareForShutdownWithBudgets(
    std::chrono::milliseconds quiesce_budget,
    std::chrono::milliseconds abandon_budget,
    std::chrono::milliseconds mesh_idle_budget)
{
  if (ShutdownPrepared)
  {
    return;
  }
  ShutdownPrepared = true;
  if (CoopSession && CoopSession->Active)
  {
    CoopSession->Cancel();
  }

  using clock = std::chrono::high_resolution_clock;
  const auto t0 = clock::now();
  auto phase_ms = [&](const char *phase)
  {
    const double ms =
        std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    std::cerr << "[Shutdown] phase=" << phase << " elapsed_ms=" << ms
              << std::endl;
  };

  // Quit path already ran budgeted TickBackgroundQuiesce; do not reset the
  // latch and re-enter 10s WaitIdle (hangs after "World saved.").
  if (!BackgroundQuiesceFinished)
  {
    QuiesceBackgroundWork(quiesce_budget);
    phase_ms("quiesce");
  }
  else
  {
    AllowProceduralFill = false;
    if (Streaming && Streaming->HasStreamer())
    {
      Streaming->GetStreamer()->SetEnabled(false);
    }
    phase_ms("quiesce_skip");
  }

  CancelAsyncRelightWork();
  phase_ms("cancel_relight");
  if (BlockPhysicsService)
  {
    BlockPhysicsService->ClearFluidQueue();
    phase_ms("clear_fluid_queue");
  }
  if (Streaming)
  {
    // Abandon before mesh WaitIdle so long populate cannot hang shutdown.
    Streaming->AbandonWorkersForProcessExit(abandon_budget);
    phase_ms("abandon_workers");
  }
  if (MeshService)
  {
    MeshService->CancelAsyncMeshWork();
    if (mesh_idle_budget.count() > 0)
    {
      (void)MeshService->WaitForAsyncMeshIdleFor(mesh_idle_budget);
    }
    // Workers may Push Completed after Cancel epoch bump — drop residual RAM.
    MeshService->CancelAsyncMeshWork();
  }
  phase_ms("mesh_idle");
  const double total_ms =
      std::chrono::duration<double, std::milli>(clock::now() - t0).count();
  std::cerr << "[Shutdown] PrepareForShutdown: done elapsed_ms=" << total_ms
            << std::endl;
}

void UWorld::RefreshBlockRegistry()
{
  // Drain in-flight mesh/relight readers before swapping catalogs. Do not call
  // QuiesceBackgroundWork here: it permanently disables the streamer and sets
  // BackgroundQuiesceFinished, which breaks create/load cooperative paths.
  CancelAsyncRelightWork();
  if (MeshService)
  {
    MeshService->CancelAsyncInFlightKeepDirty();
    const auto mesh_wait = BackgroundQuiesceFinished
                               ? std::chrono::milliseconds(50)
                               : std::chrono::milliseconds(1000);
    (void)MeshService->WaitForAsyncMeshIdleFor(mesh_wait);
  }
  if (Streaming)
  {
    // Save/quit paths already quiesce workers before RefreshBlockRegistry().
    // Avoid re-entering a long wait here, which can look like a hang after
    // "World saved." or during save init.
    if (BackgroundQuiesceFinished)
    {
      Streaming->CancelChunkGeneration();
    }
    else
    {
      Streaming->PauseChunkGeneration(std::chrono::milliseconds(10000));
    }
  }
  if (BlockRegistry)
  {
    if (BlockMergeRegistry)
    {
      BlockRegistry->SetMergeRegistry(BlockMergeRegistry);
      if (BlockDefinitions)
      {
        BlockMergeRegistry->PopulateBlockDefinitionStorage(*BlockDefinitions);
      }
    }
    if (BlockDefinitions)
    {
      BlockRegistry->SetDefinitions(BlockDefinitions);
      BlockWorld.SetFluidDefinitions(BlockDefinitions.get());
    }
    BlockRegistry->Reload();
  }
}

void UWorld::RebuildBlockMesh()
{
  if (!BlockRegistry)
  {
    return;
  }
  MeshService->RebuildAll(BlockWorld, *BlockRegistry);
  CachedBlockCount = BlockWorld.CountNonAir();
  BlockWorldReady = CachedBlockCount > 0;
}

void UWorld::AbandonTerrainForWorldReplace()
{
  if (HasActiveCooperativeOperation())
  {
    CancelCooperativeOperation();
  }

  if (Streaming)
  {
    Streaming->CancelChunkGeneration();
    Streaming->AbandonWorkersForProcessExit(std::chrono::milliseconds(150));
  }
  if (Persistence)
  {
    (void)Persistence->AbortAsyncChunkIoFor(std::chrono::milliseconds(0));
  }

  if (MeshService)
  {
    MeshService->CancelAsyncMeshWork();
    (void)MeshService->WaitForAsyncMeshIdleFor(std::chrono::milliseconds(2000));
  }
  CancelAsyncRelightWork();

  BlockWorld.Clear();
  if (MeshService)
  {
    MeshService->GetCache().MarkAllDirty();
  }
  ModifiedChunks.clear();
  BlockWorldReady = false;
  CachedBlockCount = 0;
}

bool UWorld::IsReasonablePlayerPosition(const glm::vec3 &position) const
{
  if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
      !std::isfinite(position.z))
  {
    return false;
  }
  if (position.y <= kMinReasonablePlayerY || position.y > kMaxReasonablePlayerY)
  {
    return false;
  }
  if (std::abs(position.x) > 100000.0f || std::abs(position.z) > 100000.0f)
  {
    return false;
  }
  return true;
}

void UWorld::SanitizeUserPosition(const std::shared_ptr<UUser> &user)
{
  if (!user)
  {
    return;
  }
  if (!IsReasonablePlayerPosition(user->GetPosition()))
  {
    user->SetPosition(SpawnPoint);
    user->SetCameraOrientation(-90.0f, 0.0f);
  }
  if (std::abs(user->GetCameraPitch()) > 89.0f)
  {
    user->SetCameraOrientation(user->GetCameraYaw(), 0.0f);
  }
}

std::optional<int> UWorld::FindHighestSolidY(int x, int z) const
{
  return Collision.FindHighestSolidY(x, z);
}

std::optional<float> UWorld::QueryGroundFeetYColumn(int worldX,
                                                    int worldZ) const
{
  return Collision.QueryGroundFeetYColumn(worldX, worldZ);
}

std::optional<float> UWorld::QueryGroundFeetYUnder(int worldX, int worldZ,
                                                   float referenceFeetY) const
{
  return Collision.QueryGroundFeetYUnder(worldX, worldZ, referenceFeetY);
}

bool UWorld::IsValidStandCell(const glm::ivec3 &cell,
                              const PlayerCapsule &cap) const
{
  return Collision.IsValidStandCell(cell, cap);
}

bool UWorld::IsValidStandFootprint(const glm::vec3 &eyePos,
                                   const PlayerCapsule &cap, float feetY) const
{
  return Collision.IsValidStandFootprint(eyePos, cap, feetY);
}

void UWorld::WarmupSpawnAreaForEnterGame()
{
  Streaming->WarmupSpawnAreaForEnterGame(*this);
}

void UWorld::PrepareEnterGameSession()
{
  Streaming->PrepareEnterGameSession(*this);
}

namespace
{

int EnterGameMeshRadiusChunks(const UWorld &world)
{
  return std::max(1, world.GetRenderDistanceChunks() + 1);
}

bool HasMissingGreedyMeshesNearFocus(const UWorld &world)
{
  // Do not CountNonAir here: under streamer contention it can stall for minutes.
  const glm::ivec3 center =
      UChunkManager::WorldToChunk(world.GetPreferredLoadFocusBlock());
  const int radius = EnterGameMeshRadiusChunks(world);
  const UWorldMeshService &mesh = world.GetMeshService();
  for (int dx = -radius; dx <= radius; ++dx)
  {
    for (int dz = -radius; dz <= radius; ++dz)
    {
      const glm::ivec3 coord(center.x + dx, 0, center.z + dz);
      if (!world.GetBlockWorld().GetChunkManager().HasChunk(coord))
      {
        continue;
      }
      if (!mesh.HasGreedyMesh(coord))
      {
        return true;
      }
    }
  }
  return false;
}

} // namespace

bool UWorld::DrainEnterGameMeshWarmup(int budget)
{
  if (!BlockWorldReady && CachedBlockCount == 0 &&
      MeshService->GetGreedyCacheSize() == 0)
  {
    return true;
  }
  UWorldMeshService &mesh = *MeshService;
  const glm::ivec3 center =
      UChunkManager::WorldToChunk(GetPreferredLoadFocusBlock());
  const int radius = EnterGameMeshRadiusChunks(*this);
  const bool spawn_meshes_pending =
      mesh.HasDirtyWithinHorizontalRadius(center, radius) ||
      HasMissingGreedyMeshesNearFocus(*this);
  if (!spawn_meshes_pending && !mesh.HasPendingAsyncMeshWork())
  {
    return true;
  }
  if (!BlockRegistry)
  {
    return mesh.GetGreedyCacheSize() > 0;
  }
  // Never MarkAllDirtyFromWorld here: ForEachChunk races with streamer workers
  // and re-dirties the entire load radius every frame (hang / crash).
  if (HasMissingGreedyMeshesNearFocus(*this))
  {
    for (int dx = -radius; dx <= radius; ++dx)
    {
      for (int dz = -radius; dz <= radius; ++dz)
      {
        const glm::ivec3 coord(center.x + dx, 0, center.z + dz);
        if (!BlockWorld.GetChunkManager().HasChunk(coord))
        {
          continue;
        }
        if (!mesh.HasGreedyMesh(coord))
        {
          mesh.MarkDirtyPriority(coord);
        }
      }
    }
  }
  const UChunkEmergeCoordinator::FrameBudget mesh_budget =
      UChunkEmergeCoordinator::CooperativeWarmupBudget(std::max(budget, 16));
  mesh.RebuildDirtyChunks(BlockWorld, *BlockRegistry, mesh_budget.MaxMeshDrain,
                          mesh_budget.MaxMeshSchedule);
  mesh.DrainAsyncMeshResults(BlockWorld, *BlockRegistry,
                             mesh_budget.MaxMeshDrain);
  if (BlockRegistry && mesh.GetPendingGpuAppliesCount() > 0)
  {
    mesh.DrainPendingGpuMeshes(BlockWorld, *BlockRegistry,
                               mesh_budget.MaxMeshDrain,
                               std::max(8.0, static_cast<double>(budget)));
  }
  return !HasMissingGreedyMeshesNearFocus(*this) &&
         !mesh.HasDirtyWithinHorizontalRadius(center, radius) &&
         !mesh.HasPendingAsyncMeshWork() &&
         mesh.CountPendingGpuAppliesInHorizontalRadius(center, radius) == 0;
}

bool UWorld::IsSpawnMeshRingReady() const
{
  if (!MeshService || !BlockRegistry)
  {
    return false;
  }
  const glm::ivec3 center =
      UChunkManager::WorldToChunk(GetPreferredLoadFocusBlock());
  const int radius = EnterGameMeshRadiusChunks(*this);
  if (HasMissingGreedyMeshesNearFocus(*this))
  {
    return false;
  }
  if (MeshService->HasDirtyWithinHorizontalRadius(center, radius))
  {
    return false;
  }
  if (MeshService->HasPendingAsyncMeshWork())
  {
    return false;
  }
  return MeshService->CountPendingGpuAppliesInHorizontalRadius(center,
                                                               radius) == 0;
}

int UWorld::CountPostLoadRingNotReady() const
{
  const glm::ivec3 focus =
      UChunkManager::WorldToChunk(GetPreferredLoadFocusBlock());
  const glm::ivec3 focus_ground(focus.x, 0, focus.z);
  return CountUnfinishedVisualNear(focus_ground, GetRenderDistanceChunks() + 1);
}

void UWorld::TickEnterGameMeshBurst()
{
  if (EnterGameMeshBurstFrames > 0)
  {
    --EnterGameMeshBurstFrames;
  }
}

void UWorld::BeginEnterGameMeshBurst(int frames)
{
  EnterGameMeshBurstFrames = std::max(EnterGameMeshBurstFrames, std::max(0, frames));
}

void UWorld::SetEnterGameWarmupMissingGreedy(int n)
{
  EnterGameWarmupMissingGreedy = std::max(0, n);
  PhysicsTelemetryData.EnterGameWarmupMissingGreedy = EnterGameWarmupMissingGreedy;
}

bool UWorld::NeedsEnterGameMeshWarmup() const
{
  if (!BlockWorldReady && CachedBlockCount == 0 &&
      MeshService->GetGreedyCacheSize() == 0)
  {
    return false;
  }
  const UWorldMeshService &mesh = *MeshService;
  if (mesh.HasPendingAsyncMeshWork())
  {
    return true;
  }
  const glm::ivec3 center =
      UChunkManager::WorldToChunk(GetPreferredLoadFocusBlock());
  const int radius = EnterGameMeshRadiusChunks(*this);
  if (mesh.HasDirtyWithinHorizontalRadius(center, radius))
  {
    return true;
  }
  if (HasMissingGreedyMeshesNearFocus(*this))
  {
    return true;
  }
  // TD-ARCH-021/D2b: Visual RD spawn barrier — post-load ring must be draw-ready.
  if (CountPostLoadRingNotReady() > 0)
  {
    return true;
  }
  return mesh.CountPendingGpuAppliesInHorizontalRadius(center, radius) > 0;
}

bool UWorld::IsCreateSpawnWarmupSettled() const
{
  if (!IsStreamingEnabled() || !Streaming || !Streaming->HasStreamer())
  {
    if (GetBlockWorld().CountNonAir() == 0)
    {
      return true;
    }
    const glm::ivec3 feet = GetPreferredLoadFocusBlock();
    const glm::ivec3 center = UChunkManager::WorldToChunk(feet);
    const int radius = RenderDistanceChunks + 1;
    const UWorldMeshService &mesh = *MeshService;
    if (mesh.HasDirtyWithinHorizontalRadius(center, radius))
    {
      return false;
    }
    return !mesh.HasPendingAsyncMeshWork();
  }
  return IsEnterStreamingWarmupSettled();
}

void UWorld::DrainSpawnRadiusMeshWarmup(int budget)
{
  if (GetBlockWorld().CountNonAir() == 0 || !BlockRegistry)
  {
    return;
  }
  UWorldMeshService &mesh = *MeshService;
  const UChunkEmergeCoordinator::FrameBudget mesh_budget =
      UChunkEmergeCoordinator::CooperativeWarmupBudget(std::max(budget, 16));
  mesh.RebuildDirtyChunks(BlockWorld, *BlockRegistry, mesh_budget.MaxMeshDrain,
                          mesh_budget.MaxMeshSchedule);
  mesh.DrainAsyncMeshResults(BlockWorld, *BlockRegistry,
                             mesh_budget.MaxMeshDrain);
}

void UWorld::RefreshPersistedTerrainAfterSave()
{
  HasPersistedSave = true;
  LoadedFromChunkSave = true;
  EnsureStreamingActiveAfterBackgroundQuiesce();
  if (Streaming && Streaming->HasStreamer())
  {
    Streaming->MarkPersistedColumnsFromWorld();
  }
}

void UWorld::EnsureStreamingActiveAfterBackgroundQuiesce()
{
  AllowProceduralFill = IsStreamingEnabled();
  if (Streaming)
  {
    Streaming->ResumeStreamerAfterQuiesce();
    InitStreamerCallbacks();
  }
}

void UWorld::ResumeAfterSessionSave()
{
  BackgroundQuiesceFinished = false;
  EnsureStreamingActiveAfterBackgroundQuiesce();
}

bool UWorld::IsEnterStreamingWarmupSettled() const
{
  if (!IsStreamingEnabled() || !Streaming || !Streaming->HasStreamer())
  {
    return true;
  }
  // Cooperative load already filled the spawn patch; waiting for the live
  // streamer to finish an expanding focus ring stalls EnterGame (and used to
  // remesh the whole world each frame).
  if (SpawnAreaPreparedByCooperativeLoad)
  {
    return true;
  }
  const glm::ivec3 feet = GetPreferredLoadFocusBlock();
  if (!IsCollisionReadyAtFeet(feet))
  {
    return false;
  }
  if (MeshService->HasPendingAsyncMeshWork())
  {
    return false;
  }
  const glm::ivec3 center = UChunkManager::WorldToChunk(feet);
  const int radius = EnterGameMeshRadiusChunks(*this);
  if (MeshService->HasDirtyWithinHorizontalRadius(center, radius))
  {
    return false;
  }
  if (Persistence &&
      (Persistence->GetPendingPlayerRelightCount() > 0 ||
       Persistence->GetPendingTerrainColumnRelightCount() > 0))
  {
    return false;
  }
  const int max_y = GetProceduralSettings().MaxHeight;
  for (int dx = -radius; dx <= radius; ++dx)
  {
    for (int dz = -radius; dz <= radius; ++dz)
    {
      const glm::ivec3 coord(center.x + dx, 0, center.z + dz);
      if (!IsTerrainChunkComplete(BlockWorld, coord, max_y))
      {
        return false;
      }
    }
  }
  return true;
}

void UWorld::TickEnterStreamingWarmup(int iteration_budget)
{
  if (!IsStreamingEnabled() || !Streaming->HasStreamer())
  {
    return;
  }
  const int iterations = std::max(1, iteration_budget);
  for (int i = 0; i < iterations; ++i)
  {
    UpdateStreaming();
    TickAsyncChunkSystems();
    TickMeshEmerge();
  }
}

void UWorld::MarkSpawnAreaPreparedByCooperativeLoad()
{
  SpawnAreaPreparedByCooperativeLoad = true;
}

bool UWorld::ConsumeSpawnAreaPreparedByCooperativeLoad()
{
  const bool prepared = SpawnAreaPreparedByCooperativeLoad;
  SpawnAreaPreparedByCooperativeLoad = false;
  return prepared;
}

void UWorld::ClearSpawnAreaPreparedByCooperativeLoad()
{
  SpawnAreaPreparedByCooperativeLoad = false;
}

void UWorld::ResetPhysicsRuntimeState()
{
  if (BlockPhysicsService)
  {
    BlockPhysicsService->ResetRuntimeState();
  }
  if (ChunkDirtyService)
  {
    ChunkDirtyService->ClearPendingQueues();
  }
}

void UWorld::FinalizePlayerAfterWorldLoad()
{
  ResetPhysicsRuntimeState();
  ResetMeshLoadDiagnostics();
  BlockCounter.MarkNeedsRecount();
  bool has_terrain_chunks = false;
  BlockWorld.GetChunkManager().ForEachChunk([&](const UChunk &)
                                            { has_terrain_chunks = true; });
  BlockWorldReady = has_terrain_chunks || CachedBlockCount > 0;
  PhysicsSuspendFrames = 3;

  if (CurrentUserName.empty() && !Users.empty())
  {
    SetCurrentUserName(Users.begin()->first);
  }
  if (Environment.GetControlledCreatureId() == 0)
  {
    if (Environment.GetPlayerCreatureId() != 0)
    {
      SetControlledCreature(Environment.GetPlayerCreatureId());
    }
    else if (auto user = GetCurrentUser())
    {
      if (user->GetPlayerCreatureId() != 0)
      {
        Environment.SetPlayerCreatureId(user->GetPlayerCreatureId());
        SetControlledCreature(Environment.GetPlayerCreatureId());
      }
    }
  }

  if (auto user = GetCurrentUser())
  {
    SanitizeUserPosition(user);
    if (BlockWorldReady)
    {
      EnsurePlayerOnGround();
    }
    else
    {
      ApplySpawnToCamera();
    }
  }
  else
  {
    ApplySpawnToCamera();
  }

  if (ViewBinding)
  {
    ViewBinding->ResetCurrentCameraVerticalPhysics(*this);
  }
}

void UWorld::Create(const std::string &world_name)
{
  UNullProgressSink sink;
  BeginCooperativeCreate(world_name);
  while (!TickCooperativeCreate(sink, 64))
  {
  }
}

void UWorld::ApplyGameModeLocomotionPolicy()
{
  ForEachCreature(
      [this](UCreature &creature)
      {
        const CreatureDefinition *def = GetCreatureDefinition(creature.GetTypeId());
        if (!def)
        {
          return;
        }
        CreatureLocomotionCapabilities caps = def->locomotion;
        if (!ModePolicy::AllowsFlight(GameMode, def->habitat))
        {
          caps.canFly = false;
          if (creature.GetMovementMode() == CreatureMovementMode::Flying &&
              def->habitat == CreatureHabitat::Terrestrial)
          {
            creature.GetLocomotion().SetMode(CreatureMovementMode::Walking);
          }
        }
        creature.SetCapabilities(caps);
      });
  if (GameMode == WorldGameMode::Survival)
  {
    if (auto camera = GetCurrentUserCamera())
    {
      CreatureHabitat habitat = CreatureHabitat::Terrestrial;
      if (UCreature *controlled = GetControlledCreature())
      {
        if (const CreatureDefinition *def =
                GetCreatureDefinition(controlled->GetTypeId()))
        {
          habitat = def->habitat;
        }
      }
      if (!ModePolicy::AllowsFlight(GameMode, habitat))
      {
        camera->SetFreeMove(false);
      }
    }
  }
}

void UWorld::Load(const std::string &world_folder_path)
{
  UNullProgressSink sink;
  BeginCooperativeLoad(world_folder_path);
  while (!TickCooperativeLoad(sink, 64))
  {
  }
}

void UWorld::Save(const std::string &world_folder_path)
{
  UNullProgressSink sink;
  BeginCooperativeSave(world_folder_path);
  while (!TickCooperativeSave(sink, 64))
  {
  }
}

void UWorld::SaveSessionSnapshot(const std::string &world_folder_path,
                                 const bool skip_quiesce)
{
  if (world_folder_path.empty())
  {
    return;
  }
  if (!skip_quiesce)
  {
    if (CoopSession && CoopSession->Active)
    {
      CoopSession->Cancel();
    }
    QuiesceBackgroundWork(std::chrono::milliseconds(2000));
  }
  RefreshBlockRegistry();
  std::filesystem::create_directories(world_folder_path);
  std::filesystem::create_directories(world_folder_path + "/chunks");
  SetWorldFolderPath(world_folder_path);

  if (BlockRegistry)
  {
    std::unordered_set<glm::ivec3, IVec3Hash> grounds;
    BlockWorld.GetChunkManager().ForEachChunk(
        [&](const UChunk &chunk)
        {
          const glm::ivec3 coord = chunk.GetCoord();
          grounds.insert(glm::ivec3(coord.x, 0, coord.z));
        });
    for (const glm::ivec3 &modified : ModifiedChunks)
    {
      grounds.insert(glm::ivec3(modified.x, 0, modified.z));
    }
    for (const glm::ivec3 &ground : grounds)
    {
      if (IsTerrainChunkComplete(BlockWorld, ground, ProceduralTemplate.MaxHeight))
      {
        GetChunkStorage().SaveTerrainColumn(
            ground, BlockWorld, world_folder_path, *BlockRegistry,
            ProceduralTemplate.MaxHeight);
      }
      else
      {
        GetChunkStorage().RemoveTerrainColumnFromDisk(
            world_folder_path, ground, ProceduralTemplate.MaxHeight);
      }
    }
  }

  GetChunkStorage().WriteStorageMarker(world_folder_path);
  SaveUsers(world_folder_path + "/users.json");
  SaveCreatures(world_folder_path + "/creatures.json");
  SaveWorldData(world_folder_path + "/world_data.json");
  ModifiedChunks.clear();
  // Quit path must not recreate the chunk scheduler (join while populate
  // finishes). Autosave / menu save still resume streaming.
  if (!BackgroundQuiesceFinished)
  {
    EnsureStreamingActiveAfterBackgroundQuiesce();
  }
}

void UWorld::PersistWorldMetadata()
{
  const std::string &folder = GetWorldFolderPath();
  if (folder.empty())
  {
    return;
  }
  SaveWorldData(folder + "/world_data.json");
}

void UWorld::BeginCooperativeLoad(const std::string &world_folder_path)
{
  if (!CoopSession)
  {
    CoopSession = std::make_unique<UWorldCooperativeSession>();
  }
  CoopSession->BeginLoad(*this, world_folder_path);
}

bool UWorld::TickCooperativeLoad(IUProgressSink &sink, int chunkBudget)
{
  if (!CoopSession)
  {
    return true;
  }
  return CoopSession->Tick(*this, sink, chunkBudget);
}

void UWorld::BeginCooperativeSave(const std::string &world_folder_path,
                                  bool resume_streaming_after_save)
{
  if (!CoopSession)
  {
    CoopSession = std::make_unique<UWorldCooperativeSession>();
  }
  CoopSession->BeginSave(*this, world_folder_path, resume_streaming_after_save);
}

bool UWorld::TickCooperativeSave(IUProgressSink &sink, int chunkBudget)
{
  if (!CoopSession)
  {
    return true;
  }
  return CoopSession->Tick(*this, sink, chunkBudget);
}

void UWorld::BeginCooperativeCreate(const std::string &world_name)
{
  if (!CoopSession)
  {
    CoopSession = std::make_unique<UWorldCooperativeSession>();
  }
  CoopSession->BeginCreate(*this, world_name);
}

bool UWorld::TickCooperativeCreate(IUProgressSink &sink, int columnBudget)
{
  if (!CoopSession)
  {
    return true;
  }
  return CoopSession->Tick(*this, sink, columnBudget);
}

bool UWorld::HasActiveCooperativeOperation() const
{
  return CoopSession && CoopSession->Active;
}

void UWorld::CancelCooperativeOperation()
{
  if (CoopSession && CoopSession->Active)
  {
    CoopSession->Cancel();
  }
}

bool UWorld::BlocksAsyncRelightDrain() const
{
  return CoopSession && CoopSession->Active && CoopSession->BlocksStreamingTick();
}

void UWorld::BeginBackgroundQuiesce(UBackgroundQuiesceState &state)
{
  state = UBackgroundQuiesceState{};
  BackgroundQuiesceFinished = false;
}

bool UWorld::TickBackgroundQuiesce(UBackgroundQuiesceState &state,
                                   const std::chrono::milliseconds step_timeout,
                                   IUProgressSink *sink)
{
  auto report = [&](const char *phase_id, float fraction, const char *message)
  {
    if (sink)
    {
      sink->Report(phase_id, fraction, message);
    }
  };

  switch (state.phase)
  {
  case UBackgroundQuiesceState::Phase::Start:
    AllowProceduralFill = false;
    if (CoopSession)
    {
      CoopSession->CancelBackgroundWorkers();
    }
    report("start", 0.05f, "Stopping background work...");
    state.phase = UBackgroundQuiesceState::Phase::StreamingOff;
    return false;

  case UBackgroundQuiesceState::Phase::StreamingOff:
    AllowProceduralFill = false;
    if (Streaming && Streaming->HasStreamer())
    {
      Streaming->GetStreamer()->SetEnabled(false);
    }
    if (Streaming)
    {
      // Cancel queued gen immediately; active workers drain in ChunkGenCancel.
      Streaming->PauseChunkGeneration(std::chrono::milliseconds(0));
    }
    report("streaming", 0.15f, "Stopping streaming...");
    state.phase = UBackgroundQuiesceState::Phase::ChunkGenCancel;
    return false;

  case UBackgroundQuiesceState::Phase::ChunkGenCancel:
  {
    bool idle = true;
    if (Streaming && Streaming->GetChunkScheduler())
    {
      // First passes: brief wait. Later: cancel-only so long carve cannot pin
      // the quit path for the full kMaxWaitPasses budget.
      if (state.waitChunkGenPasses < 8)
      {
        Streaming->PauseChunkGeneration(step_timeout);
        idle = Streaming->GetChunkScheduler()->GetGenBacklogTotal() == 0 &&
               Streaming->GetChunkScheduler()->WaitForWorkersIdle(step_timeout);
      }
      else
      {
        Streaming->CancelChunkGeneration();
        idle = Streaming->GetChunkScheduler()->GetGenInFlightCount() == 0;
      }
    }
    const float frac =
        0.15f + 0.15f * static_cast<float>(state.waitChunkGenPasses) /
                    static_cast<float>(UBackgroundQuiesceState::kMaxWaitPasses);
    report("chunk_gen", std::min(0.3f, frac), "Waiting for chunk workers...");
    if (idle || ++state.waitChunkGenPasses >=
                    UBackgroundQuiesceState::kMaxWaitPasses)
    {
      state.phase = UBackgroundQuiesceState::Phase::DrainIo;
    }
    return false;
  }

  case UBackgroundQuiesceState::Phase::DrainIo:
  {
    bool drained = true;
    if (Persistence)
    {
      drained = Persistence->TickDrainAsyncChunkIo(*this, 8);
      if (!drained)
      {
        drained = Persistence->AbortAsyncChunkIoFor(step_timeout);
      }
    }
    const float frac =
        0.3f + 0.05f * static_cast<float>(state.drainIoPasses) /
                   static_cast<float>(UBackgroundQuiesceState::kMaxDrainIoPasses);
    report("drain_io", std::min(0.35f, frac), "Flushing chunk IO...");
    if (drained || ++state.drainIoPasses >=
                       UBackgroundQuiesceState::kMaxDrainIoPasses)
    {
      if (Persistence && !drained)
      {
        (void)Persistence->AbortAsyncChunkIoFor(step_timeout);
      }
      state.phase = UBackgroundQuiesceState::Phase::CancelWorkers;
    }
    return false;
  }

  case UBackgroundQuiesceState::Phase::CancelWorkers:
    CancelAsyncRelightWork();
    if (MeshService)
    {
      MeshService->CancelAsyncInFlightKeepDirty();
    }
    report("cancel", 0.35f, "Cancelling pending jobs...");
    state.phase = UBackgroundQuiesceState::Phase::WaitRelight;
    return false;

  case UBackgroundQuiesceState::Phase::WaitRelight:
  {
    const bool idle = WaitForPendingRelightJobsFor(step_timeout);
    const float frac =
        0.35f + 0.3f * static_cast<float>(state.waitRelightPasses) /
                    static_cast<float>(UBackgroundQuiesceState::kMaxWaitPasses);
    report("relight", std::min(0.65f, frac), "Waiting for relight jobs...");
    if (idle || ++state.waitRelightPasses >=
                   UBackgroundQuiesceState::kMaxWaitPasses)
    {
      state.phase = UBackgroundQuiesceState::Phase::WaitMesh;
    }
    return false;
  }

  case UBackgroundQuiesceState::Phase::WaitMesh:
  {
    const bool idle =
        MeshService ? MeshService->WaitForAsyncMeshIdleFor(step_timeout) : true;
    const float frac =
        0.65f + 0.25f * static_cast<float>(state.waitMeshPasses) /
                    static_cast<float>(UBackgroundQuiesceState::kMaxWaitPasses);
    report("mesh", std::min(0.9f, frac), "Waiting for mesh jobs...");
    if (idle || ++state.waitMeshPasses >= UBackgroundQuiesceState::kMaxWaitPasses)
    {
      state.phase = UBackgroundQuiesceState::Phase::Finalize;
    }
    return false;
  }

  case UBackgroundQuiesceState::Phase::Finalize:
    if (MeshService)
    {
      MeshService->CancelAsyncMeshWork();
      (void)MeshService->WaitForAsyncMeshIdleFor(std::chrono::milliseconds(100));
    }
    if (Persistence)
    {
      (void)Persistence->AbortAsyncChunkIoFor(std::chrono::milliseconds(0));
    }
    BackgroundQuiesceFinished = true;
    report("done", 1.f, "Background work stopped.");
    state.phase = UBackgroundQuiesceState::Phase::Done;
    return true;

  case UBackgroundQuiesceState::Phase::Done:
    return true;
  }
  return true;
}

bool UWorld::TryAddFluidObject(glm::ivec3 blockPos, BlockId liquidId)
{
  return UWorldFluidFacade::TryAddFluidObject(*this, blockPos, liquidId);
}

void UWorld::ApplyBreakSiteFluidFlood(
    glm::ivec3 blockPos, std::vector<glm::ivec3> &mesh_touch_blocks)
{
  UWorldFluidFacade::ApplyBreakSiteFluidFlood(*this, blockPos,
                                              mesh_touch_blocks);
}

bool UWorld::AddObject(const std::string type_id, const glm::vec3 &position)
{
  if (!BlockRegistry)
  {
    return false;
  }
  const BlockId Id = BlockRegistry->GetIdByTypeName(type_id);
  if (Id == BLOCK_AIR)
  {
    std::cerr << "World::AddObject: Unknown block type '" << type_id << "'"
              << std::endl;
    return false;
  }
  const glm::ivec3 blockPos = WorldPosToBlock(position);
  const BlockId existing = BlockWorld.GetBlock(blockPos);
  if (BlockRegistry->IsLiquid(Id))
  {
    return TryAddFluidObject(blockPos, Id);
  }
  else if (existing != BLOCK_AIR && !BlockRegistry->IsLiquid(existing))
  {
    if (BlockRegistry->BlocksMovement(existing))
    {
      return false;
    }
  }
  BlockWorld.SetBlock(blockPos, Id);
  if (BlockWorld.GetBlock(blockPos) != Id)
  {
    return false;
  }
  ++CachedBlockCount;
  BlockWorldReady = true;
  ++PhysicsTelemetryData.PlaceCompleteN;
  const int emission =
      BlockRegistry ? BlockRegistry->GetLightEmission(Id) : 0;
  if (emission > 0)
  {
    ++PhysicsTelemetryData.PlaceEmissionN;
  }
  PhysicsTelemetryData.EditLightEmission =
      std::max(PhysicsTelemetryData.EditLightEmission, emission);
  const auto edit_t0 = std::chrono::high_resolution_clock::now();
  ApplyEditFastRelight({blockPos});
  // Face-neighbor Immediate + light ring so side-wall underside air that lost
  // skylight still remeshes neighbors (manual absolute-black under place).
  MarkBlockChunkDirty(blockPos, /*sync_neighbor_chunks=*/true,
                      /*sync_light_ring=*/true);
  PhysicsTelemetryData.EditToFirstMeshMs =
      std::chrono::duration<double, std::milli>(
          std::chrono::high_resolution_clock::now() - edit_t0)
          .count();
  ApplyEditLighting({blockPos});
  PlayerRelightMeshBurstFrames = 5;
  PublishBlockPhysicsEvent(blockPos);
  PublishNeighborPhysicsEvents(blockPos);
  if (BlockRegistry && BlockRegistry->IsLiquid(Id) && PhysicsFlags.EnableFluids)
  {
    EnqueueFluidFrontierAt(*this, blockPos);
    MarkBlockChunkDirty(blockPos);
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      MarkBlockChunkDirty(blockPos + offset);
    }
  }
  return true;
}

bool UWorld::PlaceObject(const std::string &prefab_name,
                         glm::ivec3 anchorWorldPos)
{
  if (!ObjectLibrary || !BlockRegistry)
  {
    return false;
  }
  const WorldObjectDefinition *prefab = ObjectLibrary->Get(prefab_name);
  if (!prefab)
  {
    return false;
  }

  const ObjectPlacementStats stats =
      PlaceObjectAt(BlockWorld, *BlockRegistry, *prefab, anchorWorldPos, true);
  if (stats.placedCount == 0)
  {
    return false;
  }
  std::vector<glm::ivec3> placed_blocks;
  placed_blocks.reserve(prefab->voxels.size());
  int max_emission = 0;
  for (const auto &voxel : prefab->voxels)
  {
    const glm::ivec3 worldPos = anchorWorldPos + voxel.offset - prefab->anchor;
    const BlockId blockId =
        ResolveObjectVoxelPlacementId(voxel, *BlockRegistry);
    if (BlockWorld.GetBlock(worldPos) != blockId)
    {
      continue;
    }
    placed_blocks.push_back(worldPos);
    max_emission =
        std::max(max_emission, BlockRegistry->GetLightEmission(blockId));
  }
  if (!placed_blocks.empty())
  {
    ApplyEditFastRelight(placed_blocks);
    for (const glm::ivec3 &worldPos : placed_blocks)
    {
      MarkBlockChunkDirty(worldPos, /*sync_neighbor_chunks=*/true,
                          /*sync_light_ring=*/max_emission > 0);
    }
    ApplyEditLighting(placed_blocks);
    if (max_emission > 0)
    {
      PhysicsTelemetryData.EditLightEmission =
          std::max(PhysicsTelemetryData.EditLightEmission, max_emission);
      PlayerRelightMeshBurstFrames = 5;
    }
  }
  return true;
}

bool UWorld::CanPlaceObject(const std::string &prefab_name,
                            glm::ivec3 anchorWorldPos) const
{
  if (!ObjectLibrary || !BlockRegistry)
  {
    return false;
  }
  const WorldObjectDefinition *prefab = ObjectLibrary->Get(prefab_name);
  if (!prefab)
  {
    return false;
  }
  return CanPlaceObjectAt(BlockWorld, *prefab, anchorWorldPos);
}

std::optional<glm::ivec3>
UWorld::FindObjectAnchorFromView(const glm::vec3 &position,
                                 const glm::vec3 &front) const
{
  const auto hit =
      RaycastSolidBlocks(BlockWorld, *BlockRegistry, position, front);
  if (!hit)
  {
    return std::nullopt;
  }
  glm::ivec3 normal = hit->faceNormal;
  if (normal == glm::ivec3(0))
  {
    const glm::vec3 toCamera = position - BlockCenter(hit->blockPos);
    if (std::abs(toCamera.x) >= std::abs(toCamera.y) &&
        std::abs(toCamera.x) >= std::abs(toCamera.z))
    {
      normal.x = toCamera.x > 0.0f ? 1 : -1;
    }
    else if (std::abs(toCamera.y) >= std::abs(toCamera.z))
    {
      normal.y = toCamera.y > 0.0f ? 1 : -1;
    }
    else
    {
      normal.z = toCamera.z > 0.0f ? 1 : -1;
    }
  }
  return hit->blockPos + normal;
}

bool UWorld::AddUser(const std::string &Name)
{
  if (Users.find(Name) != Users.end())
    return false;

  if (Name.empty())
    return false;

  Users[Name] = std::make_shared<UUser>();
  auto user = Users[Name];
  const glm::vec3 eyeOffset = ResolveControlledDefaultEyeOffset();
  const glm::vec3 bodyOrigin = BodyOriginFromEye(SpawnPoint, eyeOffset);
  std::string speciesId = "human";
  if (const auto &creature_definitions = GetCreatureDefinitionStorage())
  {
    const std::string controlled =
        creature_definitions->GetControlledDefaultSpeciesId();
    if (!controlled.empty())
    {
      speciesId = controlled;
    }
  }
  const CreatureId pid = SpawnCreature(speciesId, bodyOrigin);
  user->SetPlayerCreatureId(pid);
  if (UPlayer *player = dynamic_cast<UPlayer *>(GetCreature(pid)))
  {
    player->BindUser(user);
    if (!user->GetSelectedSkinId().empty())
    {
      player->SetSkinId(user->GetSelectedSkinId());
      if (const CreatureDefinition *def = GetCreatureDefinition(speciesId))
      {
        player->SetVisual(CreateCreatureVisual(*def));
      }
    }
    UCreatureInventory &inv = player->GetInventory();
    if (ModePolicy::ShouldInitCreativeDefaults(GameMode))
    {
      inv.InitCreativeDefaults();
    }
    inv.EnsureDefaultHotbar();
  }
  if (Users.size() == 1)
  {
    Environment.SetPlayerCreatureId(pid);
    Environment.SetControlledCreatureId(pid);
  }
  if (!ViewBinding)
  {
    return false;
  }
  const size_t viewId = ViewBinding->CreateUserCamera(SpawnPoint);
  user->SetViewId(viewId);
  if (Users.size() == 1)
  {
    SetCurrentUserName(Name);
    ViewBinding->SetActiveCamera(viewId);
  }

  return true;
}

void UWorld::DelUser(const std::string &Name)
{
  if (Users.find(Name) == Users.end())
    return;

  Users.erase(Name);
}

std::shared_ptr<UUser> UWorld::GetUser(const std::string &Name)
{
  auto I = Users.find(Name);
  return (I != Users.end()) ? I->second : nullptr;
}

const std::string &UWorld::GetCurrentUserName() const
{
  return CurrentUserName;
}

std::shared_ptr<UUser> UWorld::GetCurrentUser()
{
  return GetUser(CurrentUserName);
}

std::shared_ptr<UUser> UWorld::GetCurrentUser() const
{
  return const_cast<UWorld *>(this)->GetUser(CurrentUserName);
}

UCreatureInventory *
UWorld::GetPlayerInventory(const std::shared_ptr<UUser> &user)
{
  if (!user || user->GetPlayerCreatureId() == 0)
  {
    return nullptr;
  }
  if (UCreature *creature = GetCreature(user->GetPlayerCreatureId()))
  {
    return &creature->GetInventory();
  }
  return nullptr;
}

const UCreatureInventory *
UWorld::GetPlayerInventory(const std::shared_ptr<UUser> &user) const
{
  return const_cast<UWorld *>(this)->GetPlayerInventory(user);
}

void UWorld::EnsurePlayerHotbarCount(const std::shared_ptr<UUser> &user,
                                     size_t barCount)
{
  if (UCreatureInventory *inv = GetPlayerInventory(user))
  {
    inv->EnsureHotbarCount(barCount);
  }
}

bool UWorld::SetCurrentUserName(const std::string &Name)
{
  if (Users.find(Name) == Users.end())
    return false;
  CurrentUserName = Name;
  if (auto user = GetCurrentUser())
  {
    if (user->GetPlayerCreatureId() != 0)
    {
      Environment.SetPlayerCreatureId(user->GetPlayerCreatureId());
      SetControlledCreature(Environment.GetPlayerCreatureId());
    }
  }
  ApplyUserToCamera(GetCurrentUser());
  if (auto user = GetCurrentUser(); user && ViewBinding)
  {
    ViewBinding->SetActiveCamera(user->GetViewId());
  }
  return true;
}

bool UWorld::CheckRayIntersection(
    const glm::vec3 &position, const glm::vec3 &front,
    std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>>
        &distance_map) const
{
  return Collision.CheckRayIntersection(position, front, distance_map);
}

bool UWorld::CheckRayIntersection(const glm::vec3 &position,
                                  const glm::vec3 &front,
                                  glm::vec3 &intersecion, float &distance,
                                  size_t &cube_index, int &cube_side,
                                  size_t &object_index) const
{
  return Collision.CheckRayIntersection(position, front, intersecion, distance,
                                        cube_index, cube_side, object_index);
}

bool UWorld::CheckPositionFree(const glm::vec3 &position, float size) const
{
  return Collision.CheckPositionFree(position, size);
}

std::optional<glm::vec3>
UWorld::FindNearestFreeCubePosition(const glm::vec3 &position,
                                    const glm::vec3 &front,
                                    const PlayerCapsule &cap) const
{
  return Collision.FindNearestFreeCubePosition(position, front, cap);
}

bool UWorld::AddObjectByView(const glm::vec3 &position, const glm::vec3 &front)
{
  return UBlockPlacementService::AddObjectByView(*this, position, front);
}

bool UWorld::PlaceActiveObjectByView(const glm::vec3 &position,
                                     const glm::vec3 &front)
{
  return UBlockPlacementService::PlaceActiveObjectByView(*this, position,
                                                         front);
}

bool UWorld::DelBlockAt(glm::ivec3 blockPos)
{
  if (!BlockRegistry)
  {
    return false;
  }
  if (BlockWorld.GetBlock(blockPos) == BLOCK_AIR)
  {
    return false;
  }
  const int removed_emission =
      BlockRegistry->GetLightEmission(BlockWorld.GetBlock(blockPos));
  PhysicsTelemetryData.EditLightEmission =
      std::max(PhysicsTelemetryData.EditLightEmission, removed_emission);
  BlockWorld.SetBlock(blockPos, BLOCK_AIR);
  if (CachedBlockCount > 0)
  {
    --CachedBlockCount;
  }
  const std::vector<glm::ivec3> broken_above =
      BreakUnsupportedBlocksAbove(BlockWorld, *BlockRegistry, blockPos);
  std::vector<glm::ivec3> mesh_touch_blocks;
  mesh_touch_blocks.reserve(1 + broken_above.size());
  mesh_touch_blocks.push_back(blockPos);
  mesh_touch_blocks.insert(mesh_touch_blocks.end(), broken_above.begin(),
                           broken_above.end());
  ApplyBreakSiteFluidFlood(blockPos, mesh_touch_blocks);
  const auto edit_t0 = std::chrono::high_resolution_clock::now();
  ApplyEditFastRelight(mesh_touch_blocks);
  // Face-neighbor Immediate so newly exposed sides are not left dark while
  // Dirty neighbors wait behind mesh_async≈42 (manual 230913).
  MarkBlocksChunkDirtyBatch(mesh_touch_blocks, /*sync_neighbor_chunks=*/true,
                            /*sync_light_ring=*/removed_emission > 0,
                            PhysicsTelemetryData.BreakCompleteN > 0);
  PhysicsTelemetryData.EditToFirstMeshMs =
      std::chrono::duration<double, std::milli>(
          std::chrono::high_resolution_clock::now() - edit_t0)
          .count();
  ApplyEditLighting(mesh_touch_blocks);
  PlayerRelightMeshBurstFrames = 5;
  PublishBlockPhysicsEvent(blockPos);
  PublishNeighborPhysicsEvents(blockPos);
  for (const glm::ivec3 &above_pos : broken_above)
  {
    if (CachedBlockCount > 0)
    {
      --CachedBlockCount;
    }
    PublishBlockPhysicsEvent(above_pos);
    PublishNeighborPhysicsEvents(above_pos);
  }
  if (ViewBinding)
  {
    ViewBinding->RefreshIntersectionFromCurrentView(*this);
  }
  return true;
}

bool UWorld::DelObjectByView(const glm::vec3 &position, const glm::vec3 &front)
{
  const auto hit =
      RaycastSolidBlocks(BlockWorld, *BlockRegistry, position, front);
  if (!hit)
  {
    return false;
  }
  return DelBlockAt(hit->blockPos);
}

void UWorld::StartBreakSession(glm::ivec3 blockPos, float pendingWearDelta,
                               std::string pendingToolId)
{
  if (!BreakService)
  {
    BreakService = std::make_unique<UBlockBreakService>();
  }
  BreakService->Start(blockPos, pendingWearDelta, std::move(pendingToolId));
}

void UWorld::CancelBreakSession()
{
  if (BreakService)
  {
    BreakService->Cancel();
  }
}

void UWorld::TickBreakSession(float dt, float durationSeconds)
{
  if (BreakService)
  {
    BreakService->Tick(dt, durationSeconds);
  }
}

bool UWorld::CompleteBreakSession()
{
  if (!BreakService)
  {
    return false;
  }
  return BreakService->Complete(*this);
}

float UWorld::GetBreakProgress() const
{
  return BreakService ? BreakService->GetProgress() : 0.f;
}

std::optional<glm::ivec3> UWorld::GetBreakSessionBlockPos() const
{
  return BreakService ? BreakService->GetBlockPos() : std::nullopt;
}

FluidColumnSurface UWorld::FindFluidColumnSurfaceAt(int bx, int bz,
                                                    int hintY) const
{
  return Collision.FindFluidColumnSurfaceAt(bx, bz, hintY);
}

FluidColumnSurface UWorld::FindFluidColumnSurface(const glm::vec3 &eye) const
{
  return Collision.FindFluidColumnSurfaceEye(eye);
}

bool UWorld::HasNearbyFluidSurface(glm::ivec3 cameraBlock,
                                   int radiusBlocks) const
{
  if (!BlockRegistry)
  {
    return false;
  }
  return HasFluidSurfaceNear(BlockWorld, *BlockRegistry, cameraBlock.x,
                             cameraBlock.z, cameraBlock.y, radiusBlocks);
}

bool UWorld::IsCameraInsideFluid(const glm::vec3 &eye, BlockId *outFluid) const
{
  const FluidColumnSurface column = FindFluidColumnSurface(eye);
  if (!column.valid || eye.y >= column.surfaceY)
  {
    return false;
  }
  if (outFluid)
  {
    *outFluid = column.fluidId;
  }
  return true;
}

SampledFluidState
UWorld::SampleFluidPhysicsVolume(const CollisionVolume &vol) const
{
  return Collision.SampleFluidPhysicsVolume(vol);
}

bool UWorld::IsFoliageFluidBlock(BlockId id) const
{
  if (!BlockRegistry || id == BLOCK_AIR)
  {
    return false;
  }
  const auto &mov = BlockRegistry->Physics(id).Movement;
  return mov.Occupancy < 1.0f && mov.SinkSpeed == 0.0f &&
         mov.RiseSpeed == 0.0f && mov.DragHorizontal == 0.0f;
}

SampledFluidState UWorld::SampleFluidPhysics(const glm::vec3 &eyePos,
                                             const PlayerCapsule &cap) const
{
  return SampleFluidPhysicsVolume(CollisionVolumeFromEye(eyePos, cap));
}

bool UWorld::CheckBlockCollisionVolume(const CollisionVolume &vol) const
{
  return Collision.CheckBlockCollisionVolume(vol);
}

bool UWorld::CheckCreatureCollisionVolume(const CollisionVolume &vol,
                                          CreatureId skipCreatureId) const
{
  return Collision.CheckCreatureCollisionVolume(vol, skipCreatureId);
}

bool UWorld::CheckCollisionVolume(const CollisionVolume &vol,
                                  CreatureId skipCreatureId) const
{
  return Collision.CheckCollisionVolume(vol, skipCreatureId);
}

bool UWorld::CheckCollision(const glm::vec3 &eyePos,
                            const PlayerCapsule &cap) const
{
  return CheckCollision(eyePos, cap, GetMovementCollisionSkipId());
}

bool UWorld::CheckCollision(const glm::vec3 &eyePos, const PlayerCapsule &cap,
                            CreatureId skipCreatureId) const
{
  return Collision.CheckCollision(eyePos, cap, skipCreatureId);
}

bool UWorld::DepenetrateEye(glm::vec3 &eyePos, const PlayerCapsule &cap,
                            CreatureId skipCreatureId) const
{
  return Collision.DepenetrateEye(eyePos, cap, skipCreatureId);
}

bool UWorld::DepenetrateCreatureBodyXZ(glm::vec3 &bodyOrigin,
                                       const glm::vec3 &sizeBlocks,
                                       CreatureId skipCreatureId) const
{
  return Collision.DepenetrateCreatureBodyXZ(bodyOrigin, sizeBlocks,
                                             skipCreatureId);
}

bool UWorld::DepenetrateBlockBodyXZ(glm::vec3 &bodyOrigin,
                                    const glm::vec3 &sizeBlocks) const
{
  return Collision.DepenetrateBlockBodyXZ(bodyOrigin, sizeBlocks);
}

bool UWorld::HasGroundSupportVolume(const CollisionVolume &vol,
                                    float feetY) const
{
  return Collision.HasGroundSupportVolume(vol, feetY);
}

bool UWorld::HasGroundSupport(const glm::vec3 &eyePos,
                              const PlayerCapsule &cap) const
{
  return Collision.HasGroundSupport(eyePos, cap);
}

glm::vec3 UWorld::ResolveMovementBody(const glm::vec3 &bodyOrigin,
                                      const glm::vec3 &delta,
                                      const glm::vec3 &currentSizeBlocks,
                                      CreatureId skipCreatureId) const
{
  return Collision.ResolveMovementBody(bodyOrigin, delta, currentSizeBlocks,
                                       skipCreatureId);
}

glm::vec3 UWorld::ResolveMovement(const glm::vec3 &eyePos,
                                  const glm::vec3 &delta,
                                  const PlayerCapsule &cap,
                                  CreatureId skipCreatureId) const
{
  return Collision.ResolveMovement(eyePos, delta, cap, skipCreatureId);
}

UWorldCollision::StepUpProbe UWorld::ProbeStepUp(const glm::vec3 &eyePos,
                                                 const glm::vec3 &horiz,
                                                 const PlayerCapsule &cap,
                                                 float maxTriggerDistance) const
{
  return Collision.ProbeStepUp(eyePos, horiz, cap, maxTriggerDistance);
}

bool UWorld::GetStepUpLanding(const glm::vec3 &eyePos, const glm::vec3 &horiz,
                              const PlayerCapsule &cap,
                              float maxTriggerDistance,
                              glm::vec3 &outLanding) const
{
  return Collision.GetStepUpLanding(eyePos, horiz, cap, maxTriggerDistance,
                                    outLanding);
}

bool UWorld::TryStepUp(glm::vec3 &eyePos, const glm::vec3 &horiz,
                       const PlayerCapsule &cap, float maxTriggerDistance) const
{
  return Collision.TryStepUp(eyePos, horiz, cap, maxTriggerDistance);
}

void UWorld::LoadUsers(const std::string &file_name)
{
  Persistence->LoadUsers(*this, file_name);
}

void UWorld::SaveUsers(const std::string &file_name)
{
  Persistence->SaveUsers(*this, file_name);
}

void UWorld::LoadWorldData(const std::string &file_name)
{
  Persistence->LoadWorldData(*this, file_name);
}

void UWorld::SaveWorldData(const std::string &file_name)
{
  Persistence->SaveWorldData(*this, file_name);
}

void UWorld::SaveMovementDiagnostics(const std::string &file_name) const
{
  UMovementDiagnosticsRecorder::SaveToFile(*this, file_name);
}

void UWorld::ConfigurePhysicsServices()
{
  BlockPhysicsService = std::make_unique<UWorldBlockPhysicsService>();
  MovementPhysicsService = std::make_unique<UWorldMovementPhysicsService>();
  BreakService = std::make_unique<UBlockBreakService>();
  ChunkDirtyService = std::make_unique<UWorldChunkDirtyService>();
  if (BlockPhysicsService)
  {
    BlockPhysicsService->SetBudgets(PhysicsBudgetConfig);
    UPhysicsProfileFactory::ConfigureService(
        ActivePhysicsProfile, *BlockPhysicsService, PhysicsFlags);
  }
  if (ChunkDirtyService)
  {
    ChunkDirtyService->SetBudgets(PhysicsBudgetConfig);
  }
  PhysicsScheduler = std::make_unique<UWorldPhysicsScheduler>(
      MovementPhysicsService.get(), BlockPhysicsService.get(),
      ChunkDirtyService.get());
  Collision.SetTelemetry(&PhysicsTelemetryData);
}

void UWorld::SetPhysicsProfile(PhysicsProfile profile)
{
  ActivePhysicsProfile = profile;
  if (BlockPhysicsService)
  {
    UPhysicsProfileFactory::ConfigureService(
        ActivePhysicsProfile, *BlockPhysicsService, PhysicsFlags);
  }
}

void UWorld::SetPhysicsFeatureFlags(const PhysicsFeatureFlags &flags)
{
  PhysicsFlags = flags;
  Collision.SetBroadphaseEnabled(flags.EnableCollisionBroadphase);
  Collision.SetCollisionDdaEnabled(flags.EnableCollisionDda);
  Collision.SetTelemetry(&PhysicsTelemetryData);
  if (BlockPhysicsService)
  {
    UPhysicsProfileFactory::ConfigureService(
        ActivePhysicsProfile, *BlockPhysicsService, PhysicsFlags);
  }
}

void UWorld::SetPhysicsBudgets(const PhysicsBudgets &budgets)
{
  PhysicsBudgetConfig = budgets;
  if (BlockPhysicsService)
  {
    BlockPhysicsService->SetBudgets(budgets);
  }
  if (ChunkDirtyService)
  {
    ChunkDirtyService->SetBudgets(budgets);
  }
}

void UWorld::UpdatePhysicsQueueStats(const BlockUpdateQueueStats &blockStats,
                                     const FluidUpdateSetStats &fluidStats)
{
  PhysicsTelemetryData.BlockQueueDepth = blockStats.Depth;
  PhysicsTelemetryData.LiquidQueueDepth = fluidStats.Depth;
  PhysicsTelemetryData.DeferredUpdates = blockStats.Deferred;
  PhysicsTelemetryData.DroppedUpdates = blockStats.Dropped + fluidStats.Dropped;
  PhysicsTelemetryData.PurgedUpdates = blockStats.Purged;
}

void UWorld::AccumulateFallingStats(const FallingBlocksStats &stats)
{
  PhysicsTelemetryData.DeferredUpdates += stats.Deferred;
  PhysicsTelemetryData.DroppedUpdates += stats.Dropped;
}

void UWorld::AccumulateFluidStats(const FluidSpreadStats &stats)
{
  PhysicsTelemetryData.DeferredUpdates += stats.Candidates - stats.Applied;
  PhysicsTelemetryData.DroppedUpdates += 0;
  (void)stats;
}

bool UWorld::IsWithinLiquidUpdateRadius(glm::ivec3 blockPos) const
{
  const glm::ivec3 chunkCoord = UChunkManager::WorldToChunk(blockPos);
  const glm::ivec3 focus = MovementDiag.feetChunk;
  const int radius = std::max({std::abs(chunkCoord.x - focus.x),
                               std::abs(chunkCoord.y - focus.y),
                               std::abs(chunkCoord.z - focus.z)});
  return radius <= PhysicsBudgetConfig.LiquidUpdateRadiusChunks;
}

void UWorld::MarkFluidRegionDirty(glm::ivec3 center, int block_radius)
{
  UWorldFluidFacade::MarkFluidRegionDirty(*this, center, block_radius);
}

void UWorld::MarkFluidChangeDirty(glm::ivec3 blockPos)
{
  // Simulation path: always budgeted remesh (async when enabled). Use
  // MarkBlockChunkDirty for player-driven edits that need instant feedback.
  MarkBlockChunkDirtyFromPhysics(blockPos);
}

void UWorld::MarkFluidFloodMeshDirty(
    glm::ivec3 blockPos, const std::vector<glm::ivec3> &filled_blocks)
{
  UWorldFluidFacade::MarkFluidFloodMeshDirty(*this, blockPos, filled_blocks);
}

void UWorld::TryEnqueueFluidAt(glm::ivec3 blockPos)
{
  if (!BlockPhysicsService || !BlockRegistry || !PhysicsFlags.EnableFluids)
  {
    return;
  }
  const UBlockDefinitionStorage *definitions = BlockRegistry->GetDefinitions();
  if (definitions == nullptr)
  {
    return;
  }
  const BlockId block_id = BlockWorld.GetBlock(blockPos);
  if (!BlockRegistry->IsLiquid(block_id) &&
      !UFluidSpreadSystem::CanReceiveFluid(BlockWorld, *definitions, blockPos))
  {
    return;
  }
  if (!UFluidSpreadSystem::HasSpreadTargetForTick(BlockWorld, *definitions,
                                                  blockPos, PhysicsTickCounter))
  {
    return;
  }
  BlockPhysicsService->PublishFluid(blockPos);
}

void UWorld::ForceEnqueueFluidAt(glm::ivec3 blockPos)
{
  if (!BlockPhysicsService || !BlockRegistry || !PhysicsFlags.EnableFluids)
  {
    return;
  }
  BlockPhysicsService->PublishFluid(blockPos);
}

void UWorld::WakeFluidFrontier(glm::ivec3 blockPos, int radius_blocks)
{
  if (!BlockRegistry || !PhysicsFlags.EnableFluids || !BlockPhysicsService)
  {
    return;
  }
  BlockPhysicsService->PublishFluid(blockPos);
  const int clamped_radius = std::max(0, radius_blocks);
  for (int dx = -clamped_radius; dx <= clamped_radius; ++dx)
  {
    for (int dy = -clamped_radius; dy <= clamped_radius; ++dy)
    {
      for (int dz = -clamped_radius; dz <= clamped_radius; ++dz)
      {
        if (dx == 0 && dy == 0 && dz == 0)
        {
          continue;
        }
        const glm::ivec3 pos(blockPos.x + dx, blockPos.y + dy, blockPos.z + dz);
        if (BlockRegistry->IsLiquid(BlockWorld.GetBlock(pos)))
        {
          TryEnqueueFluidAt(pos);
        }
      }
    }
  }
}

void UWorld::TrySeedFallingAt(glm::ivec3 blockPos)
{
  if (!BlockPhysicsService || !BlockRegistry || !PhysicsFlags.EnableFalling)
  {
    return;
  }
  if (!BlockRegistry->IsFallingBlock(BlockWorld.GetBlock(blockPos)))
  {
    return;
  }
  const glm::ivec3 chunk_coord = UChunkManager::WorldToChunk(blockPos);
  BlockPhysicsService->PublishSupportLost(
      blockPos, chunk_coord, PhysicsTickCounter, ++PhysicsEventOrderCounter);
}

void UWorld::PublishBlockPhysicsEvent(glm::ivec3 blockPos)
{
  if (!BlockPhysicsService)
  {
    return;
  }
  const glm::ivec3 chunkCoord = UChunkManager::WorldToChunk(blockPos);
  BlockPhysicsService->PublishBlockChanged(
      blockPos, chunkCoord, PhysicsTickCounter, ++PhysicsEventOrderCounter);
}

void UWorld::PublishNeighborPhysicsEvents(glm::ivec3 blockPos)
{
  if (!BlockPhysicsService)
  {
    return;
  }
  for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
  {
    const glm::ivec3 pos = blockPos + offset;
    const glm::ivec3 chunkCoord = UChunkManager::WorldToChunk(pos);
    BlockPhysicsService->PublishNeighborChanged(
        pos, chunkCoord, PhysicsTickCounter, ++PhysicsEventOrderCounter);
  }
  const glm::ivec3 above(blockPos.x, blockPos.y + 1, blockPos.z);
  const glm::ivec3 aboveChunk = UChunkManager::WorldToChunk(above);
  BlockPhysicsService->PublishSupportLost(above, aboveChunk, PhysicsTickCounter,
                                          ++PhysicsEventOrderCounter);
}

bool UWorld::IsCollisionReadyAtFeet(const glm::ivec3 &feetBlock) const
{
  if (!PhysicsFlags.EnableCollisionReadinessGate || !Streaming ||
      !Streaming->HasStreamer())
  {
    return true;
  }
  if (BlockRegistry)
  {
    for (int dy = 0; dy <= 2; ++dy)
    {
      const BlockId id =
          BlockWorld.GetBlock(glm::ivec3(feetBlock.x, feetBlock.y + dy, feetBlock.z));
      if (BlockRegistry->IsLiquid(id))
      {
        return true;
      }
    }
  }
  if (const auto *streamer = Streaming->GetStreamer())
  {
    return streamer->IsCollisionReady(
        feetBlock, PhysicsBudgetConfig.CollisionSafetyRadiusChunks);
  }
  return false;
}

void UWorld::DoMovement()
{
  {
    using clock = std::chrono::high_resolution_clock;
    const auto t0 = clock::now();
    TickEnvironment(static_cast<float>(WallFrameDeltaSec));
    PhysicsTelemetryData.TickEnvMs =
        std::chrono::duration<double, std::milli>(clock::now() - t0).count();
  }
  ++PhysicsTickCounter;
  if (PhysicsScheduler)
  {
    using clock = std::chrono::high_resolution_clock;
    const auto t_begin = clock::now();
    auto t_after_move = t_begin;

    if (MovementPhysicsService)
    {
      MovementPhysicsService->TickMovement(*this);
      t_after_move = clock::now();
    }

    double block_ms = 0.0;
    double drain_ms = 0.0;
    if (BlockPhysicsService)
    {
      const auto tb = clock::now();
      BlockPhysicsService->TickBlockPhysics(*this);
      block_ms =
          std::chrono::duration<double, std::milli>(clock::now() - tb).count();
    }
    if (ChunkDirtyService)
    {
      const auto tb = clock::now();
      ChunkDirtyService->DrainRebuildQueues(*this);
      drain_ms =
          std::chrono::duration<double, std::milli>(clock::now() - tb).count();
    }

    const auto t_end = clock::now();
    PhysicsTelemetryData.MovementStepMs =
        std::chrono::duration<double, std::milli>(t_after_move - t_begin)
            .count();
    PhysicsTelemetryData.BlockStepMs = block_ms;
    PhysicsTelemetryData.DrainStepMs = drain_ms;
    PhysicsTelemetryData.FluidStepMs = block_ms;
    PhysicsTelemetryData.SimulationStepsThisFrame = 1;
    PhysicsTelemetryData.PhysicsStepMs =
        std::chrono::duration<double, std::milli>(t_end - t_begin).count();
    DurationDoMovementMks = static_cast<uint64_t>(
        std::chrono::duration<double, std::micro>(t_end - t_begin).count());
    return;
  }
  RunLegacyPhysicsFrame();
}

void UWorld::SetWallFrameDelta(double seconds)
{
  WallFrameDeltaSec = seconds > 0.0 ? seconds : 0.0;
}

void UWorld::UpdateFrameHitchDiagnostics(double draw_scene_mks,
                                         double view_update_mks)
{
  DurationDrawSceneMks = static_cast<uint64_t>(draw_scene_mks);
  DurationViewUpdateMks = static_cast<uint64_t>(view_update_mks);
  const double sim_ms =
      (DurationDoMovementMks + view_update_mks + draw_scene_mks) / 1000.0;
  const double wall_ms =
      WallFrameDeltaSec > 0.0 ? WallFrameDeltaSec * 1000.0 : sim_ms;
  const double frameMs = std::max(sim_ms, wall_ms);
  MovementDiag.hitchDetected = frameMs > 50.0 ||
                               PhysicsTelemetryData.PhysicsStepMs > 50.0 ||
                               MovementDiag.deltaTime > 0.1f;
  MovementDiag.simMs = sim_ms;
  MovementDiag.swapWaitMs = LastSwapWaitMs;
  MovementDiag.unaccountedMs = wall_ms - sim_ms - LastSwapWaitMs;
}

void UWorld::ResetMeshLoadDiagnostics()
{
  FramesSinceLoad = 0;
  MeshBacklogClearedLatch = false;
  MeshLoadDiagActive = true;
}

void UWorld::TickMeshLoadDiagnostics()
{
  if (!MeshLoadDiagActive)
  {
    return;
  }
  if (FramesSinceLoad < 600 || MeshService->HasPendingDirty() ||
      MeshService->HasPendingAsyncMeshWork())
  {
    ++FramesSinceLoad;
  }
  if (!MeshBacklogClearedLatch && !MeshService->HasPendingDirty() &&
      !MeshService->HasPendingAsyncMeshWork())
  {
    MeshBacklogClearedLatch = true;
  }
  if (MeshBacklogClearedLatch && FramesSinceLoad >= 600 &&
      !MeshService->HasPendingDirty())
  {
    MeshLoadDiagActive = false;
  }
}

void UWorld::UpdateStreaming()
{
  Streaming->UpdateStreaming(*this, *MeshService, Render, RenderDistanceChunks,
                             EffectiveRenderDistance, EffectiveFogStartRatio,
                             AltitudeParams, LastCameraPosition,
                             LastMovementSpeed, LastMovementDirXz);
}

size_t UWorld::GetRenderInstanceCount() const
{
  if (Render.GreedyMeshing)
  {
    return MeshService->GetGreedyVertexCount();
  }
  return MeshService->GetInstanceCount();
}

bool UWorld::GetIsIntersectionExists() const { return IsIntersectionExists; }

size_t UWorld::GetIntersectionObjectIndex() const
{
  return IntersectionObjectIndex;
}

size_t UWorld::GetIntersectionCubeIndex() const
{
  return IntersectionCubeIndex;
}

uint64_t UWorld::GetDurationDoMovementMks() const
{
  return DurationDoMovementMks;
}

void UWorld::InvalidateBlockMesh()
{
  if (BlockRegistry)
  {
    MeshService->MarkAllDirtyFromWorld(BlockWorld);
  }
}

void UWorld::SetRenderSettings(const RenderSettings &settings)
{
  LightingMode new_mode =
      GraphicsQualityProfile::FromPreset(settings.Preset).GetLightingMode();
  // D1.4: Desktop GPU stack must not consume Flat lighting results.
  {
    const RenderBackendCaps caps = GetActiveRenderBackendCaps();
    const RenderBackendSelection sel = URenderBackendFactory::Select(caps);
    if (sel.Mesher == MesherBackendKind::GpuGreedy ||
        sel.Mesher == MesherBackendKind::AndroidHybridGpu)
    {
      new_mode = LightingMode::Full;
    }
  }
  const LightingMode old_mode = LightingPipeline
                                    ? LightingPipeline->GetMode()
                                    : LightingMode::Full;
  Render = settings;
  MeshService->SetRenderSettings(settings);
  if (!LightingPipeline || old_mode != new_mode)
  {
    LightingPipeline = ULightingPipelineFactory::Create(new_mode);
    if (old_mode != new_mode)
    {
      if (!LightingPipeline->RequiresLitGate())
      {
        LightingPipeline->FillAllLoadedChunks(BlockWorld);
        PendingLightBeforeMesh.clear();
      }
      else if (BlockRegistry)
      {
        LightingPipeline->RelightAllLoadedChunks(BlockWorld, *BlockRegistry);
      }
      InvalidateBlockMesh();
    }
  }
}

UWorldMeshService &UWorld::GetMeshService() { return *MeshService; }

const UWorldMeshService &UWorld::GetMeshService() const { return *MeshService; }

void UWorld::MarkColumnMeshDirty(int world_x, int world_z, int min_y, int max_y)
{
  MeshService->MarkColumnMeshDirty(world_x, world_z, min_y, max_y);
}

void UWorld::MarkTerrainChunkMeshDirty(glm::ivec3 groundChunkCoord, int min_y,
                                       int max_y)
{
  MeshService->MarkTerrainChunkMeshDirty(groundChunkCoord, min_y, max_y);
}

void UWorld::MarkTerrainChunkMeshDirtySeamed(glm::ivec3 groundChunkCoord,
                                             int min_y, int max_y,
                                             bool include_horizontal_neighbors)
{
  MeshService->MarkTerrainChunkMeshDirtySeamed(
      groundChunkCoord, min_y, max_y, include_horizontal_neighbors);
}

void UWorld::MarkBlocksChunkDirtyBatch(
    const std::vector<glm::ivec3> &block_positions, bool sync_neighbor_chunks,
    bool sync_light_ring, bool collect_break_diag)
{
  MeshService->MarkBlocksChunkDirtyBatchFromEdit(
      BlockWorld, BlockRegistry.get(), block_positions, ModifiedChunks,
      sync_neighbor_chunks, sync_light_ring, collect_break_diag,
      &PhysicsTelemetryData);
  if (ChunkDirtyService)
  {
    for (const glm::ivec3 &pos : block_positions)
    {
      ChunkDirtyService->MarkCollisionRebuild(*this, pos);
    }
  }
}

void UWorld::MarkBlockChunkDirty(glm::ivec3 blockPos, bool sync_neighbor_chunks,
                                 bool sync_light_ring)
{
  MeshService->MarkBlockChunkDirtyFromEdit(
      BlockWorld, BlockRegistry.get(), blockPos, ModifiedChunks,
      sync_neighbor_chunks, sync_light_ring);
  MeshService->NotifyFluidSurfaceDirtyAtBlock(BlockWorld, BlockRegistry.get(),
                                              blockPos);
  // Player/world edits remesh immediately but used to leave ChunkOccupancyMask
  // stale — visible stone with walk-through collision (prefab buildings).
  if (ChunkDirtyService)
  {
    ChunkDirtyService->MarkCollisionRebuild(*this, blockPos);
  }
}

void UWorld::MarkBlockChunkDirtyFromPhysics(glm::ivec3 blockPos)
{
  if (ChunkDirtyService)
  {
    ChunkDirtyService->MarkVisualRemesh(*this, blockPos);
    // Pure fluid cells do not change solid occupancy — skip collision rebuild.
    const BlockId id = BlockWorld.GetBlock(blockPos);
    const bool needs_collision =
        !BlockRegistry || !BlockRegistry->IsLiquid(id) ||
        BlockRegistry->BlocksMovement(id);
    if (needs_collision)
    {
      ChunkDirtyService->MarkCollisionRebuild(*this, blockPos);
    }
    if (MeshService)
    {
      MeshService->NotifyFluidSurfaceDirtyAtBlock(BlockWorld,
                                                  BlockRegistry.get(), blockPos);
    }
    return;
  }
  MarkBlockChunkDirty(blockPos);
}

} // namespace cutum

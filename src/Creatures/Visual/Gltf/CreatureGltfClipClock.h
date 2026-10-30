#ifndef CREATUREGLTFCLIPCLOCK_H
#define CREATUREGLTFCLIPCLOCK_H

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

namespace cutum
{

/// Clip-local time: looped clips wrap into [0, duration), one-shot clips
/// clamp at the end. A clip without keys (duration 0) stays at 0.
inline float WrapGltfClipTime(float timeSec, float duration, bool loop)
{
  if (duration <= 0.f)
  {
    return 0.f;
  }
  if (!loop)
  {
    return std::clamp(timeSec, 0.f, duration);
  }
  float t = std::fmod(timeSec, duration);
  if (t < 0.f)
  {
    t += duration;
  }
  return t;
}

/// Playback clock for one creature's glTF clips. The time used to reset to 0
/// on every clip change (Walk <-> Idle flips several times a second at the
/// edge of the walk threshold: the pose snapped back to the clip start) and
/// to grow without bound otherwise (float precision lost after hours).
/// Now each looped clip keeps its own wrapped phase and resumes it, one-shot
/// clips restart, and the pose of the clip being left fades out over
/// kFadeSec (frozen at the time it was left).
class UGltfClipClock
{
public:
  static constexpr float kFadeSec = 0.15f;

  void Update(const std::string &clip, float dt, float playback,
              float duration, bool loop)
  {
    if (clip != Clip)
    {
      if (!Clip.empty())
      {
        FadeClip = Clip;
        FadeTime = Time;
        FadeElapsed = 0.f;
      }
      Clip = clip;
      const auto it = Phases.find(clip);
      Time = (loop && it != Phases.end()) ? it->second : 0.f;
    }
    else
    {
      FadeElapsed += dt;
    }
    Time = WrapGltfClipTime(Time + dt * playback, duration, loop);
    if (loop)
    {
      Phases[clip] = Time;
    }
    if (FadeElapsed >= kFadeSec)
    {
      FadeClip.clear();
    }
  }

  const std::string &GetClip() const { return Clip; }
  float GetTime() const { return Time; }
  /// Clip being faded out ("" when none) and its frozen time.
  const std::string &GetFadeClip() const { return FadeClip; }
  float GetFadeTime() const { return FadeTime; }
  /// Weight of the faded-out pose: 1 right after the switch, 0 after kFadeSec.
  float GetFadeWeight() const
  {
    if (FadeClip.empty())
    {
      return 0.f;
    }
    return std::clamp(1.f - FadeElapsed / kFadeSec, 0.f, 1.f);
  }

private:
  std::string Clip;
  float Time{0.f};
  std::string FadeClip;
  float FadeTime{0.f};
  float FadeElapsed{0.f};
  std::unordered_map<std::string, float> Phases;
};

} // namespace cutum

#endif

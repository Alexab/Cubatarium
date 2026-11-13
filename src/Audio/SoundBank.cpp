#include "Audio/SoundBank.h"

#include "Audio/AudioEngine.h"
#include "Audio/SoundManifest.h"
#include "ThirdParty/miniaudio.h"

#include <algorithm>
#include <deque>
#include <iostream>

namespace cutum
{

namespace
{

/// Voices per set: footstep spam needs overlap headroom; round-robin keeps
/// repeats varied. Total pool memory stays tiny (short decoded clips).
constexpr std::uint32_t kVoicesPerSet = 4;
/// World sounds fade to nothing this far out regardless of the set's
/// reference distance (linear distance model).
constexpr float kMaxSoundDistance = 72.f;

std::filesystem::path ResolveVariantPath(const std::filesystem::path &root,
                                         const std::string &relative)
{
  return root / std::filesystem::path(relative);
}

} // namespace

struct USoundBank::State
{
  /// Flat pool: sets * kVoicesPerSet ma_sounds, each pre-bound to one
  /// variant file. Restarting a voice is a seek + start. deque keeps
  /// element addresses stable: an initialized ma_sound must not move
  /// (its node-graph links are self-referential).
  std::deque<ma_sound> Voices;
  std::deque<bool> VoiceReady;
};

USoundBank::~USoundBank()
{
  if (Ready)
  {
    Shutdown();
  }
}

std::size_t USoundBank::Init(UAudioEngine *engine,
                             const USoundManifest &manifest,
                             const std::filesystem::path &soundsRoot)
{
  Shutdown();
  ma_engine *maEngine = static_cast<ma_engine *>(engine->GetRawEngine());
  if (!maEngine)
  {
    return 0;
  }

  Sets.clear();
  IdsByName.clear();
  State_ = new State();

  std::size_t usableSets = 0;
  std::uint32_t totalVoices = 0;
  for (const SoundSetDef &def : manifest.GetSets())
  {
    if (def.Loop)
    {
      // Ambience loops stream through the director's loop pairs, not the
      // one-shot voice pool.
      continue;
    }
    BankSet set;
    set.Name = def.Name;
    set.Bus = def.Bus;
    set.BaseGain = def.BaseGain;
    set.BaseDistance = def.BaseDistance;
    set.MinIntervalSec = def.MinIntervalSec;

    ma_sound_group *group =
        static_cast<ma_sound_group *>(engine->GetRawGroup(def.Bus));
    const ma_sound_flags flags = static_cast<ma_sound_flags>(
        (set.BaseDistance > 0.f)
            ? MA_SOUND_FLAG_DECODE
            : (MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION));

    set.VoiceBase = totalVoices;
    std::uint32_t readyVoices = 0;
    for (std::uint32_t voice = 0; voice < kVoicesPerSet; ++voice)
    {
      const std::string &variant =
          def.Files[voice % def.Files.size()];
      const std::filesystem::path fullPath =
          ResolveVariantPath(soundsRoot, variant);
      const std::string pathStr = fullPath.string();

      State_->Voices.emplace_back();
      ma_sound &sound = State_->Voices.back();
      const ma_result result = ma_sound_init_from_file(
          maEngine, pathStr.c_str(), flags, group, nullptr, &sound);
      if (result == MA_SUCCESS)
      {
        if (set.BaseDistance > 0.f)
        {
          ma_sound_set_min_distance(&sound, set.BaseDistance * 0.5f);
          ma_sound_set_max_distance(&sound, kMaxSoundDistance);
        }
        State_->VoiceReady.push_back(true);
        ++readyVoices;
        ++totalVoices;
      }
      else
      {
        // Missing asset: pop the unusable slot and keep the game silent on
        // this voice (PlaceholderTextureCache degradation pattern).
        State_->Voices.pop_back();
        State_->VoiceReady.push_back(false);
        std::cerr << "WARN: Audio: missing sound file: " << pathStr
                  << std::endl;
      }
    }
    set.VoiceCount = readyVoices;
    if (readyVoices > 0)
    {
      ++usableSets;
      IdsByName[set.Name] = static_cast<std::uint32_t>(Sets.size());
      Sets.push_back(std::move(set));
    }
  }

  Ready = true;
  return usableSets;
}

void USoundBank::Shutdown()
{
  if (State_)
  {
    // ma_sound_uninit works regardless of init failure order; only free
    // slots that were successfully initialized.
    for (std::size_t i = 0; i < State_->Voices.size(); ++i)
    {
      if (i < State_->VoiceReady.size() && State_->VoiceReady[i])
      {
        ma_sound_uninit(&State_->Voices[i]);
      }
    }
    State_->Voices.clear();
    State_->VoiceReady.clear();
    delete State_;
    State_ = nullptr;
  }
  Sets.clear();
  IdsByName.clear();
  Ready = false;
}

std::uint32_t USoundBank::ResolveId(const std::string &name) const
{
  auto it = IdsByName.find(name);
  return it == IdsByName.end() ? 0 : it->second;
}

const char *USoundBank::GetName(std::uint32_t bankId) const
{
  if (bankId == 0 || bankId > Sets.size())
  {
    return "";
  }
  return Sets[bankId - 1].Name.c_str();
}

USoundBank::BankSet *USoundBank::GetSet(std::uint32_t bankId)
{
  if (bankId == 0 || bankId > Sets.size())
  {
    return nullptr;
  }
  return &Sets[bankId - 1];
}

const USoundBank::BankSet *USoundBank::GetSet(std::uint32_t bankId) const
{
  if (bankId == 0 || bankId > Sets.size())
  {
    return nullptr;
  }
  return &Sets[bankId - 1];
}

void USoundBank::Play(std::uint32_t bankId, float x, float y, float z,
                      float pitch, float gainMul, float strength)
{
  BankSet *set = GetSet(bankId);
  if (!set || set->VoiceCount == 0 || !State_)
  {
    return;
  }
  // Round-robin; skip voices that failed to load.
  for (std::uint32_t attempt = 0; attempt < set->VoiceCount; ++attempt)
  {
    const std::size_t voiceIndex =
        set->VoiceBase + (set->NextVoice + attempt) % set->VoiceCount;
    set->NextVoice = (set->NextVoice + attempt + 1) % set->VoiceCount;
    if (voiceIndex >= State_->VoiceReady.size() ||
        !State_->VoiceReady[voiceIndex])
    {
      continue;
    }
    ma_sound &sound = State_->Voices[voiceIndex];
    ma_sound_set_pitch(&sound, pitch);
    ma_sound_set_volume(&sound, set->BaseGain * gainMul * strength);
    if (set->BaseDistance > 0.f)
    {
      ma_sound_set_position(&sound, x, y, z);
    }
    ma_sound_seek_to_pcm_frame(&sound, 0);
    ma_sound_start(&sound);
    return;
  }
}

bool USoundBank::IsOnCooldown(std::uint32_t bankId, float nowSec,
                              float intervalMul) const
{
  const BankSet *set = GetSet(bankId);
  return set && nowSec - set->LastPlaySec <
                    set->MinIntervalSec * std::max(1.f, intervalMul);
}

void USoundBank::MarkPlayed(std::uint32_t bankId, float nowSec)
{
  if (BankSet *set = GetSet(bankId))
  {
    set->LastPlaySec = nowSec;
  }
}

} // namespace cutum

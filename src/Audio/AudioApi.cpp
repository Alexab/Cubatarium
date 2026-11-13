#include "Audio/AudioApi.h"

#include "Audio/AudioEventQueue.h"

#include <mutex>

namespace cutum
{

namespace
{

std::mutex ListenerMutex;
AudioListenerSnapshot Listener;

std::mutex AmbienceMutex;
AudioAmbienceSnapshot Ambience;

} // namespace

UAudioEventQueue *UAudioApi::Queue = nullptr;

void UAudioApi::Bind(UAudioEventQueue *queue) { Queue = queue; }

void UAudioApi::Post(const UAudioEvent &event)
{
  if (Queue)
  {
    Queue->Post(event);
  }
}

void UAudioApi::UpdateListener(const AudioListenerSnapshot &snapshot)
{
  std::lock_guard<std::mutex> lock(ListenerMutex);
  Listener = snapshot;
}

AudioListenerSnapshot UAudioApi::ReadListener()
{
  std::lock_guard<std::mutex> lock(ListenerMutex);
  return Listener;
}

void UAudioApi::UpdateAmbience(const AudioAmbienceSnapshot &snapshot)
{
  std::lock_guard<std::mutex> lock(AmbienceMutex);
  Ambience = snapshot;
}

AudioAmbienceSnapshot UAudioApi::ReadAmbience()
{
  std::lock_guard<std::mutex> lock(AmbienceMutex);
  return Ambience;
}

bool UAudioApi::IsBound() { return Queue != nullptr; }

} // namespace cutum

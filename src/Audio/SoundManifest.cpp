#include "Audio/SoundManifest.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{

using json = nlohmann::json;

AudioBus ParseBus(const std::string &bus, AudioBus fallback)
{
  if (bus == "music")
  {
    return AudioBus::Music;
  }
  if (bus == "ambient")
  {
    return AudioBus::Ambient;
  }
  if (bus == "ui")
  {
    return AudioBus::Ui;
  }
  if (bus == "sfx")
  {
    return AudioBus::Sfx;
  }
  return fallback;
}

} // namespace

std::uint32_t UAudioNameRegistry::Intern(const std::string &name)
{
  for (std::size_t i = 0; i < Names.size(); ++i)
  {
    if (Names[i] == name)
    {
      return static_cast<std::uint32_t>(i);
    }
  }
  Names.push_back(name);
  return static_cast<std::uint32_t>(Names.size() - 1);
}

const std::string *UAudioNameRegistry::GetName(std::uint32_t id) const
{
  if (id >= Names.size())
  {
    return nullptr;
  }
  return &Names[id];
}

bool USoundManifest::LoadFromFile(const std::string &path)
{
  std::ifstream file(path);
  if (!file.is_open())
  {
    std::cerr << "Audio: sound manifest not found: " << path << std::endl;
    return false;
  }
  try
  {
    json root = json::parse(file);
    return Load(&root);
  }
  catch (const json::exception &e)
  {
    std::cerr << "Audio: sound manifest parse error in " << path << ": "
              << e.what() << std::endl;
    return false;
  }
}

bool USoundManifest::Load(const void *jsonRoot)
{
  const json *root = static_cast<const json *>(jsonRoot);
  if (!root || !root->is_object() || !root->contains("sets") ||
      !(*root)["sets"].is_array())
  {
    std::cerr << "Audio: sound manifest missing 'sets' array" << std::endl;
    return false;
  }
  Sets.clear();
  for (const auto &entry : (*root)["sets"])
  {
    if (!entry.is_object() || !entry.contains("name") ||
        !entry["name"].is_string())
    {
      continue;
    }
    SoundSetDef set;
    set.Name = entry["name"].get<std::string>();
    set.Bus = ParseBus(entry.value("bus", std::string("sfx")),
                       AudioBus::Sfx);
    set.BaseGain = entry.value("gain", 1.f);
    set.BaseDistance = entry.value("distance", 0.f);
    set.MinIntervalSec = entry.value("min_interval_sec", 0.f);
    set.Loop = entry.value("loop", false);
    if (entry.contains("files") && entry["files"].is_array())
    {
      for (const auto &file : entry["files"])
      {
        if (file.is_string())
        {
          set.Files.push_back(file.get<std::string>());
        }
      }
    }
    if (!set.Files.empty())
    {
      Sets.push_back(std::move(set));
    }
  }
  return true;
}

const SoundSetDef *USoundManifest::FindByName(const std::string &name) const
{
  for (const SoundSetDef &set : Sets)
  {
    if (set.Name == name)
    {
      return &set;
    }
  }
  return nullptr;
}

} // namespace cutum

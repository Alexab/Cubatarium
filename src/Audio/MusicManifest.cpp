#include "Audio/MusicManifest.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{

using json = nlohmann::json;

} // namespace

bool UMusicManifest::LoadFromFile(const std::string &path)
{
  std::ifstream file(path);
  if (!file.is_open())
  {
    std::cerr << "Audio: music manifest not found: " << path << std::endl;
    return false;
  }
  try
  {
    json root = json::parse(file);
    return Load(&root);
  }
  catch (const json::exception &e)
  {
    std::cerr << "Audio: music manifest parse error in " << path << ": "
              << e.what() << std::endl;
    return false;
  }
}

bool UMusicManifest::Load(const void *jsonRoot)
{
  const json *root = static_cast<const json *>(jsonRoot);
  if (!root || !root->is_object() || !root->contains("contexts") ||
      !(*root)["contexts"].is_object())
  {
    std::cerr << "Audio: music manifest missing 'contexts' object"
              << std::endl;
    return false;
  }
  Contexts.clear();
  CrossfadeSeconds = root->value("crossfade_seconds", 3.f);
  for (auto it = (*root)["contexts"].begin();
       it != (*root)["contexts"].end(); ++it)
  {
    if (!it.value().is_array())
    {
      continue;
    }
    MusicPlaylistDef def;
    def.Context = it.key();
    for (const auto &file : it.value())
    {
      if (file.is_string())
      {
        def.Files.push_back(file.get<std::string>());
      }
    }
    if (!def.Files.empty())
    {
      Contexts.push_back(std::move(def));
    }
  }
  return !Contexts.empty();
}

const MusicPlaylistDef *UMusicManifest::Match(const std::string &biomeId) const
{
  const MusicPlaylistDef *best = nullptr;
  std::size_t bestLen = 0;
  const MusicPlaylistDef *fallback = nullptr;
  for (const MusicPlaylistDef &def : Contexts)
  {
    if (def.Context == "overworld")
    {
      fallback = &def;
      continue;
    }
    if (!biomeId.empty() &&
        biomeId.find(def.Context) != std::string::npos &&
        def.Context.size() > bestLen)
    {
      best = &def;
      bestLen = def.Context.size();
    }
  }
  return best ? best : fallback;
}

} // namespace cutum

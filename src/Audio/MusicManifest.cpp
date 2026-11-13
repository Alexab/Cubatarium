#include "Audio/MusicManifest.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{

using json = nlohmann::json;

/// Letters and digits of the file name only, so punctuation and parent
/// folders do not keep a downloaded track from matching the manifest.
std::string MusicFileKey(const std::string &path)
{
  std::string name = path;
  const auto slash = name.find_last_of("/\\");
  if (slash != std::string::npos)
  {
    name = name.substr(slash + 1);
  }
  std::string key;
  key.reserve(name.size());
  for (unsigned char ch : name)
  {
    if (std::isalnum(ch) != 0)
    {
      key.push_back(static_cast<char>(std::tolower(ch)));
    }
  }
  return key;
}

const MusicPlaylistDef *FindContext(const std::vector<MusicPlaylistDef> &contexts,
                                    const std::string &name)
{
  for (const MusicPlaylistDef &def : contexts)
  {
    if (def.Context == name)
    {
      return &def;
    }
  }
  return nullptr;
}

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

MusicSelection UMusicManifest::Select(const std::vector<std::string> &availableFiles,
                                      const std::string &biomeId,
                                      bool isNight) const
{
  std::unordered_set<std::string> manifestKeys;
  for (const MusicPlaylistDef &def : Contexts)
  {
    for (const std::string &file : def.Files)
    {
      const std::string key = MusicFileKey(file);
      if (!key.empty())
      {
        manifestKeys.insert(key);
      }
    }
  }

  std::unordered_map<std::string, std::string> keyToPath;
  std::vector<std::string> unmatched;
  for (const std::string &path : availableFiles)
  {
    const std::string key = MusicFileKey(path);
    if (key.empty())
    {
      continue;
    }
    if (manifestKeys.find(key) == manifestKeys.end())
    {
      unmatched.push_back(path);
      continue;
    }
    if (keyToPath.find(key) == keyToPath.end())
    {
      keyToPath.emplace(key, path);
    }
  }

  auto take = [&](const MusicPlaylistDef *def)
  {
    std::vector<std::string> files;
    if (!def)
    {
      return files;
    }
    std::unordered_set<std::string> seen;
    for (const std::string &name : def->Files)
    {
      const auto found = keyToPath.find(MusicFileKey(name));
      if (found == keyToPath.end())
      {
        continue;
      }
      if (seen.insert(found->second).second)
      {
        files.push_back(found->second);
      }
    }
    return files;
  };

  if (isNight)
  {
    const std::vector<std::string> night = take(FindContext(Contexts, "night"));
    if (!night.empty())
    {
      MusicSelection selection;
      selection.Context = "night";
      selection.Files = night;
      return selection;
    }
  }

  // "night" is a time-of-day playlist, not a biome family.
  const MusicPlaylistDef *best = nullptr;
  std::size_t bestLen = 0;
  const MusicPlaylistDef *fallback = FindContext(Contexts, "overworld");
  for (const MusicPlaylistDef &def : Contexts)
  {
    if (def.Context == "overworld" || def.Context == "night")
    {
      continue;
    }
    if (!biomeId.empty() && biomeId.find(def.Context) != std::string::npos &&
        def.Context.size() > bestLen)
    {
      best = &def;
      bestLen = def.Context.size();
    }
  }

  MusicSelection selection;
  if (best)
  {
    selection.Context = best->Context;
    selection.Files = take(best);
    return selection;
  }

  selection.Context = "overworld";
  selection.Files = take(fallback);
  for (const std::string &extra : unmatched)
  {
    selection.Files.push_back(extra);
  }
  return selection;
}

} // namespace cutum

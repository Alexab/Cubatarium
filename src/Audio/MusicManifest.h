#pragma once

#include <string>
#include <vector>

namespace cutum
{

struct MusicPlaylistDef
{
  std::string Context; // "overworld", "forest", "caves", "night"...
  std::vector<std::string> Files; // file names under the music root
};

/// Parse-only manifest for content/audio/music_manifest.json. Contexts
/// resolve against a biome id by longest contained substring; "overworld"
/// is the fallback. Device-free so unit tests cover it.
class UMusicManifest
{
public:
  bool LoadFromFile(const std::string &path);
  bool Load(const void *jsonRoot);

  float GetCrossfadeSeconds() const { return CrossfadeSeconds; }

  /// Longest contained context key wins ("dense_forest" → "forest");
  /// @p biomeId empty → fallback. Returns nullptr when nothing matches.
  const MusicPlaylistDef *Match(const std::string &biomeId) const;

  const std::vector<MusicPlaylistDef> &GetContexts() const
  {
    return Contexts;
  }

private:
  std::vector<MusicPlaylistDef> Contexts;
  float CrossfadeSeconds{3.f};
};

} // namespace cutum

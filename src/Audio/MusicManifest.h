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

/// Playlist the director should play for the current world. Files are paths
/// relative to the music root (the names that actually exist on disk).
/// An empty Files list means the music bus stays silent; biome ambience is
/// a separate layer and is not filled in from another context.
struct MusicSelection
{
  std::string Context;
  std::vector<std::string> Files;
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

  /// Bind manifest names to files that exist. Names match if their letters
  /// and digits are the same, so a khinsider "1-08. Title.mp3" satisfies
  /// "1-08 Title.mp3". A biome context with no resolved files stays empty.
  /// Unmatched files are added only to the overworld selection. Night is
  /// used only when @p isNight and that playlist resolved at least one file.
  MusicSelection Select(const std::vector<std::string> &availableFiles,
                        const std::string &biomeId, bool isNight) const;

  const std::vector<MusicPlaylistDef> &GetContexts() const
  {
    return Contexts;
  }

private:
  std::vector<MusicPlaylistDef> Contexts;
  float CrossfadeSeconds{3.f};
};

} // namespace cutum

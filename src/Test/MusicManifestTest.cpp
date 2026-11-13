#include "Audio/MusicManifest.h"

#include <iostream>
#include <nlohmann/json.hpp>

namespace
{

constexpr const char *kTestName = "music_manifest_test";

void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << kTestName << " FAIL: " << msg << "\n";
    std::exit(1);
  }
}

} // namespace

int main()
{
  const nlohmann::json root = nlohmann::json::parse(R"json({
    "crossfade_seconds": 2.5,
    "contexts": {
      "overworld": ["a.mp3", "b.mp3"],
      "forest": ["f.mp3"],
      "caves": ["c1.mp3", "c2.mp3", "c3.mp3"],
      "night": ["n.mp3"]
    }
  })json");

  cutum::UMusicManifest manifest;
  Expect(manifest.Load(&root), "manifest parses");
  Expect(manifest.GetContexts().size() == 4, "four contexts");
  Expect(manifest.GetCrossfadeSeconds() == 2.5f, "crossfade read");

  // Longest containment wins; sub-biome ids embed their family.
  Expect(manifest.Match("dense_forest") &&
             manifest.Match("dense_forest")->Context == "forest",
         "sub-biome matches family");
  Expect(manifest.Match("dripstone_caves") &&
             manifest.Match("dripstone_caves")->Context == "caves",
         "caves substring");
  Expect(manifest.Match("") && manifest.Match("")->Context == "overworld",
         "empty id falls back to overworld");
  Expect(manifest.Match("savanna") &&
             manifest.Match("savanna")->Context == "overworld",
         "unknown biome falls back");
  Expect(manifest.Match("forest") && manifest.Match("forest")->Files.size() == 1,
         "forest playlist files");

  // Malformed manifests fail cleanly.
  cutum::UMusicManifest broken;
  Expect(!broken.Load(&nlohmann::json::parse(R"json({"contexts": []})json")),
         "missing contexts object rejected");

  // Available files bind by letters and digits. A biome with none of its
  // own tracks stays silent instead of borrowing the overworld playlist.
  const nlohmann::json pickup = nlohmann::json::parse(R"json({
    "contexts": {
      "overworld": ["1-08 Theme of Prontera.mp3"],
      "forest": ["1-03 Peaceful Forest.mp3"],
      "desert": ["1-24 Desert.mp3"],
      "caves": ["1-19 Under the ground.mp3"],
      "night": ["1-07 Wind of Tragedy.mp3"]
    }
  })json");
  cutum::UMusicManifest music;
  Expect(music.Load(&pickup), "pickup manifest parses");
  const std::vector<std::string> disk = {
      "1-08. Theme of Prontera.mp3", "album/extra theme.mp3"};
  const cutum::MusicSelection menu = music.Select(disk, "", false);
  Expect(menu.Context == "overworld", "menu uses overworld");
  Expect(menu.Files.size() == 2, "overworld keeps unmatched files");
  Expect(menu.Files[0] == "1-08. Theme of Prontera.mp3",
         "khinsider dot still matches");
  Expect(menu.Files[1] == "album/extra theme.mp3",
         "extra file stays on overworld");

  const cutum::MusicSelection forest = music.Select(disk, "dense_forest", false);
  Expect(forest.Context == "forest", "forest context chosen");
  Expect(forest.Files.empty(), "forest does not borrow overworld");

  const cutum::MusicSelection nightFallback =
      music.Select(disk, "savanna", true);
  Expect(nightFallback.Context == "overworld" && nightFallback.Files.size() == 2,
         "night without files keeps the biome playlist");

  const std::vector<std::string> withNight = {
      "1-08. Theme of Prontera.mp3", "1-07 Wind of Tragedy.mp3"};
  const cutum::MusicSelection atNight = music.Select(withNight, "forest", true);
  Expect(atNight.Context == "night" && atNight.Files.size() == 1 &&
             atNight.Files[0] == "1-07 Wind of Tragedy.mp3",
         "night playlist plays when its files exist");

  std::cout << kTestName << " OK\n";
  return 0;
}

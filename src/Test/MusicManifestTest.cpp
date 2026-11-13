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

  std::cout << kTestName << " OK\n";
  return 0;
}

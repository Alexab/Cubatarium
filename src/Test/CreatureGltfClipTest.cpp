#include "Creatures/Visual/Gltf/CreatureGltfBonePalette.h"
#include "Creatures/Visual/Gltf/CreatureGltfClipClock.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "creature_gltf_clip_test: " << message << std::endl;
    std::exit(1);
  }
}

static bool Near(float a, float b, float eps = 1e-4f)
{
  return std::abs(a - b) < eps;
}

int main()
{
  using namespace cutum;

  // Wrap: looped clips wrap, one-shot clips clamp, empty clips stay at 0.
  Expect(Near(WrapGltfClipTime(2.5f, 1.0f, true), 0.5f), "loop wraps");
  Expect(Near(WrapGltfClipTime(-0.25f, 1.0f, true), 0.75f), "negative wraps");
  Expect(Near(WrapGltfClipTime(2.5f, 1.0f, false), 1.0f), "one-shot clamps");
  Expect(Near(WrapGltfClipTime(7.0f, 0.0f, true), 0.0f), "empty clip stays 0");
  // Hours of play stay inside the clip (no unbounded float growth).
  {
    UGltfClipClock clock;
    for (int i = 0; i < 200000; ++i)
    {
      clock.Update("walk", 1.0f / 60.0f, 1.0f, 0.8f, true);
    }
    Expect(clock.GetTime() >= 0.f && clock.GetTime() < 0.8f,
           "long playback stays wrapped");
  }

  // Clip switches: walk resumes its phase after idle, the left clip fades.
  {
    UGltfClipClock clock;
    clock.Update("walk", 0.3f, 1.0f, 1.0f, true);
    Expect(Near(clock.GetTime(), 0.3f), "walk advanced");
    clock.Update("idle", 0.1f, 1.0f, 2.0f, true);
    Expect(clock.GetFadeClip() == "walk" && Near(clock.GetFadeTime(), 0.3f),
           "switch fades out the walk pose where it was");
    Expect(Near(clock.GetFadeWeight(), 1.0f), "fade starts at full weight");
    clock.Update("idle", 0.075f, 1.0f, 2.0f, true);
    Expect(Near(clock.GetFadeWeight(), 0.5f), "half way through the fade");
    clock.Update("idle", 0.1f, 1.0f, 2.0f, true);
    Expect(clock.GetFadeClip().empty() && Near(clock.GetFadeWeight(), 0.0f),
           "fade over after 0.15 s");
    clock.Update("walk", 0.1f, 1.0f, 1.0f, true);
    Expect(Near(clock.GetTime(), 0.4f),
           "walk resumes its own phase instead of restarting at 0");
    // One-shot clips restart.
    clock.Update("attack", 0.5f, 1.0f, 0.4f, false);
    Expect(Near(clock.GetTime(), 0.4f), "one-shot clamps at its end");
    clock.Update("walk", 0.0f, 1.0f, 1.0f, true);
    clock.Update("attack", 0.1f, 1.0f, 0.4f, false);
    Expect(Near(clock.GetTime(), 0.1f), "one-shot restarts from 0");
  }

  // Palette: two joints, node 0 keyed over 1 s, node 1 only over 0.5 s.
  CreatureGltfMeshAsset asset;
  asset.nodes.resize(2);
  asset.hasSkin = true;
  asset.skin.jointNodes = {0, 1};
  GltfAnimationCpu walk;
  walk.name = "walk";
  GltfAnimationChannelCpu ch0;
  ch0.nodeIndex = 0;
  ch0.path = "translation";
  ch0.keyTimes = {0.0f, 1.0f};
  ch0.keyVec3 = {glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)};
  GltfAnimationChannelCpu ch1;
  ch1.nodeIndex = 1;
  ch1.path = "translation";
  ch1.keyTimes = {0.0f, 0.5f};
  ch1.keyVec3 = {glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 2.0f)};
  walk.channels = {ch0, ch1};
  Expect(Near(GltfAnimationDuration(walk), 1.0f), "clip duration is the longest channel");

  const auto at = ComputeGltfSkinMatrices(asset, &walk, 0.75f, true);
  Expect(Near(at[0][3].x, 0.75f), "node 0 at 0.75 s");
  Expect(Near(at[1][3].z, 2.0f),
         "short channel holds its last key instead of wrapping on its own");
  const auto wrapped = ComputeGltfSkinMatrices(asset, &walk, 1.25f, true);
  Expect(Near(wrapped[0][3].x, 0.25f) && Near(wrapped[1][3].z, 1.0f),
         "the whole clip wraps together");

  // Blend: halfway between walk at 0.5 s and the rest pose.
  const auto blended = ComputeGltfSkinMatricesBlended(asset, nullptr, 0.0f,
                                                      &walk, 0.5f, 0.5f);
  Expect(Near(blended[0][3].x, 0.25f) && Near(blended[1][3].z, 1.0f),
         "fade mixes node transforms by weight");
  const auto noFade =
      ComputeGltfSkinMatricesBlended(asset, &walk, 0.5f, nullptr, 0.f, 0.f);
  Expect(Near(noFade[0][3].x, 0.5f), "zero weight is the active clip only");

  std::cout << "creature_gltf_clip_test: ok" << std::endl;
  return 0;
}

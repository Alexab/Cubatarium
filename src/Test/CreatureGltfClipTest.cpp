#include "Creatures/Visual/Gltf/CreatureGltfBonePalette.h"
#include "Creatures/Visual/Gltf/CreatureGltfClipClock.h"
#include "Creatures/Visual/Gltf/GltfDistancePlayback.h"
#include "Creatures/Visual/Gltf/GltfKeyframeInterval.h"
#include "Gui/Preview/CreatureGltfPreviewBounds.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

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
  size_t intervalChecks = 0;
  {
    // Integrated world travel, including tiny speeds and actual pauses, must
    // agree with the wrapped gait phase. A minimum playback floor fails this.
    const float duration=1.96f,distance=5.39f;
    UGltfClipClock clock;double travelled=0.;double maxWorldError=0.;
    for(int i=0;i<20000;++i)
    {
      const float speeds[]={0.f,.01f,.19f,.475f,.95f,2.2f,3.6f};
      const float steps[]={.005f,.016f,.033f};
      const float speed=speeds[(i/13)%7],dt=steps[i%3];
      clock.Update("walk",dt,GltfDistancePlayback(speed,duration,distance),duration,true);
      travelled+=static_cast<double>(speed)*dt;
      const double expected=std::fmod(travelled,static_cast<double>(distance));
      double error=std::abs(clock.GetTime()/duration*distance-expected);
      error=std::min(error,static_cast<double>(distance)-error);
      maxWorldError=std::max(maxWorldError,error);
    }
    Expect(maxWorldError<.001,"distance gait conserves variable travel within1mm across pauses/loops");
    Expect(GltfDistancePlayback(0.f,duration,distance)==0.f,"stopped distance gait does not walk in place");
    Expect(GltfDistancePlayback(.01f,duration,distance)>0.f,"tiny moving speed has no arbitrary dead zone");
    Expect(GltfDistancePlayback(1.f,0.f,distance)==0.f,"empty distance clip stays stopped");
    Expect(GltfDistancePlayback(1.f,duration,-1.f)==0.f,"invalid cycle distance fails closed");
    std::cout<<"distance_travel_max_error_blocks="<<maxWorldError<<std::endl;
  }

  // Exact bracket equivalence, including timestamps and adjacent floats.
  // Picking the next interval at an exact key can change SLERP rounding.
  {
    const auto reference = [](const std::vector<float> &times, float t)
    {
      if (times.empty()) return size_t(0);
      size_t i = 1;
      while (i < times.size() && times[i] < t) ++i;
      return i;
    };
    uint32_t random = 123456789u;
    for (int distribution = 0; distribution < 9; ++distribution)
    for (size_t count : {0u, 1u, 2u, 3u, 7u, 8u, 9u, 15u, 16u, 17u,
                        31u, 32u, 33u, 57u, 65u, 73u, 127u, 128u, 129u,
                        257u, 513u, 2049u, 4097u})
    {
      std::vector<float> times;
      float value = .125f;
      for (size_t i = 0; i < count; ++i)
      {
        switch (distribution)
        {
        case 0: value += .001f + static_cast<float>(i % 7) * .013f; break;
        case 1: value = -.5f + static_cast<float>(i) * .015625f; break;
        case 2: value += i < count*3/4 ? .0001f : 10.f; break;
        case 3: value += i < count/4 ? 10.f : .0001f; break;
        case 4: value = static_cast<float>(i/4) * .125f; break;
        case 5: value = .125f; break;
        case 6: value = std::numeric_limits<float>::denorm_min() * static_cast<float>(i); break;
        case 7: value = std::numeric_limits<float>::max() *
                         (.125f + static_cast<float>(i) / (4.f*static_cast<float>(count))); break;
        case 8: value = i == 0 ? -std::numeric_limits<float>::infinity() :
                         i+1 == count ? std::numeric_limits<float>::infinity() : static_cast<float>(i)*.02f; break;
        }
        times.push_back(value);
      }
      const auto check = [&](float t)
      {
        ++intervalChecks;
        if (GltfKeyframeUpperIndex(times, t) != reference(times, t))
        {
          std::cerr << "distribution=" << distribution << " keys=" << count << " time=" << t << '\n';
          Expect(false, "key interval equals reference scan");
        }
      };
      for (float key : times)
      {
        check(key);
        check(std::nextafter(key, -std::numeric_limits<float>::infinity()));
        check(std::nextafter(key, std::numeric_limits<float>::infinity()));
      }
      check(-1.f); check(1000.f);
      check(std::numeric_limits<float>::infinity());
      check(-std::numeric_limits<float>::infinity());
      check(std::numeric_limits<float>::quiet_NaN());
      for (int i = 0; i < 1000; ++i)
      {
        random = random * 1664525u + 1013904223u;
        check((static_cast<float>(random % 100001u) / 50000.f - .5f) * value);
      }
    }
  }

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

  // In-place palette with reused scratch matches the allocating path, also
  // through a 3-level parent chain (memoized globals) and on reuse.
  {
    CreatureGltfMeshAsset chain;
    chain.nodes.resize(3);
    chain.nodes[1].parent = 0;
    chain.nodes[2].parent = 1;
    chain.nodes[0].translation = glm::vec3(1.0f, 0.0f, 0.0f);
    chain.nodes[1].translation = glm::vec3(0.0f, 2.0f, 0.0f);
    chain.nodes[2].translation = glm::vec3(0.0f, 0.0f, 3.0f);
    chain.hasSkin = true;
    chain.skin.jointNodes = {2, 0, 1};
    GltfSkinScratch scratch;
    std::vector<glm::mat4> palette;
    for (int pass = 0; pass < 2; ++pass)
    {
      ComputeGltfSkinMatricesInto(chain, nullptr, 0.f, nullptr, 0.f, 0.f,
                                  scratch, palette);
      const auto reference =
          ComputeGltfSkinMatricesBlended(chain, nullptr, 0.f, nullptr, 0.f, 0.f);
      Expect(palette.size() == 3 && reference.size() == 3, "palette sizes");
      for (size_t j = 0; j < 3; ++j)
      {
        Expect(palette[j] == reference[j], "in-place palette matches");
      }
      Expect(Near(palette[0][3].x, 1.0f) && Near(palette[0][3].y, 2.0f) &&
                 Near(palette[0][3].z, 3.0f),
             "leaf joint accumulates its whole parent chain");
    }
    ComputeGltfSkinMatricesInto(asset, &walk, 0.75f, nullptr, 0.f, 0.f,
                                scratch, palette);
    Expect(palette.size() == 2 && Near(palette[0][3].x, 0.75f),
           "scratch reused for a different (smaller) skin");
  }

  {
    CreatureGltfMeshAsset wing;
    wing.nodes.resize(1);wing.hasSkin=true;
    wing.skin.jointNodes={0};wing.skin.inverseBindMatrices={glm::mat4(1.f)};
    GltfPrimitiveCpu primitive;
    primitive.skinned=true;
    primitive.mesh.interleavedPosUv={0.f,-2.f,0.f,0.f,0.f};
    primitive.jointIndices={0,0,0,0};primitive.jointWeights={1.f,0.f,0.f,0.f};
    wing.primitives.push_back(primitive);
    GltfAnimationCpu beat;beat.name="fly";
    GltfAnimationChannelCpu channel;
    channel.nodeIndex=0;channel.path="rotation";channel.keyTimes={0.f,.37f,1.f};
    channel.keyQuat={glm::quat(1.f,0.f,0.f,0.f),
                    glm::angleAxis(1.57079632679f,glm::vec3(0.f,0.f,1.f)),
                    glm::quat(1.f,0.f,0.f,0.f)};
    beat.channels.push_back(channel);
    const auto rest=ComputeGltfPreviewBounds(wing,&beat,false);
    const auto cycle=ComputeGltfPreviewBounds(wing,&beat,true);
    Expect(rest && cycle,"preview geometry envelope exists");
    Expect(Near(rest->second.x,0.f),"folded wing does not force icon zoom out");
    Expect(cycle->second.x>1.99f && cycle->first.y < -1.99f,
           "cycle envelope contains both folded wing and off-grid peak");
    Expect(!ComputeGltfPreviewBounds(CreatureGltfMeshAsset{},nullptr,false),
           "empty geometry has no artificial envelope");
  }
  {
    // A root-only model can be much larger than its gameplay collider and
    // translate/rotate/scale at off-grid keys. Its full geometry still has
    // to fit, with no artificial skin required just for a preview.
    CreatureGltfMeshAsset rootOnly;
    GltfPrimitiveCpu primitive;
    primitive.mesh.interleavedPosUv={-3.f,-2.f,-1.f,0.f,0.f, 3.f,2.f,1.f,1.f,1.f};
    rootOnly.primitives.push_back(primitive);
    GltfAnimationCpu move;move.name="walk";
    GltfAnimationChannelCpu translation;
    translation.path="translation";translation.keyTimes={0.f,.37f,1.f};
    translation.keyVec3={glm::vec3(0.f),glm::vec3(8.f,0.f,0.f),glm::vec3(0.f)};
    move.channels.push_back(translation);
    const auto rest=ComputeGltfPreviewBounds(rootOnly,&move,false);
    const auto cycle=ComputeGltfPreviewBounds(rootOnly,&move,true);
    Expect(rest && cycle,"root-only geometry envelope exists");
    Expect(Near(rest->first.x,-3.f) && Near(rest->second.x,3.f),
           "root-only rest envelope uses actual mesh rather than collider");
    Expect(Near(cycle->second.x,11.f) && Near(cycle->first.x,-3.f),
           "root-only off-grid translated geometry is retained in cycle fit");
  }
  std::cout << "creature_gltf_clip_test: ok, interval_checks=" << intervalChecks << std::endl;
  return 0;
}

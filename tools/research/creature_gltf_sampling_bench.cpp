// Release-only research benchmark of actual loader/palette/root implementations.
// Batch averages are not individual world-frame p95 or GPU timings.
#include "Creatures/Visual/Gltf/CreatureGltfLoader.h"
#include "Creatures/Visual/Gltf/CreatureGltfBonePalette.h"
#include "Creatures/Visual/Gltf/CreatureGltfAnimPlayer.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace cutum;
using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;
static volatile float consumed = 0.f;

static Json Distribution(std::vector<double> values)
{
  std::sort(values.begin(), values.end());
  const auto q = [&](double f) { return values[static_cast<size_t>(f * (values.size() - 1))]; };
  return {{"samples", values.size()}, {"min", values.front()}, {"p50", q(.5)}, {"p95", q(.95)}, {"max", values.back()}};
}

static uint64_t HashMatrix(uint64_t hash, const glm::mat4 &m)
{
  for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
  {
    uint32_t bits; std::memcpy(&bits, &m[c][r], sizeof(bits));
    hash ^= bits; hash *= 1099511628211ull;
  }
  return hash;
}

// Accounted heap-owned vector/string capacity, excluding SSO strings, map,
// allocator/control-block overhead, total RSS and GPU memory.
static size_t HeapBytes(const CreatureGltfMeshAsset &a)
{
  const auto stringHeap = [](const std::string &s)
  {
    const auto data = reinterpret_cast<uintptr_t>(s.data());
    const auto object = reinterpret_cast<uintptr_t>(&s);
    return data >= object && data < object + sizeof(s) ? size_t(0) : s.capacity() + 1;
  };
  size_t bytes = a.nodes.capacity() * sizeof(GltfNodeCpu) + a.animations.capacity() * sizeof(GltfAnimationCpu)
    + a.primitives.capacity() * sizeof(GltfPrimitiveCpu) + a.skin.jointNodes.capacity() * sizeof(int)
    + a.skin.inverseBindMatrices.capacity() * sizeof(glm::mat4);
  for (const auto &n : a.nodes) bytes += stringHeap(n.name);
  for (const auto &p : a.primitives)
    bytes += p.mesh.interleavedPosUv.capacity() * sizeof(float) + p.mesh.indices.capacity() * sizeof(unsigned int)
      + p.jointIndices.capacity() + p.jointWeights.capacity() * sizeof(float) + stringHeap(p.textureStem);
  for (const auto &animation : a.animations)
  {
    bytes += stringHeap(animation.name) + animation.channels.capacity() * sizeof(GltfAnimationChannelCpu);
    for (const auto &c : animation.channels)
      bytes += stringHeap(c.path) + c.keyTimes.capacity() * sizeof(float) + c.keyVec3.capacity() * sizeof(glm::vec3)
        + c.keyQuat.capacity() * sizeof(glm::quat);
  }
  return bytes;
}

static Json BenchAsset(const std::string &path)
{
  auto asset = CreatureGltfLoader::LoadFromFile(path);
  if (!asset || !asset->loaded || !asset->hasSkin) throw std::runtime_error("Skinned asset required: " + path);
  std::vector<double> loads;
  for (int i = 0; i < 13; ++i)
  {
    const auto begin = Clock::now();
    auto next = CreatureGltfLoader::LoadFromFile(path);
    const auto end = Clock::now();
    if (!next || !next->loaded) throw std::runtime_error("Reload failed");
    consumed = consumed + next->bindMinY;
    loads.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
  }
  Json result = {{"path", path}, {"nodes", asset->nodes.size()}, {"joints", asset->skin.jointNodes.size()},
    {"bin_bytes", std::filesystem::file_size(std::filesystem::path(path).parent_path() / "model.bin")},
    {"accounted_vector_string_heap_bytes", HeapBytes(*asset)}, {"warm_loader_milliseconds", Distribution(loads)},
    {"cases", Json::array()}};
  const auto &idle = asset->animations.at(asset->animationIndexByName.at("idle"));
  for (const std::string clipName : {"walk", "run"}) for (bool fade : {false, true})
  {
    const auto &clip = asset->animations.at(asset->animationIndexByName.at(clipName));
    const float duration = GltfAnimationDuration(clip), idleDuration = GltfAnimationDuration(idle);
    GltfSkinScratch scratch; std::vector<glm::mat4> palette;
    const auto compute = [&](size_t i)
    {
      const float phase = static_cast<float>((i * 73 + 19) % 997) / 997.f;
      ComputeGltfSkinMatricesInto(*asset, &clip, phase * duration, fade ? &idle : nullptr,
        phase * idleDuration, fade ? .5f : 0.f, scratch, palette);
      consumed = consumed + palette.front()[3].x * 1e-9f;
    };
    for (size_t i = 0; i < 1024; ++i) compute(i);
    std::vector<double> batches;
    constexpr size_t count = 128;
    for (size_t batch = 0; batch < 81; ++batch)
    {
      const auto begin = Clock::now();
      for (size_t i = 0; i < count; ++i) compute(batch * count + i);
      batches.push_back(std::chrono::duration<double, std::micro>(Clock::now() - begin).count() / count);
    }
    uint64_t signature = 1469598103934665603ull;
    // Includes exact keys, non-key times, and both fade/no-fade states.
    for (size_t i = 0; i <= 1024; ++i)
    {
      const float phase = static_cast<float>(i) / 1024.f;
      ComputeGltfSkinMatricesInto(*asset, &clip, phase * duration, fade ? &idle : nullptr,
        phase * idleDuration, fade ? .5f : 0.f, scratch, palette);
      for (const auto &m : palette) signature = HashMatrix(signature, m);
    }
    size_t keys = 0, maximum = 0;
    for (const auto &c : clip.channels) { keys += c.keyTimes.size(); maximum = std::max(maximum, c.keyTimes.size()); }
    result["cases"].push_back({{"clip", clipName}, {"fade", fade}, {"key_times", keys}, {"max_keys_per_channel", maximum},
      {"duration", duration}, {"batch_average_microseconds_per_pose", Distribution(batches)}, {"palette_signature", signature}});
  }
  // Root-only sampler path, independently signed at exact/adjacent key times.
  uint64_t root = 1469598103934665603ull;
  for (const auto &animation : asset->animations)
  {
    const float duration = GltfAnimationDuration(animation);
    for (int i = 0; i <= 512; ++i) root = HashMatrix(root, SampleGltfRootTransform(animation, duration * i / 512.f, false));
  }
  result["root_sampler_signature"] = root;
  return result;
}

int main(int argc, char **argv)
{
  try
  {
    if (argc < 3) throw std::runtime_error("usage: bench OUTPUT.json MODEL.gltf ...");
    Json report = {{"scope", "Release actual CPU loader and warmed allocation-reusing pose sampler; warm I/O, batch-average p95, no world/GPU/total RSS acceptance"},
      {"compiler", _MSC_FULL_VER}, {"pointer_bits", sizeof(void *) * 8}, {"resources", Json::array()}};
    for (int i = 2; i < argc; ++i) report["resources"].push_back(BenchAsset(argv[i]));
    std::ofstream out(argv[1]); out << report.dump(2) << '\n';
    if (!out) throw std::runtime_error("Output failed");
    std::cout << "bench resources=" << argc - 2 << " consumed=" << consumed << '\n';
  }
  catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

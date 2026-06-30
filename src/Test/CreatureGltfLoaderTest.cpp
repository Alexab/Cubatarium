#include "Creatures/Visual/Gltf/CreatureGltfBonePalette.h"
#include "Creatures/Visual/Gltf/CreatureGltfLoader.h"
#include "Creatures/Visual/Gltf/CreatureGltfAnimPlayer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{

bool NearlyEqual(float a, float b, float eps = 0.001f)
{
  return std::fabs(a - b) <= eps;
}

bool HasSkinInGltfJson(const std::string &path)
{
  std::ifstream in(path);
  if (!in)
  {
    return false;
  }
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  return content.find("\"skins\"") != std::string::npos;
}

std::vector<std::string> DiscoverSkinnedSpecies()
{
  namespace fs = std::filesystem;
  std::vector<std::string> out;
  const fs::path root = "models/creatures";
  if (!fs::is_directory(root))
  {
    return {"oerkki"};
  }
  for (const auto &entry : fs::directory_iterator(root))
  {
    if (!entry.is_directory())
    {
      continue;
    }
    const fs::path gltf = entry.path() / "model.gltf";
    if (fs::is_regular_file(gltf) && HasSkinInGltfJson(gltf.string()))
    {
      out.push_back(entry.path().filename().string());
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

int TestSpecies(const std::string &species)
{
  const std::string path = "models/creatures/" + species + "/model.gltf";
  auto asset = cutum::CreatureGltfLoader::LoadFromFile(path);
  if (!asset)
  {
    std::cerr << "SKIP " << species << ": no " << path << "\n";
    return 0;
  }

  if (asset->primitives.empty())
  {
    std::cerr << "FAIL " << species << ": no primitives\n";
    return 1;
  }

  if (!asset->hasSkin)
  {
    std::cerr << "FAIL " << species << ": expected skinned glTF\n";
    return 1;
  }
  if (asset->skin.inverseBindMatrices.size() != asset->skin.jointNodes.size())
  {
    std::cerr << "FAIL " << species << ": IBM count "
              << asset->skin.inverseBindMatrices.size() << " != joint count "
              << asset->skin.jointNodes.size() << "\n";
    return 1;
  }

  const std::vector<glm::mat4> bindPose =
      cutum::ComputeGltfSkinMatrices(*asset, nullptr, 0.f, true);
  if (bindPose.empty())
  {
    std::cerr << "FAIL " << species << ": empty bind-pose bone palette\n";
    return 1;
  }

  const auto idleIt = asset->animationIndexByName.find("idle");
  if (idleIt == asset->animationIndexByName.end() && !asset->animations.empty())
  {
    std::cerr << "FAIL " << species << ": missing idle animation\n";
    return 1;
  }

  const auto walkIt = asset->animationIndexByName.find("walk");
  if (walkIt != asset->animationIndexByName.end())
  {
    const cutum::GltfAnimationCpu &walk = asset->animations[walkIt->second];
    if (!walk.channels.empty())
    {
      const std::vector<glm::mat4> t0 =
          cutum::ComputeGltfSkinMatrices(*asset, &walk, 0.f, true);
      const std::vector<glm::mat4> tMid =
          cutum::ComputeGltfSkinMatrices(*asset, &walk, 0.5f, true);
      const std::vector<glm::mat4> tEnd =
          cutum::ComputeGltfSkinMatrices(*asset, &walk, 0.95f, true);
      if (t0.empty())
      {
        std::cerr << "FAIL " << species << ": empty walk palette\n";
        return 1;
      }
      if (t0 == tMid && t0 == tEnd)
      {
        std::cerr << "WARN " << species
                  << ": walk clip samples identical (static KEYS)\n";
      }
    }
  }

  std::cout << "OK " << species << ": joints=" << bindPose.size() << "\n";
  return 0;
}

} // namespace

int main()
{
  const std::vector<std::string> species = DiscoverSkinnedSpecies();
  if (species.empty())
  {
    std::cerr << "SKIP creature_gltf_loader_test: no skinned species under "
                 "models/creatures\n";
    return 0;
  }

  int failures = 0;
  int tested = 0;
  for (const std::string &sp : species)
  {
  tested++;
    failures += TestSpecies(sp);
  }

  if (failures)
  {
    std::cerr << "FAIL creature_gltf_loader_test: " << failures << "/"
              << tested << " species\n";
    return 1;
  }
  std::cout << "OK creature_gltf_loader_test: " << tested
            << " skinned species\n";
  return 0;
}

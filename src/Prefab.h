#ifndef PREFAB_H
#define PREFAB_H

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include "BlockTypes.h"

namespace cutum {

struct PrefabVoxel {
 glm::ivec3 offset;
 BlockId id{BLOCK_AIR};
};

struct Prefab {
 std::string name;
 glm::ivec3 anchor{0};
 std::vector<PrefabVoxel> voxels;
 glm::ivec3 boundsMin{0};
 glm::ivec3 boundsMax{0};
};

class BlockRegistry;

class PrefabLibrary {
public:
 void Load(const std::string& prefabs_folder, BlockRegistry& registry);
 const Prefab* Get(const std::string& name) const;
 std::vector<std::string> ListNames() const;

private:
 bool LoadFile(const std::string& path, BlockRegistry& registry);

 std::unordered_map<std::string, Prefab> prefabs_;
};

}

#endif

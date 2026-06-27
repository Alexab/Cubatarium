#pragma once

#include <glm/glm.hpp>
#include <string>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;

class ULegacyChunkJsonLoader
{
public:
  static void LoadBlocksFile(UBlockWorld &world, UBlockRegistry &registry,
                             const std::string &file_name);
  static void LoadMonolithicChunksFile(UBlockWorld &world,
                                       UBlockRegistry &registry,
                                       const std::string &file_name);

private:
  static bool SetBlockFromTypeName(UBlockWorld &world, UBlockRegistry &registry,
                                   glm::ivec3 worldPos,
                                   const std::string &type);
};

} // namespace cutum

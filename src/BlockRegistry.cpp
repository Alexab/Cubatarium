#include "BlockRegistry.h"
#include "TextureCube.h"

namespace cutum {

BlockRegistry::BlockRegistry(std::shared_ptr<TextureCubeStorage> textures)
 : textures_(std::move(textures))
{
 RebuildMaps();
}

void BlockRegistry::Reload()
{
 RebuildMaps();
}

void BlockRegistry::RebuildMaps()
{
 nameToId_.clear();
 idToName_.clear();
 if (!textures_) {
  return;
 }
 for (const auto& entry : textures_->GetTextures()) {
  const auto& cube = entry.second;
  const BlockId id = static_cast<BlockId>(cube.GetTypeId());
  if (id == BLOCK_AIR) {
   continue;
  }
  nameToId_[cube.GetName()] = id;
  idToName_[id] = cube.GetName();
 }
}

BlockId BlockRegistry::GetIdByTypeName(const std::string& name) const
{
 auto it = nameToId_.find(name);
 if (it != nameToId_.end()) {
  return it->second;
 }
 if (textures_) {
  const size_t id = textures_->GetTypeIdByName(name);
  if (id != 0) {
   return static_cast<BlockId>(id);
  }
 }
 return BLOCK_AIR;
}

const std::string& BlockRegistry::GetTypeNameById(BlockId id) const
{
 static const std::string empty;
 auto it = idToName_.find(id);
 if (it != idToName_.end()) {
  return it->second;
 }
 if (textures_) {
  const auto texIt = textures_->GetTextures().find(static_cast<size_t>(id));
  if (texIt != textures_->GetTextures().end()) {
   return texIt->second.GetName();
  }
 }
 return empty;
}

bool BlockRegistry::IsSolid(BlockId id) const
{
 return id != BLOCK_AIR;
}

size_t BlockRegistry::GetTextureId(BlockId id) const
{
 return static_cast<size_t>(id);
}

}

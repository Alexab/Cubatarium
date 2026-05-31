#include "BlockRegistry.h"
#include "BlockDefinitionStorage.h"
#include "TextureCube.h"

namespace cutum {

BlockRegistry::BlockRegistry(std::shared_ptr<TextureCubeStorage> textures,
                             std::shared_ptr<BlockDefinitionStorage> definitions)
 : textures_(std::move(textures))
 , definitions_(std::move(definitions))
 , solidDefault_(BlockPhysicsProfile::Solid())
{
 RebuildMaps();
}

void BlockRegistry::SetDefinitions(std::shared_ptr<BlockDefinitionStorage> definitions)
{
 definitions_ = std::move(definitions);
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

const BlockPhysicsProfile& BlockRegistry::Physics(BlockId id) const
{
 if (id == BLOCK_AIR) {
  return solidDefault_;
 }
 if (definitions_) {
  if (const BlockDefinition* def = definitions_->GetById(id)) {
   return def->physics;
  }
 }
 return solidDefault_;
}

bool BlockRegistry::BlocksMovement(BlockId id) const
{
 if (id == BLOCK_AIR) {
  return false;
 }
 return Physics(id).movement.occupancy >= 1.0f;
}

bool BlockRegistry::IsSolid(BlockId id) const
{
 return BlocksMovement(id);
}

bool BlockRegistry::IsTransparent(BlockId id) const
{
 if (id == BLOCK_AIR) {
  return true;
 }
 if (definitions_) {
  if (const BlockDefinition* def = definitions_->GetById(id)) {
   return def->render.transparent;
  }
 }
 return false;
}

BlockRenderStyle BlockRegistry::GetRenderStyle(BlockId id) const
{
 if (definitions_) {
  if (const BlockDefinition* def = definitions_->GetById(id)) {
   return def->render.style;
  }
 }
 return BlockRenderStyle::Cube;
}

const FluidViewProfile* BlockRegistry::GetFluidView(BlockId id) const
{
 if (definitions_) {
  if (const BlockDefinition* def = definitions_->GetById(id)) {
   if (def->render.style == BlockRenderStyle::Fluid
       || def->render.style == BlockRenderStyle::Cross
       || def->render.fluidView.overlayAlpha > 0.0f) {
    return &def->render.fluidView;
   }
  }
 }
 return nullptr;
}

size_t BlockRegistry::GetTextureId(BlockId id) const
{
 return static_cast<size_t>(id);
}

}

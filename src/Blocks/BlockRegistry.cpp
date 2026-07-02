#include "Blocks/BlockRegistry.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Render/Textures/TextureCube.h"
#include "ResourcePacks/BlockMergeRegistry.h"

namespace cutum
{

UBlockRegistry::UBlockRegistry(
    std::shared_ptr<UTextureCubeStorage> textures,
    std::shared_ptr<UBlockDefinitionStorage> definitions)
    : Textures(std::move(textures)), Definitions(std::move(definitions)),
      SolidDefault(BlockPhysicsProfile::Solid())
{
  RebuildMaps();
}

void UBlockRegistry::SetMergeRegistry(
    std::shared_ptr<UBlockMergeRegistry> merge_registry)
{
  MergeRegistry = std::move(merge_registry);
  RebuildMaps();
}

void UBlockRegistry::SetDefinitions(
    std::shared_ptr<UBlockDefinitionStorage> definitions)
{
  Definitions = std::move(definitions);
}

void UBlockRegistry::Reload() { RebuildMaps(); }

void UBlockRegistry::RebuildMaps()
{
  NameToId.clear();
  IdToName.clear();
  if (MergeRegistry)
  {
    for (const auto &entry : MergeRegistry->GetNameToId())
    {
      NameToId[entry.first] = entry.second;
      IdToName[entry.second] = entry.first;
    }
    return;
  }
  if (!Textures)
  {
    return;
  }
  for (const auto &entry : Textures->GetTextures())
  {
    const auto &cube = entry.second;
    const BlockId Id = static_cast<BlockId>(cube.GetTypeId());
    if (Id == BLOCK_AIR)
    {
      continue;
    }
    NameToId[cube.GetName()] = Id;
    IdToName[Id] = cube.GetName();
  }
}

BlockId UBlockRegistry::GetIdByTypeName(const std::string &Name) const
{
  if (MergeRegistry)
  {
    return MergeRegistry->ResolveName(Name);
  }
  auto it = NameToId.find(Name);
  if (it != NameToId.end())
  {
    return it->second;
  }
  if (Textures)
  {
    const size_t Id = Textures->GetTypeIdByName(Name);
    if (Id != 0)
    {
      return static_cast<BlockId>(Id);
    }
  }
  return BLOCK_AIR;
}

const std::string &UBlockRegistry::GetTypeNameById(BlockId Id) const
{
  static const std::string empty;
  if (MergeRegistry)
  {
    if (const std::string *name = MergeRegistry->GetTypeNameById(Id))
    {
      return *name;
    }
  }
  auto it = IdToName.find(Id);
  if (it != IdToName.end())
  {
    return it->second;
  }
  if (Textures)
  {
    const auto texIt = Textures->GetTextures().find(static_cast<size_t>(Id));
    if (texIt != Textures->GetTextures().end())
    {
      return texIt->second.GetName();
    }
  }
  return empty;
}

const BlockPhysicsProfile &UBlockRegistry::Physics(BlockId Id) const
{
  if (Id == BLOCK_AIR)
  {
    return SolidDefault;
  }
  if (Definitions)
  {
    if (const BlockDefinition *def = Definitions->GetById(Id))
    {
      return def->Physics;
    }
  }
  return SolidDefault;
}

bool UBlockRegistry::BlocksMovement(BlockId Id) const
{
  if (Id == BLOCK_AIR)
  {
    return false;
  }
  return Physics(Id).Movement.Occupancy >= 1.0f;
}

bool UBlockRegistry::IsSolid(BlockId Id) const { return BlocksMovement(Id); }

bool UBlockRegistry::IsFallingBlock(BlockId Id) const
{
  if (Definitions)
  {
    if (const BlockDefinition *def = Definitions->GetById(Id))
    {
      return def->Physics.Falling;
    }
  }
  return false;
}

bool UBlockRegistry::IsLiquid(BlockId Id) const
{
  if (Definitions)
  {
    if (const BlockDefinition *def = Definitions->GetById(Id))
    {
      return def->Physics.IsLiquid;
    }
  }
  return false;
}

bool UBlockRegistry::IsLiquidRenewable(BlockId Id) const
{
  if (Definitions)
  {
    if (const BlockDefinition *def = Definitions->GetById(Id))
    {
      return def->Physics.LiquidRenewable;
    }
  }
  return false;
}

bool UBlockRegistry::IsFloodable(BlockId Id) const
{
  if (Definitions)
  {
    if (const BlockDefinition *def = Definitions->GetById(Id))
    {
      return def->Physics.Floodable;
    }
  }
  return false;
}

bool UBlockRegistry::IsTransparent(BlockId Id) const
{
  if (Id == BLOCK_AIR)
  {
    return true;
  }
  if (Definitions)
  {
    if (const BlockDefinition *def = Definitions->GetById(Id))
    {
      return def->Render.Transparent;
    }
  }
  return false;
}

BlockRenderStyle UBlockRegistry::GetRenderStyle(BlockId Id) const
{
  if (Definitions)
  {
    if (const BlockDefinition *def = Definitions->GetById(Id))
    {
      return def->Render.Style;
    }
  }
  return BlockRenderStyle::UCube;
}

const FluidViewProfile *UBlockRegistry::GetFluidView(BlockId Id) const
{
  if (Definitions)
  {
    if (const BlockDefinition *def = Definitions->GetById(Id))
    {
      if (def->Render.Style == BlockRenderStyle::Fluid ||
          def->Render.Style == BlockRenderStyle::Cross ||
          def->Render.FluidView.OverlayAlpha > 0.0f)
      {
        return &def->Render.FluidView;
      }
    }
  }
  return nullptr;
}

const BlockAnimationSpec &UBlockRegistry::Animation(BlockId Id) const
{
  if (Definitions)
  {
    if (const BlockDefinition *def = Definitions->GetById(Id))
    {
      return def->Animation;
    }
  }
  return DefaultAnimation;
}

size_t UBlockRegistry::GetTextureId(BlockId Id) const
{
  return static_cast<size_t>(Id);
}

} // namespace cutum

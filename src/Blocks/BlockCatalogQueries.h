#ifndef BLOCKCATALOGQUERIES_H
#define BLOCKCATALOGQUERIES_H

#include "Blocks/BlockDefinition.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "World/Math/BlockTypes.h"

namespace cutum
{

/// Immutable catalog reads for mesh/relight workers (Q4). Prefer these over
/// live UBlockRegistry queries so Reload cannot change mid-job decisions.
inline const BlockDefinition *
FindCatalogDefinition(const BlockDefinitionCatalog *catalog, BlockId id)
{
  if (!catalog || id == BLOCK_AIR)
  {
    return nullptr;
  }
  const auto it = catalog->ById.find(id);
  return it == catalog->ById.end() ? nullptr : &it->second;
}

inline bool CatalogIsTransparent(const BlockDefinitionCatalog *catalog,
                                 BlockId id)
{
  if (id == BLOCK_AIR)
  {
    return true;
  }
  if (const BlockDefinition *def = FindCatalogDefinition(catalog, id))
  {
    return def->Render.Transparent;
  }
  return false;
}

inline BlockRenderStyle CatalogGetRenderStyle(const BlockDefinitionCatalog *catalog,
                                              BlockId id)
{
  if (const BlockDefinition *def = FindCatalogDefinition(catalog, id))
  {
    return def->Render.Style;
  }
  return BlockRenderStyle::UCube;
}

inline int CatalogGetLightEmission(const BlockDefinitionCatalog *catalog,
                                   BlockId id)
{
  if (const BlockDefinition *def = FindCatalogDefinition(catalog, id))
  {
    return def->Lighting.Emission;
  }
  return 0;
}

} // namespace cutum

#endif // BLOCKCATALOGQUERIES_H

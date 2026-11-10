#ifndef CREATUREVISUALBACKENDSELECT_H
#define CREATUREVISUALBACKENDSELECT_H

#include "Creatures/Core/CreatureCatalogTypes.h"

namespace cutum
{

struct CreatureDefinition;

/// True when the definition renders as a camera-facing sprite regardless of
/// the declared backend (the sprite check runs before backend parsing).
bool CreatureUsesBillboardSprite(const CreatureDefinition &def);

/// Backend the definition renders with: the declared backend, or its
/// fallback when the backend's required assets are absent. The fallback
/// itself is not asset-checked (single-step fallback).
CreatureVisualBackend ResolveCreatureVisualBackend(const CreatureDefinition &def);

} // namespace cutum

#endif

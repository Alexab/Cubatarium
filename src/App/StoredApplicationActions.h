#pragma once
#include "WorldGen/Core/ProceduralSettings.h"
#include "ResourcePacks/ResourcePackResolver.h"
#include "World/View/WorldViewSettings.h"
#include "Game/WorldGameMode.h"
#include "Game/WorldDifficulty.h"
namespace cutum {
class UApplication;class UNewWorldScreen;class ULoadWorldScreen;
struct ApplicationReturnMenuAction {UApplication *owner;void operator()() const;};
struct ApplicationShowMenuAction {UApplication *owner;void operator()() const;};
struct ApplicationRetrySaveAction {UApplication *owner;void operator()() const;};
struct ApplicationShutdownCompleteAction {UApplication *owner;void operator()() const;};
// Const fields preserve the original by-value lambda capture types,including
// their potentially throwing copies when a function wrapper moves a target.
struct CreateWorldMenuAction {
  UNewWorldScreen *owner;const ProceduralSettings settings;ResourcePackSelection packs;
  const WorldViewSettings view;const WorldGameMode gameMode;const WorldDifficulty difficulty;
  void operator()() const;
};
struct LoadWorldMenuAction {ULoadWorldScreen *owner;const std::string worldName;void operator()() const;};
} // namespace cutum

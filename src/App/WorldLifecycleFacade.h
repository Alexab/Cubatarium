#ifndef WORLDLIFECYCLEFACADE_H
#define WORLDLIFECYCLEFACADE_H

#include "App/CreateWorldCli.h"
#include "ResourcePacks/ResourcePackResolver.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include <string>
#include <vector>

namespace cutum
{

class UCore;

/// World create/load/save/list operations (extracted from UCore).
class UWorldLifecycleFacade
{
public:
  void CreateWorld(UCore &core, const std::string &terrain_type = "");
  void CreateWorldFromProceduralConfig(UCore &core);
  void CreateNewWorldWithCurrentSettings(UCore &core);

  void LoadWorld(UCore &core, const std::string &world_name);
  void LoadLastWorld(UCore &core);
  void LoadWorldByName(UCore &core, const std::string &world_name);

  void PrepareLoadWorld(UCore &core, const std::string &world_name);
  void FinalizeLoadedWorld(UCore &core);
  void FinalizeEnterGameSession(UCore &core);

  void EnterGameWorld(UCore &core);

  void PrepareEnterGameWorldList(UCore &core);
  void LoadWorldList(UCore &core, const std::string &world_path);
  void RefreshWorldList(UCore &core);
  void SaveWorld(UCore &core, const std::string &world_name);

  void CreateNewWorldWithSettings(UCore &core,
                                  const ProceduralSettings &settings,
                                  const std::vector<std::string> &resourcePacks);
  void CreateNewWorldWithSettings(UCore &core,
                                  const ProceduralSettings &settings,
                                  const ResourcePackSelection &selection);
  bool CreateWorldHeadless(UCore &core, const CreateWorldCliArgs &args,
                           CreateWorldReport &report);

  std::string AllocateNextWorldName(const UCore &core) const;

  std::string SetupNewWorldForCreation(UCore &core);
  void ApplyNewWorldCreationRequest(UCore &core,
                                    const ProceduralSettings &settings,
                                    const ResourcePackSelection &selection);
  void RefreshWorldListAfterSave(UCore &core);
};

} // namespace cutum

#endif

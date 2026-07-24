#ifndef IU_GUI_MENU_HOST_H
#define IU_GUI_MENU_HOST_H

#include "ResourcePacks/ResourcePackResolver.h"
#include "World/View/WorldViewSettings.h"
#include <functional>
#include <string>
#include <vector>

namespace cutum
{

struct AppSettingsSnapshot;
struct ProceduralSettings;
struct InstalledPackInfo;

class IUGuiMenuHost
{
public:
  virtual ~IUGuiMenuHost() = default;

  virtual void ReturnToMainMenu() = 0;
  /// Saves the Active world if a session is running, then runs @p proceed next
  /// frame.
  virtual void SaveIfNeededAndProceed(std::function<void()> proceed) = 0;

  virtual AppSettingsSnapshot LoadAppSettingsSnapshot() const = 0;
  virtual ProceduralSettings LoadProceduralTemplate() const = 0;
  virtual void
  SaveAppAndTemplateSettings(const AppSettingsSnapshot &app,
                             const ProceduralSettings &procedural) = 0;

  virtual void
  CreateNewWorldWithSettings(const ProceduralSettings &settings,
                             const std::vector<std::string> &resourcePacksEnabled) = 0;
  virtual void
  CreateNewWorldWithSettings(const ProceduralSettings &settings,
                             const ResourcePackSelection &selection) = 0;
  virtual void
  CreateNewWorldWithSettings(const ProceduralSettings &settings,
                             const ResourcePackSelection &selection,
                             const WorldViewSettings &view) = 0;
  virtual void LoadSelectedWorld(const std::string &worldName) = 0;
  virtual void RefreshWorldList() = 0;
  virtual const std::vector<std::string> &GetWorldNames() const = 0;
  virtual std::vector<InstalledPackInfo> ListInstalledResourcePacks() const = 0;
  virtual std::vector<std::string> GetDefaultEnabledResourcePacks() const = 0;
  virtual ResourcePackSelection GetDefaultResourcePackSelection() const = 0;
  virtual std::vector<std::string>
  PeekWorldResourcePacks(const std::string &worldName) const = 0;
  virtual ResourcePackSelection GetCurrentWorldResourcePackSelection() const = 0;
  virtual bool
  ApplyResourcePacksToCurrentWorld(const ResourcePackSelection &selection) = 0;
  virtual WorldViewSettings GetCurrentWorldViewSettings() const = 0;
  virtual bool ApplyViewSettingsToCurrentWorld(const WorldViewSettings &view) = 0;
  /// Apply packs + view in memory and persist world_data.json only.
  virtual bool
  ApplyWorldSettings(const ResourcePackSelection &selection,
                     const WorldViewSettings &view) = 0;
  virtual void ApplyLiveUiScale(float user_scale) = 0;
};

} // namespace cutum

#endif

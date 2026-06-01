#ifndef I_GUI_MENU_HOST_H
#define I_GUI_MENU_HOST_H

#include <functional>
#include <string>
#include <vector>

namespace cutum {

struct AppSettingsSnapshot;
struct ProceduralSettings;

class IGuiMenuHost {
public:
    virtual ~IGuiMenuHost() = default;

    virtual void ReturnToMainMenu() = 0;
    virtual void RequestConfirmSaveAndProceed(const std::string& message,
                                              std::function<void()> proceed) = 0;

    virtual AppSettingsSnapshot LoadAppSettingsSnapshot() const = 0;
    virtual ProceduralSettings LoadProceduralTemplate() const = 0;
    virtual void SaveAppAndTemplateSettings(const AppSettingsSnapshot& app,
                                            const ProceduralSettings& procedural) = 0;

    virtual void CreateNewWorldWithSettings(const ProceduralSettings& settings) = 0;
    virtual void LoadSelectedWorld(const std::string& worldName) = 0;
    virtual void RefreshWorldList() = 0;
    virtual const std::vector<std::string>& GetWorldNames() const = 0;
};

} // namespace cutum

#endif

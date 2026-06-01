#ifndef GAME_SESSION_H
#define GAME_SESSION_H

#include "Commands/CommandRegistry.h"
#include "Content/ContentTypeRegistry.h"
#include "Gui/Interfaces/IContentCatalog.h"
#include "Gui/Interfaces/IGameCommandContext.h"
#include "Gui/Interfaces/IGuiGameActions.h"
#include "Gui/Interfaces/IHotbarViewModel.h"
#include <functional>
#include <memory>
#include <vector>

namespace cutum {

class Application;
class World;

class GameSession : public IGuiGameActions,
                    public IHotbarViewModel,
                    public IGameCommandContext {
public:
    GameSession(Application* application, std::shared_ptr<World> world);

    void InitializeCatalog(const std::string& typesJsonPath,
                           const BlockDefinitionStorage& blocks,
                           const PrefabLibrary& prefabs);
    void RegisterCommands();

    CommandRegistry& GetCommandRegistry() { return commandRegistry_; }
    ContentTypeRegistry& GetContentCatalog() { return contentCatalog_; }
    IContentCatalog& AsContentCatalog() { return contentCatalog_; }

    void StartGame() override;
    void QuitApplication() override;
    void OpenSettings() override;

    std::array<HotbarSlotView, 10> GetBlockSlots() const override;
    std::array<HotbarSlotView, 10> GetPrefabSlots() const override;
    size_t GetActiveBlockIndex() const override;
    size_t GetActivePrefabIndex() const override;
    void SelectBlockSlot(size_t index) override;
    void SelectPrefabSlot(size_t index) override;

    CommandResult Execute(const std::vector<std::string>& args) override;
    void AddChatLine(const std::string& line) override;
    const std::vector<std::string>& GetChatLog() const { return chatLog_; }

private:
    Application* application_;
    std::shared_ptr<World> world_;
    CommandRegistry commandRegistry_;
    ContentTypeRegistry contentCatalog_;
    std::vector<std::string> chatLog_;
};

} // namespace cutum

#endif

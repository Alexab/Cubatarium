#ifndef GAME_SESSION_H
#define GAME_SESSION_H

#include "Commands/CommandRegistry.h"
#include "ConsoleCommandHistory.h"
#include "Content/ContentTypeRegistry.h"
#include "Gui/Interfaces/IContentCatalog.h"
#include "Gui/Interfaces/IGameCommandContext.h"
#include "Gui/Interfaces/IGuiGameActions.h"
#include "Gui/Interfaces/IHotbarViewModel.h"
#include "Gui/Interfaces/IInventoryViewModel.h"
#include "SlotInteraction.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace cutum {

class Application;
class World;

class GameSession : public IGuiGameActions,
                    public IHotbarViewModel,
                    public IInventoryViewModel,
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

    void LoadLastWorld() override;
    void ResumeGame() override;
    void OpenLoadWorld() override;
    void OpenNewWorld() override;
    void QuitApplication() override;
    void OpenSettings() override;
    bool HasPausedSession() const override;
    int GetHotbarCountSetting() const override;
    void SetHotbarCountSetting(int count) override;

    size_t GetBarCount() const override;
    std::array<HotbarSlotView, 10> GetBarSlots(size_t barIndex) const override;
    size_t GetSelectedSlot(size_t barIndex) const override;
    void SelectSlot(size_t barIndex, size_t slotIndex) override;
    bool AssignSlot(size_t barIndex, size_t slotIndex, const InventoryEntryRef& entry) override;
    void BeginPendingAssignment(const InventoryEntryRef& entry) override;
    bool HasPendingAssignment() const override;
    bool ApplyPendingAssignment(size_t barIndex, size_t slotIndex) override;
    void ClearPendingAssignment() override;

    bool OnPrimaryHotbarKey(int slotIndex);
    void BeginDragFromSlot(const SlotAddress& source, const InventoryEntryRef& entry);
    bool IsDragging() const;
    const DragState& GetDrag() const { return drag_; }
    bool DropOnSlot(const SlotAddress& target);
    void CancelDrag();
    InventoryEntryRef GetHotbarEntryRef(size_t barIndex, size_t slotIndex) const;

    std::vector<InventoryGroupView> GetGroups(ContentKind tab, InventoryMode mode) const override;
    std::vector<InventoryEntryView> GetEntries(ContentKind tab,
                                               const std::string& groupId,
                                               InventoryMode mode) const override;
    bool CanAssignToHotbar(const InventoryEntryRef& entry,
                           size_t barIndex,
                           size_t slotIndex) const override;
    bool AssignToHotbar(const InventoryEntryRef& entry,
                        size_t barIndex,
                        size_t slotIndex) override;
    InventoryMode GetInventoryMode() const override;
    void SetInventoryMode(InventoryMode mode) override;

    CommandResult Execute(const std::vector<std::string>& args) override;
    void AddChatLine(const std::string& line) override;
    const std::vector<std::string>& GetChatLog() const { return chatLog_; }

    ConsoleCommandHistory& GetCommandHistory() { return commandHistory_; }
    const ConsoleCommandHistory& GetCommandHistory() const { return commandHistory_; }
    void InitCommandHistory(const std::filesystem::path& filePath);
    void SaveCommandHistory();

private:
    Application* application_;
    std::shared_ptr<World> world_;
    CommandRegistry commandRegistry_;
    ContentTypeRegistry contentCatalog_;
    std::vector<std::string> chatLog_;
    ConsoleCommandHistory commandHistory_;
    InventoryMode inventoryMode_{InventoryMode::Creative};
    std::optional<InventoryEntryRef> pendingAssignment_;
    DragState drag_;
};

} // namespace cutum

#endif

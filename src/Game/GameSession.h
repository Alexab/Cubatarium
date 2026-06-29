#ifndef GAME_SESSION_H
#define GAME_SESSION_H

#include "Commands/CommandRegistry.h"
#include "Console/ConsoleCommandHistory.h"
#include "Content/ContentTypeRegistry.h"
#include "Game/Inventory/SlotInteraction.h"
#include "Gui/Interfaces/IContentCatalog.h"
#include "Gui/Interfaces/IGameCommandContext.h"
#include "Gui/Interfaces/IGuiGameActions.h"
#include "Gui/Interfaces/IHotbarViewModel.h"
#include "Gui/Interfaces/IInventoryViewModel.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace cutum
{

class UApplication;
class UWorld;

class UGameSession : public IGuiGameActions,
                     public IHotbarViewModel,
                     public IInventoryViewModel,
                     public IGameCommandContext
{
public:
  UGameSession(UApplication *application, std::shared_ptr<UWorld> world);

  void InitializeCatalog(const std::string &typesJsonPath,
                         const UBlockDefinitionStorage &blocks,
                         const UObjectLibrary &prefabs);
  void ReindexBlockCatalog(const UBlockDefinitionStorage &blocks,
                           const UObjectLibrary &prefabs);
  void ReindexCreatureCatalog();
  void RegisterCommands();

  UCommandRegistry &GetCommandRegistry() { return UCommandRegistry; }
  std::shared_ptr<UWorld> GetWorld() const { return World; }
  UContentTypeRegistry &GetContentCatalog() { return ContentCatalog; }
  IContentCatalog &AsContentCatalog() { return ContentCatalog; }

  void LoadLastWorld() override;
  void ResumeGame() override;
  void OpenLoadWorld() override;
  void OpenNewWorld() override;
  void QuitApplication() override;
  void OpenSettings() override;
  void OpenWorldSettings() override;
  bool HasPausedSession() const override;
  int GetHotbarCountSetting() const override;
  void SetHotbarCountSetting(int count) override;

  size_t GetBarCount() const override;
  std::array<HotbarSlotView, 10> GetBarSlots(size_t barIndex) const override;
  size_t GetSelectedSlot(size_t barIndex) const override;
  void SelectSlot(size_t barIndex, size_t slotIndex) override;
  bool AssignSlot(size_t barIndex, size_t slotIndex,
                  const InventoryEntryRef &entry) override;
  void BeginPendingAssignment(const InventoryEntryRef &entry) override;
  bool HasPendingAssignment() const override;
  bool ApplyPendingAssignment(size_t barIndex, size_t slotIndex) override;
  void ClearPendingAssignment() override;

  bool OnPrimaryHotbarKey(int slotIndex);
  void BeginDragFromSlot(const SlotAddress &source,
                         const InventoryEntryRef &entry);
  bool IsDragging() const;
  const DragState &GetDrag() const { return Drag; }
  bool DropOnSlot(const SlotAddress &target);
  void CancelDrag();
  InventoryEntryRef GetHotbarEntryRef(size_t barIndex, size_t slotIndex) const;

  std::vector<InventoryGroupView> GetGroups(ContentKind tab,
                                            InventoryMode mode) const override;
  std::vector<InventoryEntryView> GetEntries(ContentKind tab,
                                             const std::string &groupId,
                                             InventoryMode mode) const override;
  bool CanAssignToHotbar(const InventoryEntryRef &entry, size_t barIndex,
                         size_t slotIndex) const override;
  bool AssignToHotbar(const InventoryEntryRef &entry, size_t barIndex,
                      size_t slotIndex) override;
  bool CanSpawnCreatureByView(const std::string &speciesId) const;
  std::string GetCreatureSpawnBlockedHint(const std::string &speciesId) const;
  InventoryMode GetInventoryMode() const override;
  void SetInventoryMode(InventoryMode mode) override;

  CommandResult Execute(const std::vector<std::string> &args) override;
  void AddChatLine(const std::string &line) override;
  const std::vector<std::string> &GetChatLog() const { return ChatLog; }

  UConsoleCommandHistory &GetCommandHistory() { return CommandHistory; }
  const UConsoleCommandHistory &GetCommandHistory() const
  {
    return CommandHistory;
  }
  void InitCommandHistory(const std::filesystem::path &filePath);
  void SaveCommandHistory();

private:
  UApplication *Application;
  std::shared_ptr<UWorld> World;
  UCommandRegistry UCommandRegistry;
  UContentTypeRegistry ContentCatalog;
  std::vector<std::string> ChatLog;
  UConsoleCommandHistory CommandHistory;
  InventoryMode ActiveInventoryMode{InventoryMode::Creative};
  std::optional<InventoryEntryRef> PendingAssignment;
  DragState Drag;
};

} // namespace cutum

#endif

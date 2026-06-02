#include "GameSession.h"
#include "Application.h"
#include "BlockDefinitionStorage.h"
#include "Camera.h"
#include "Prefab.h"
#include "User.h"
#include "World.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <sstream>

namespace cutum {

GameSession::GameSession(Application* application, std::shared_ptr<World> world)
    : application_(application)
    , world_(std::move(world))
{
}

void GameSession::InitializeCatalog(const std::string& typesJsonPath,
                                    const BlockDefinitionStorage& blocks,
                                    const PrefabLibrary& prefabs)
{
    contentCatalog_.LoadTypes(typesJsonPath);
    contentCatalog_.IndexBlocks(blocks);
    contentCatalog_.IndexPrefabs(prefabs);
}

void GameSession::RegisterCommands()
{
    commandRegistry_.Register("help", [](const std::vector<std::string>&) {
        return CommandResult{true, "Commands: help, give, tp, fly, time"};
    });

    commandRegistry_.Register("time", [](const std::vector<std::string>&) {
        return CommandResult{true, "Time of day is not implemented yet."};
    });

    commandRegistry_.Register("give", [this](const std::vector<std::string>& args) {
        if (args.size() < 2) {
            return CommandResult{false, "Usage: give <block>"};
        }
        auto user = world_->GetCurrentUser();
        if (!user) {
            return CommandResult{false, "No user"};
        }
        user->AddToInventory(args[1]);
        return CommandResult{true, "Added " + args[1]};
    });

    commandRegistry_.Register("tp", [this](const std::vector<std::string>& args) {
        if (args.size() < 4) {
            return CommandResult{false, "Usage: tp <x> <y> <z>"};
        }
        auto user = world_->GetCurrentUser();
        auto camera = world_->GetCurrentUserCamera();
        if (!user || !camera) {
            return CommandResult{false, "No user/camera"};
        }
        try {
            const float x = std::stof(args[1]);
            const float y = std::stof(args[2]);
            const float z = std::stof(args[3]);
            const glm::vec3 pos{x, y, z};
            user->SetPosition(pos);
            camera->SetPosition(pos);
            return CommandResult{true, "Teleported"};
        } catch (...) {
            return CommandResult{false, "Invalid coordinates"};
        }
    });

    commandRegistry_.Register("fly", [this](const std::vector<std::string>& args) {
        auto camera = world_->GetCurrentUserCamera();
        if (!camera) {
            return CommandResult{false, "No camera"};
        }
        bool enable = true;
        if (args.size() >= 2) {
            enable = args[1] == "on" || args[1] == "1" || args[1] == "true";
        }
        camera->SetFreeMove(enable);
        return CommandResult{true, enable ? "Flight on" : "Flight off"};
    });
}

void GameSession::LoadLastWorld()
{
    if (application_) {
        application_->ScheduleEnterGame();
    }
}

void GameSession::ResumeGame()
{
    if (application_) {
        application_->RequestEnterGame();
    }
}

void GameSession::OpenLoadWorld()
{
    if (application_) {
        application_->ShowLoadWorld();
    }
}

void GameSession::OpenNewWorld()
{
    if (application_) {
        application_->ShowNewWorld();
    }
}

void GameSession::QuitApplication()
{
    if (application_) {
        application_->ScheduleQuit();
    }
}

void GameSession::OpenSettings()
{
    if (application_) {
        application_->ShowSettings();
    }
}

bool GameSession::HasPausedSession() const
{
    return application_ && application_->HasWorldSession();
}

int GameSession::GetHotbarCountSetting() const
{
    return application_ ? application_->GetHotbarCountSetting() : 1;
}

void GameSession::SetHotbarCountSetting(int count)
{
    if (application_) {
        application_->SetHotbarCountSetting(count);
    }
}

size_t GameSession::GetBarCount() const
{
    auto user = world_ ? world_->GetCurrentUser() : nullptr;
    return user ? user->GetHotbarCount() : 0;
}

std::array<HotbarSlotView, 10> GameSession::GetBarSlots(size_t barIndex) const
{
    std::array<HotbarSlotView, 10> slots{};
    auto user = world_ ? world_->GetCurrentUser() : nullptr;
    if (!user) {
        return slots;
    }
    const HotbarBar& bar = user->GetHotbar(barIndex);
    const size_t activeIndex = user->GetActiveSlotIndex(barIndex);
    for (size_t i = 0; i < slots.size(); ++i) {
        const HotbarSlot& slot = bar.slots[i];
        if (!slot.empty && !slot.entry.empty) {
            slots[i].id = slot.entry.id;
            slots[i].label = slot.entry.id;
            slots[i].isBlock = (slot.entry.kind == InventoryEntryKind::Block);
        }
        slots[i].selected = (i == activeIndex);
        if (barIndex == 0) {
            slots[i].hotkey = static_cast<int>(i);
        }
    }
    return slots;
}

size_t GameSession::GetSelectedSlot(size_t barIndex) const
{
    auto user = world_->GetCurrentUser();
    return user ? user->GetActiveSlotIndex(barIndex) : 0;
}

void GameSession::SelectSlot(size_t barIndex, size_t slotIndex)
{
    if (auto user = world_->GetCurrentUser()) {
        user->SetActiveSlot(barIndex, slotIndex);
    }
}

bool GameSession::AssignSlot(size_t barIndex, size_t slotIndex, const InventoryEntryRef& entry)
{
    if (auto user = world_->GetCurrentUser()) {
        return user->AssignToHotbar(barIndex, slotIndex, entry);
    }
    return false;
}

void GameSession::BeginPendingAssignment(const InventoryEntryRef& entry)
{
    pendingAssignment_ = entry;
}

bool GameSession::HasPendingAssignment() const
{
    return pendingAssignment_.has_value() && !pendingAssignment_->empty;
}

bool GameSession::ApplyPendingAssignment(size_t barIndex, size_t slotIndex)
{
    if (!pendingAssignment_.has_value()) {
        return false;
    }
    if (!CanAssignToHotbar(*pendingAssignment_, barIndex, slotIndex)) {
        return false;
    }
    const bool ok = AssignSlot(barIndex, slotIndex, *pendingAssignment_);
    if (ok) {
        SelectSlot(barIndex, slotIndex);
        pendingAssignment_.reset();
    }
    return ok;
}

void GameSession::ClearPendingAssignment()
{
    pendingAssignment_.reset();
}

std::vector<InventoryGroupView> GameSession::GetGroups(ContentKind tab, InventoryMode mode) const
{
    std::vector<InventoryGroupView> groups;
    const auto ids = contentCatalog_.GetTypeIds(tab);
    for (const std::string& id : ids) {
        const auto entries = contentCatalog_.GetEntries(tab, id);
        if (mode == InventoryMode::Owned && entries.empty()) {
            continue;
        }
        groups.push_back({id, contentCatalog_.GetTypeDisplayName(id), 0});
    }
    std::stable_sort(groups.begin(), groups.end(), [](const InventoryGroupView& a, const InventoryGroupView& b) {
        if (a.id == "misc") return false;
        if (b.id == "misc") return true;
        return a.label < b.label;
    });
    return groups;
}

std::vector<InventoryEntryView> GameSession::GetEntries(ContentKind tab,
                                                        const std::string& groupId,
                                                        InventoryMode mode) const
{
    std::vector<InventoryEntryView> result;
    auto entries = contentCatalog_.GetEntries(tab, groupId);
    auto user = world_ ? world_->GetCurrentUser() : nullptr;
    const auto* inv = user ? &user->GetInventory() : nullptr;
    for (const auto& e : entries) {
        InventoryEntryRef ref;
        ref.kind = (tab == ContentKind::Block) ? InventoryEntryKind::Block : InventoryEntryKind::Object;
        ref.id = e.id;
        ref.empty = false;
        ref.count = 0;
        if (inv) {
            const auto it = inv->find(e.id);
            if (it != inv->end()) {
                ref.count = it->second;
            }
        }
        if (mode == InventoryMode::Owned && ref.count <= 0) {
            continue;
        }
        result.push_back({ref, e.displayName});
    }
    std::sort(result.begin(), result.end(), [](const InventoryEntryView& a, const InventoryEntryView& b) {
        if (a.label == b.label) return a.ref.id < b.ref.id;
        return a.label < b.label;
    });
    return result;
}

bool GameSession::CanAssignToHotbar(const InventoryEntryRef& entry,
                                    size_t barIndex,
                                    size_t slotIndex) const
{
    if (entry.empty || barIndex >= 2 || slotIndex >= 10) {
        return false;
    }
    if (inventoryMode_ == InventoryMode::Creative) {
        return true;
    }
    auto user = world_ ? world_->GetCurrentUser() : nullptr;
    if (!user) {
        return false;
    }
    const auto& inv = user->GetInventory();
    const auto it = inv.find(entry.id);
    return it != inv.end() && it->second > 0;
}

bool GameSession::AssignToHotbar(const InventoryEntryRef& entry,
                                 size_t barIndex,
                                 size_t slotIndex)
{
    if (!CanAssignToHotbar(entry, barIndex, slotIndex)) {
        return false;
    }
    return AssignSlot(barIndex, slotIndex, entry);
}

InventoryMode GameSession::GetInventoryMode() const
{
    return inventoryMode_;
}

void GameSession::SetInventoryMode(InventoryMode mode)
{
    inventoryMode_ = mode;
}

CommandResult GameSession::Execute(const std::vector<std::string>& args)
{
    if (args.empty()) {
        return {false, "Empty"};
    }
    std::string line;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            line += ' ';
        }
        line += args[i];
    }
    return commandRegistry_.ExecuteLine(line);
}

void GameSession::AddChatLine(const std::string& line)
{
    chatLog_.push_back(line);
    if (chatLog_.size() > 200) {
        chatLog_.erase(chatLog_.begin());
    }
}

} // namespace cutum

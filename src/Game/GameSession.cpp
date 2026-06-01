#include "GameSession.h"
#include "Application.h"
#include "BlockDefinitionStorage.h"
#include "Camera.h"
#include "Prefab.h"
#include "User.h"
#include "World.h"

#include <GLFW/glfw3.h>
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

void GameSession::StartGame()
{
    if (application_) {
        application_->RequestEnterGame();
    }
}

void GameSession::QuitApplication()
{
    if (application_) {
        application_->RequestQuit();
    }
}

void GameSession::OpenSettings()
{
    AddChatLine("Settings UI is not implemented yet.");
}

std::array<HotbarSlotView, 10> GameSession::GetBlockSlots() const
{
    std::array<HotbarSlotView, 10> slots{};
    auto user = world_ ? world_->GetCurrentUser() : nullptr;
    if (!user) {
        return slots;
    }
    const auto& hotbar = user->GetBlockHotbar();
    const size_t activeIndex = user->GetActiveBlockIndex();
    for (size_t i = 0; i < slots.size() && i < hotbar.size(); ++i) {
        slots[i].id = hotbar[i];
        slots[i].label = hotbar[i];
        slots[i].isBlock = true;
        slots[i].selected = (i == activeIndex);
    }
    return slots;
}

std::array<HotbarSlotView, 10> GameSession::GetPrefabSlots() const
{
    std::array<HotbarSlotView, 10> slots{};
    auto user = world_ ? world_->GetCurrentUser() : nullptr;
    if (!user) {
        return slots;
    }
    const auto& hotbar = user->GetPrefabHotbar();
    const size_t activeIndex = user->GetActivePrefabIndex();
    for (size_t i = 0; i < slots.size() && i < hotbar.size(); ++i) {
        slots[i].id = hotbar[i];
        slots[i].label = hotbar[i];
        slots[i].isBlock = false;
        slots[i].selected = (i == activeIndex);
    }
    return slots;
}

size_t GameSession::GetActiveBlockIndex() const
{
    auto user = world_->GetCurrentUser();
    return user ? user->GetActiveBlockIndex() : 0;
}

size_t GameSession::GetActivePrefabIndex() const
{
    auto user = world_->GetCurrentUser();
    return user ? user->GetActivePrefabIndex() : 0;
}

void GameSession::SelectBlockSlot(size_t index)
{
    if (auto user = world_->GetCurrentUser()) {
        user->SetActiveBlockIndex(index);
    }
}

void GameSession::SelectPrefabSlot(size_t index)
{
    if (auto user = world_->GetCurrentUser()) {
        user->SetActivePrefabIndex(index);
    }
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

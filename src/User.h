#ifndef USER_H
#define USER_H

#include <map>
#include <string>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include "InventoryTypes.h"

namespace cutum {

class Object;

class User
{
public:
 User();

 const std::map<std::string, int>& GetInventory() const;
 void AddToInventory(const std::string &cube_type);

 const std::string& GetActiveBlockTypeName() const;
 const std::string& GetActiveObjectTypeName() const;
 const std::string& GetActivePrefabName() const;
 /// Активная ячейка хотбара (bar + slot), или nullptr если пусто.
 const InventoryEntryRef* GetActiveHotbarEntry() const;

 void SetActiveBlockIndex(size_t index);
 void SetActivePrefabIndex(size_t index);
 size_t GetActiveBlockIndex() const { return activeSlotIndex_; }
 size_t GetActivePrefabIndex() const { return activeSlotIndex_; }
 void SetActiveObjectTypeName(const std::string& block_type);
 void SetPrefabHotbar(const std::vector<std::string>& prefab_names);

 std::vector<std::string> GetBlockHotbar() const;
 std::vector<std::string> GetPrefabHotbar() const;
 void EnsureHotbarCount(size_t count);
 size_t GetHotbarCount() const;
 const HotbarBar& GetHotbar(size_t bar) const;
 bool AssignToHotbar(size_t bar, size_t slot, const InventoryEntryRef& entry);
 void ClearHotbarSlot(size_t bar, size_t slot);
 bool SetActiveSlot(size_t bar, size_t slot);
 size_t GetActiveSlotIndex(size_t bar) const;
 size_t GetActiveBarIndex() const { return activeBarIndex_; }

 std::shared_ptr<Object> GetActiveObject();

 const glm::vec3& GetPosition() const;
 const glm::vec3& GetViewDirection() const;

 void SetPosition(const glm::vec3& value);
 void SetViewDirection(const glm::vec3& value);

 float GetCameraYaw() const;
 float GetCameraPitch() const;
 void SetCameraOrientation(float yaw, float pitch);

 size_t GetViewId() const;
 void SetViewId(size_t value);

 nlohmann::json SerializeHotbars() const;
 void DeserializeHotbars(const nlohmann::json& userData, size_t maxBarCount);

private:
 void InitDefaultHotbar();
 static const std::string& EmptyString();
 const InventoryEntryRef* GetActiveEntryRef() const;
 void ClampActiveIndices();

 std::map<std::string, int> Inventory;
 std::vector<HotbarBar> hotbars_;
 size_t activeBarIndex_{0};
 size_t activeSlotIndex_{0};

 std::shared_ptr<Object> ActiveObject;
 glm::vec3 Position;
 glm::vec3 ViewDirection;
 float CameraYaw{-90.0f};
 float CameraPitch{0.0f};
 size_t ViewId;
};

}

#endif // USER_H

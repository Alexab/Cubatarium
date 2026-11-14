#ifndef WORLD_ITEM_SYSTEM_H
#define WORLD_ITEM_SYSTEM_H
#include "Game/Inventory/InventoryTypes.h"
#include "Game/Physics/ThrowMath.h"
#include <glm/mat4x4.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
namespace cutum
{
class UWorld;
class UCreature;
class CreatureDrawQueue;
struct WorldItem
{
  int id{0};
  InventoryEntryRef stack;
  uint64_t creatureId{0}, carrierId{0}, throwerId{0};
  glm::vec3 position{}, velocity{};
  PhysicalProfile physical;
  float age{0.f}, heldTime{0.f}, charge{0.f}, damage{0.f};
  bool flying{false}, arrow{false}, creativeCopy{false};
};
class UWorldItemSystem
{
public:
  static UWorldItemSystem &Instance();
  int Spawn(const glm::vec3 &position, const std::string &itemId, int count,
            bool isBlock);
  int SpawnStack(glm::vec3 position, InventoryEntryRef stack,
                 bool creativeCopy = false);
  void Tick(UWorld &world, float dt);
  void Draw(UWorld &world, CreatureDrawQueue &queue, const glm::mat4 &viewProj);
  void Clear();
  size_t Count() const { return Items.size() + Pending.size(); }
  const std::vector<WorldItem> &GetItems() const { return Items; }
  PhysicalProfile Profile(UWorld &world, const InventoryEntryRef &stack) const;
  PhysicalProfile Profile(UWorld &world, const UCreature &creature) const;
  bool Grab(UWorld &world, UCreature &actor, glm::vec3 eye, glm::vec3 direction,
            std::string &reason);
  bool BeginThrow(UWorld &world, UCreature &actor, std::string &reason);
  bool Release(UWorld &world, UCreature &actor, glm::vec3 direction,
               bool gentle = false);
  void CancelCharge(UCreature &actor);
  bool HasCargo(uint64_t carrier) const;
  bool FireArrow(UWorld &world, UCreature &actor, glm::vec3 eye,
                 glm::vec3 direction, float charge);
  void Serialize(nlohmann::json &root) const;
  void Deserialize(const nlohmann::json &root);

private:
  struct Hit
  {
    float time{1.f};
    glm::vec3 normal{};
    uint64_t creature{0};
    bool found{false}, ready{true};
  };
  Hit Sweep(UWorld &world, glm::vec3 position, glm::vec3 delta, glm::vec3 half,
            uint64_t skip, uint64_t exclude, float dt) const;
  void MoveHeldCargo(UWorld &world, WorldItem &item, glm::vec3 desired,
                     float dt);
  void Settle(UWorld &world, WorldItem &item);
  int Add(WorldItem item);
  void Step(UWorld &world, float dt);
  std::vector<WorldItem> Items, Pending;
  int NextId{1};
  float Accumulator{0.f};
  float MaxTargetSpeed{0.f};
  bool Ticking{false};
};
} // namespace cutum
#endif

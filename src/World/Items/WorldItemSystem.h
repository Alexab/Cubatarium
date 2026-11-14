#ifndef WORLD_ITEM_SYSTEM_H
#define WORLD_ITEM_SYSTEM_H
#include "Game/Inventory/InventoryDomain.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Game/Physics/ThrowMath.h"
#include <glm/mat4x4.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_set>
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
  uint64_t creatureId{0}, carrierId{0}, throwerId{0}, lastContact{0};
  glm::vec3 position{}, velocity{};
  PhysicalProfile physical;
  float contactCooldown{0.f};
  float age{0.f}, heldTime{0.f}, charge{0.f}, damage{0.f};
  bool flying{false}, arrow{false}, pickupSeparated{true};
  InventoryDomain domain{InventoryDomain::Survival};
  InventoryDomain ActionDomain{InventoryDomain::Survival},
      CarrierDomain{InventoryDomain::Survival};
};
class UWorldItemSystem
{
public:
  static UWorldItemSystem &Instance();
  int Spawn(const glm::vec3 &position, const std::string &itemId, int count,
            bool isBlock, InventoryDomain domain = InventoryDomain::Survival);
  int SpawnStack(glm::vec3 position, InventoryEntryRef stack,
                 InventoryDomain domain = InventoryDomain::Survival,
                 uint64_t thrower = 0);
  void HandleDeath(UWorld &world, UCreature &creature);
  std::optional<InventoryDomain> DamageDomain;
  struct DamageScope
  {
    std::optional<InventoryDomain> previous;
    DamageScope(std::optional<InventoryDomain> domain)
        : previous(Instance().DamageDomain)
    {
      Instance().DamageDomain = domain;
    }
    ~DamageScope() { Instance().DamageDomain = previous; }
  };
  void ReconcileOwnership(UWorld &world);
  void ReleaseCarrier(UWorld &world, uint64_t carrier);
  bool ReserveDrops(size_t count);
  bool DropFromActor(UWorld &world, UCreature &actor,
                     const InventoryEntryRef &entry, std::string &reason);
  bool CanActivate() const { return Items.size() < 512; }
  void Tick(UWorld &world, float dt);
  void Draw(UWorld &world, CreatureDrawQueue &queue, const glm::mat4 &viewProj);
  void Clear();
  const std::string &GetFeedback() const { return Feedback; }
  size_t Count() const
  {
    return Items.size() + Pending.size() + Dormant.size();
  }
  const std::vector<WorldItem> &GetActiveItems() const { return Items; }
  std::vector<WorldItem> GetItems() const
  {
    auto rows = Items;
    rows.insert(rows.end(), Dormant.begin(), Dormant.end());
    rows.insert(rows.end(), Pending.begin(), Pending.end());
    return rows;
  }
  PhysicalProfile Profile(UWorld &world, const InventoryEntryRef &stack) const;
  PhysicalProfile Profile(UWorld &world, const UCreature &creature) const;
  bool Grab(UWorld &world, UCreature &actor, glm::vec3 eye, glm::vec3 direction,
            std::string &reason);
  bool CanThrow(UWorld &world, const InventoryEntryRef &entry,
                std::string &reason) const;
  bool BeginThrow(UWorld &world, UCreature &actor, std::string &reason);
  bool Release(UWorld &world, UCreature &actor, glm::vec3 direction,
               bool gentle = false);
  void CancelCharge(UCreature &actor);
  void SetThrowCharge(UCreature &actor, float charge);
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
            uint64_t skip, uint64_t exclude, float dt,
            uint64_t contact = 0) const;
  void MoveHeldCargo(UWorld &world, WorldItem &item, glm::vec3 desired,
                     float dt);
  void Settle(UWorld &world, WorldItem &item);
  int Add(WorldItem item);
  void CoalesceDormant(UWorld &world);
  void Step(UWorld &world, float dt);
  std::vector<WorldItem> Items, Pending, Dormant;
  nlohmann::json PhysicalRecovery = nlohmann::json::array();
  std::unordered_set<uint64_t> Deaths;
  int NextId{1};
  std::string Feedback;
  float FeedbackTime{0.f};
  float SleepProbeClock{0.f};
  float Accumulator{0.f};
  float MaxTargetSpeed{0.f};
  bool Ticking{false}, DormantDirty{true};
};
} // namespace cutum
#endif

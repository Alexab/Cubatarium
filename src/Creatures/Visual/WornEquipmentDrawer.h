#ifndef WORN_EQUIPMENT_DRAWER_H
#define WORN_EQUIPMENT_DRAWER_H

#include <array>
#include <functional>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <string>

namespace cutum
{

class CreatureDrawQueue;
class UCreature;
class UItemDefinitionStorage;
class UShaderProgram;
struct InventoryEntryRef;

/// Lookup bone model-space matrix by name (already without bodyMat).
using BoneMatrixLookupFn =
    std::function<bool(const std::string &boneName, glm::mat4 &out)>;
using BoneBoundsLookupFn = std::function<bool(const std::string &boneName,
                                              glm::vec3 &low, glm::vec3 &high)>;

struct WornArmorPreviewSlot
{
  std::string ItemId;
};

constexpr size_t kWornArmorSlotCount = 4;

/// Draw equipped armor overlays attached to humanoid bones.
class WornEquipmentDrawer
{
public:
  /// When true, skip TP wield on possessed creatures (FP viewmodel owns it).
  static void SetHidePossessedWield(bool hide);
  static void SubmitLoose(CreatureDrawQueue &queue, const std::string &id,
                          const glm::mat4 &viewProj, const glm::mat4 &model,
                          glm::vec3 size, unsigned int blockTexture = 0);
  static bool HidePossessedWield();
  static void SetItemDefinitions(const UItemDefinitionStorage *items);
  static const UItemDefinitionStorage *ItemDefinitions();

  /// World path: read armor from creature inventory, submit into the
  /// creature draw queue.
  static void SubmitFromCreature(CreatureDrawQueue &queue,
                                 const UCreature &creature,
                                 const glm::mat4 &viewProj,
                                 const glm::mat4 &bodyMat,
                                 const BoneMatrixLookupFn &bones,
                                 const BoneBoundsLookupFn &bounds = {});

  /// Hotbar (rightItem) + offhand (leftItem) held meshes.
  static void SubmitWieldedFromCreature(CreatureDrawQueue &queue,
                                        const UCreature &creature,
                                        const UItemDefinitionStorage *items,
                                        const glm::mat4 &viewProj,
                                        const glm::mat4 &bodyMat,
                                        const BoneMatrixLookupFn &bones,
                                        const BoneBoundsLookupFn &bounds = {});

  /// Sheet / preview path: explicit item ids; `queue` null draws immediate.
  static void SubmitWieldedPreview(
      CreatureDrawQueue *queue, UShaderProgram *shader,
      const std::string &mainItemId, const std::string &offhandItemId,
      const UItemDefinitionStorage *items, const glm::mat4 &viewProj,
      const glm::mat4 &bodyMat, const BoneMatrixLookupFn &bones, bool immediate,
      const BoneBoundsLookupFn &bounds = {});

  /// Direct GL draw for preview FBO (no CreatureDrawQueue).
  static void DrawImmediate(
      const std::array<WornArmorPreviewSlot, kWornArmorSlotCount> &slots,
      const glm::mat4 &viewProj, const glm::mat4 &bodyMat,
      const BoneMatrixLookupFn &bones, UShaderProgram *shader,
      const BoneBoundsLookupFn &bounds = {});
};

} // namespace cutum

#endif

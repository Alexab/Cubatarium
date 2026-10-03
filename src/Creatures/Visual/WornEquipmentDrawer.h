#ifndef WORN_EQUIPMENT_DRAWER_H
#define WORN_EQUIPMENT_DRAWER_H

#include <array>
#include <functional>
#include <string>
#include <glm/mat4x4.hpp>

namespace cutum
{

class UGeometryEngine;
class UCreature;
class UItemDefinitionStorage;
class UShaderProgram;
struct InventoryEntryRef;

/// Lookup bone model-space matrix by name (already without bodyMat).
using BoneMatrixLookupFn =
    std::function<bool(const std::string &boneName, glm::mat4 &out)>;

struct WornArmorPreviewSlot
{
  std::string ItemId;
};

/// Draw equipped armor overlays attached to humanoid bones.
class WornEquipmentDrawer
{
public:
  /// When true, skip TP wield on possessed creatures (FP viewmodel owns it).
  static void SetHidePossessedWield(bool hide);
  static bool HidePossessedWield();
  static void SetItemDefinitions(const UItemDefinitionStorage *items);
  static const UItemDefinitionStorage *ItemDefinitions();

  /// World path: read armor from creature inventory.
  static void SubmitFromCreature(UGeometryEngine &engine,
                                 const UCreature &creature,
                                 const glm::mat4 &viewProj,
                                 const glm::mat4 &bodyMat,
                                 const BoneMatrixLookupFn &bones);

  /// Hotbar (rightItem) + offhand (leftItem) held meshes.
  static void SubmitWieldedFromCreature(UGeometryEngine &engine,
                                        const UCreature &creature,
                                        const UItemDefinitionStorage *items,
                                        const glm::mat4 &viewProj,
                                        const glm::mat4 &bodyMat,
                                        const BoneMatrixLookupFn &bones);

  /// Sheet / preview path: explicit item ids per armor slot (0..5).
  static void SubmitFromSlots(UGeometryEngine &engine,
                              const std::array<WornArmorPreviewSlot, 6> &slots,
                              const glm::mat4 &viewProj,
                              const glm::mat4 &bodyMat,
                              const BoneMatrixLookupFn &bones);

  static void SubmitWieldedPreview(UGeometryEngine *engine,
                                   UShaderProgram *shader,
                                   const std::string &mainItemId,
                                   const std::string &offhandItemId,
                                   const UItemDefinitionStorage *items,
                                   const glm::mat4 &viewProj,
                                   const glm::mat4 &bodyMat,
                                   const BoneMatrixLookupFn &bones,
                                   bool immediate);

  /// Direct GL draw for preview FBO (no CreatureDrawQueue).
  static void DrawImmediate(const std::array<WornArmorPreviewSlot, 6> &slots,
                            const glm::mat4 &viewProj,
                            const glm::mat4 &bodyMat,
                            const BoneMatrixLookupFn &bones,
                            UShaderProgram *shader);
};

} // namespace cutum

#endif

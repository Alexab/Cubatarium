#pragma once

#include "Core/Memory/StlSourceStorage.h"
#include "Gui/Interfaces/IUCharacterStatsViewModel.h"

namespace cutum
{
struct CharacterSnapshotStorage
{
  size_t inlineBytes{}, identityBytes{}, equipmentBytes{}, slotBytes{},
      recoveryBytes{}, effectBytes{}, carriedBytes{}, protectionBytes{};
  NameStorageEnvelope heap;
};

namespace character_snapshot_storage
{
inline void Account(CharacterSnapshotStorage &out, size_t &category,
                    const StlSourceStorage &source) noexcept
{
  category = source.bytes.requested;
  out.heap.supported &= source.bytes.supported;
  out.heap.overflow |= source.bytes.overflow;
  NameStorageAdd(out.heap, category);
}

inline void AccountEntry(StlSourceStorage &out, const InventoryEntryRef &entry) noexcept
{
  out.Text(entry.Id);
  out.Text(entry.contentsId);
}

inline void AccountLook(CharacterSnapshotStorage &out, const CharacterLookSnapshot &snap,
                        const std::string *displayName = nullptr) noexcept
{
  StlSourceStorage identity, equipment;
  identity.Text(snap.typeId);
  identity.Text(snap.skinId);
  identity.Text(snap.humanAppearance.bodyVariant);
  identity.Text(snap.humanAppearance.hairstyleId);
  identity.Text(snap.humanAppearance.hairColorId);
  if (displayName)
    identity.Text(*displayName);
  for (const auto &slot : snap.equippedArmor)
    equipment.Text(slot.itemId);
  for (const auto &slot : snap.equippedTools)
    equipment.Text(slot.itemId);
  Account(out, out.identityBytes, identity);
  Account(out, out.equipmentBytes, equipment);
}

inline void AccountPage(CharacterSnapshotStorage &out, const CharacterPageSnapshot &snap) noexcept
{
  AccountLook(out, snap, &snap.displayName);
  StlSourceStorage slots, recovery, effects;
  for (const auto &slot : snap.slots)
    AccountEntry(slots, slot);
  recovery.Buffer(snap.recovery.capacity(), sizeof(InventoryEntryRef));
  for (const auto &entry : snap.recovery)
    AccountEntry(recovery, entry);
  effects.Buffer(snap.effects.capacity(), sizeof(StatusEffectInstance));
  for (const auto &effect : snap.effects)
    effects.Text(effect.DefId);
  Account(out, out.slotBytes, slots);
  Account(out, out.recoveryBytes, recovery);
  Account(out, out.effectBytes, effects);
}
} // namespace character_snapshot_storage

// Observations of returned owning values on the pinned Release profile. The
// shell is reported separately from ordinary heap requests and shared sources.
inline CharacterSnapshotStorage InspectCharacterSnapshot(const CharacterLookSnapshot &snap) noexcept
{
  CharacterSnapshotStorage out;
  out.inlineBytes = sizeof(snap);
  out.heap.supported = StlSourceStorage::SupportedProfile();
  character_snapshot_storage::AccountLook(out, snap);
  return out;
}

inline CharacterSnapshotStorage InspectCharacterSnapshot(const CharacterPageSnapshot &snap) noexcept
{
  CharacterSnapshotStorage out;
  out.inlineBytes = sizeof(snap);
  out.heap.supported = StlSourceStorage::SupportedProfile();
  character_snapshot_storage::AccountPage(out, snap);
  return out;
}

inline CharacterSnapshotStorage InspectCharacterSnapshot(const CharacterStatsSnapshot &snap) noexcept
{
  auto out = InspectCharacterSnapshot(static_cast<const CharacterPageSnapshot &>(snap));
  out.inlineBytes = sizeof(snap);
  StlSourceStorage carried, protection;
  carried.Buffer(snap.carriedItems.capacity(), sizeof(CharacterStatsSnapshot::CarriedItem));
  for (const auto &item : snap.carriedItems)
    carried.Text(item.id);
  protection.Hash(snap.protection.Ratings);
  for (const auto &rating : snap.protection.Ratings)
    protection.Text(rating.first);
  character_snapshot_storage::Account(out, out.carriedBytes, carried);
  character_snapshot_storage::Account(out, out.protectionBytes, protection);
  return out;
}
} // namespace cutum

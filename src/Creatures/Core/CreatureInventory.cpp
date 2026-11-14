#include "Creatures/Core/CreatureInventory.h"
#include "Audio/AudioApi.h"
#include "Game/Inventory/InventoryIdentity.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ToolCapabilities.h"
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>

namespace cutum
{
namespace
{
const char *ArmorId(size_t i)
{
  static const char *ids[]{"head", "chest", "legs", "feet"};
  return i < 4 ? ids[i] : "";
}
const char *KindName(InventoryEntryKind k)
{
  switch (k)
  {
  case InventoryEntryKind::Item:
    return "item";
  case InventoryEntryKind::Object:
    return "object";
  case InventoryEntryKind::UCreature:
    return "creature";
  case InventoryEntryKind::Skin:
    return "skin";
  default:
    return "block";
  }
}
InventoryEntryKind ReadKind(const std::string &s)
{
  if (s == "item")
    return InventoryEntryKind::Item;
  if (s == "object")
    return InventoryEntryKind::Object;
  if (s == "creature")
    return InventoryEntryKind::UCreature;
  if (s == "skin")
    return InventoryEntryKind::Skin;
  return InventoryEntryKind::Block;
}
nlohmann::json WriteEntry(const InventoryEntryRef &e)
{
  if (e.empty || e.Id.empty() || e.count == 0)
    return {{"empty", true}};
  return {{"empty", false},
          {"form", e.IsCatalogReference() ? "catalog" : "finite"},
          {"kind", KindName(e.kind)},
          {"id", e.Id},
          {"count", e.count},
          {"wear", e.wear},
          {"broken", e.broken},
          {"instance_id", e.instanceId},
          {"contents_id", e.contentsId},
          {"contents_units", e.contentsUnits},
          {"pinned", e.pinned}};
}
InventoryEntryRef ReadEntry(const nlohmann::json &j)
{
  InventoryEntryRef e;
  if (!j.is_object())
    return e;
  e.Id = j.value("id", std::string{});
  e.empty = j.value("empty", e.Id.empty());
  if (e.empty || e.Id.empty())
    return {};
  e.kind = ReadKind(j.value("kind", std::string{"item"}));
  e.count = j.value("count", 1);
  if (j.value("form", std::string{}) == "catalog" || e.count < 0)
    e.form = InventoryEntryForm::CatalogReference;
  e.wear = j.value("wear", 0.f);
  e.wear = std::isfinite(e.wear) ? std::clamp(e.wear, 0.f, 1.f) : 1.f;
  e.broken = j.value("broken", false);
  e.instanceId = j.value("instance_id", std::uint64_t{0});
  e.contentsId = j.value("contents_id", std::string{});
  e.contentsUnits = std::max(0, j.value("contents_units", 0));
  e.pinned = j.value("pinned", false);
  return e;
}
void GearSound(AudioEventKind kind)
{
  UAudioEvent e;
  e.Kind = kind;
  UAudioApi::Post(e);
}
} // namespace

InventoryEntryRef UCreatureInventory::Prepare(InventoryEntryRef e)
{
  if (e.empty || e.Id.empty() || e.count == 0)
    return {};
  e.empty = false;
  if (e.IsCatalogReference())
  {
    e.form = InventoryEntryForm::CatalogReference;
    e.count = 1;
  }
  e.instanceId = InventoryInstanceIdentity(e.instanceId, NextInstanceId);
  NextInstanceId = std::max(NextInstanceId, e.instanceId + 1);
  return e;
}
int UCreatureInventory::StackLimit(const InventoryEntryRef &e) const
{
  if (e.kind == InventoryEntryKind::Item)
  {
    const auto *def = Items ? Items->Get(e.Id) : nullptr;
    return def ? std::clamp(def->StackMax, 1, 999) : 1;
  }
  return e.kind == InventoryEntryKind::Block ? 64 : 1;
}
InventoryEntryKind UCreatureInventory::KindForId(const std::string &id) const
{
  return Items && Items->Get(id) ? InventoryEntryKind::Item
                                 : InventoryEntryKind::Block;
}
bool UCreatureInventory::CanMerge(const InventoryEntryRef &a,
                                  const InventoryEntryRef &b)
{
  return !a.empty && !b.empty && !a.IsCatalogReference() &&
         !b.IsCatalogReference() && a.kind == b.kind && a.Id == b.Id &&
         a.wear == b.wear && a.broken == b.broken &&
         a.contentsId == b.contentsId && a.contentsUnits == b.contentsUnits &&
         !a.pinned && !b.pinned;
}
const InventoryEntryRef &UCreatureInventory::GetSlot(size_t i) const
{
  static const InventoryEntryRef empty;
  if (i >= kSlotCount)
    return empty;
  const auto &s = GetHotbars()[i / 10].slots[i % 10];
  return s.empty || s.entry.empty || s.entry.count == 0 ? empty : s.entry;
}
InventoryEntryRef *UCreatureInventory::GetSlotMutable(size_t i)
{
  if (i >= kSlotCount)
    return nullptr;
  return &GetHotbarsMutable()[i / 10].slots[i % 10].entry;
}
const std::map<std::string, int> &UCreatureInventory::GetStorage() const
{
  std::map<std::string, int> next;
  for (const auto &row : GetHotbars())
    for (const auto &s : row.slots)
    {
      if (s.empty || s.entry.empty || s.entry.count == 0)
        continue;
      auto &n = next[s.entry.Id];
      if (n < 0 || s.entry.IsCatalogReference())
        n = -1;
      else
        n = static_cast<int>(std::min<long long>(
            INT_MAX, static_cast<long long>(n) + s.entry.count));
    }
  if (next != StorageView)
    StorageView = std::move(next);
  return StorageView;
}
int UCreatureInventory::Count(InventoryEntryKind kind,
                              const std::string &id) const
{
  int n = 0;
  for (size_t i = 0; i < kSlotCount; ++i)
  {
    const auto &e = GetSlot(i);
    if (e.empty || e.kind != kind || e.Id != id)
      continue;
    if (e.IsCatalogReference())
      return -1;
    n = static_cast<int>(
        std::min<long long>(INT_MAX, static_cast<long long>(n) + e.count));
  }
  return n;
}
InventoryOperationResult UCreatureInventory::Insert(InventoryEntryRef e)
{
  if (e.empty || e.Id.empty() || e.count <= 0)
    return {0, std::max(0, e.count), "invalid_stack"};
  const int original = e.count;
  const int limit = StackLimit(e);
  auto &rows = GetHotbarsMutable();
  if (limit > 1)
    for (auto &row : rows)
      for (auto &s : row.slots)
      {
        if (s.empty || !CanMerge(s.entry, e) || s.entry.IsCatalogReference())
          continue;
        const int n = std::min(e.count, std::max(0, limit - s.entry.count));
        s.entry.count += n;
        e.count -= n;
      }
  bool first = true;
  for (auto &row : rows)
    for (auto &s : row.slots)
    {
      if (e.count == 0)
        break;
      if (!s.empty && !s.entry.empty && s.entry.count != 0)
        continue;
      auto part = e;
      part.count = std::min(limit, e.count);
      if (!first)
        part.instanceId = 0;
      part = Prepare(part);
      s = {false, part};
      e.count -= part.count;
      first = false;
    }
  return {original - e.count, e.count, e.count ? "inventory_full" : ""};
}
InventoryOperationResult UCreatureInventory::Take(size_t i, int count,
                                                  InventoryEntryRef &out)
{
  out = {};
  if (i >= kSlotCount || count <= 0)
    return {0, std::max(0, count), "invalid_slot"};
  auto &s = GetHotbarsMutable()[i / 10].slots[i % 10];
  if (s.empty || s.entry.empty || s.entry.count == 0)
    return {0, count, "empty_slot"};
  if (!s.entry.IsCatalogReference() && s.entry.count < count)
    return {0, count, "insufficient_items"};
  out = s.entry;
  out.count = count;
  if (s.entry.IsCatalogReference())
  {
    if (!Creative)
    {
      out = {};
      return {0, count, "catalog_not_allowed"};
    }
    out.form = InventoryEntryForm::FiniteStack;
    out.instanceId = 0;
    out = Prepare(out);
    return {count, 0, {}};
  }
  if (s.entry.count == count)
    s = {};
  else
  {
    s.entry.count -= count;
    out.instanceId = 0;
    out = Prepare(out);
  }
  return {count, 0, {}};
}
InventoryOperationResult UCreatureInventory::Move(size_t from, size_t to,
                                                  int count)
{
  if (from >= kSlotCount || to >= kSlotCount)
    return {0, 0, "invalid_slot"};
  const auto source = GetSlot(from);
  if (source.empty)
    return {0, 0, "empty_slot"};
  if (from == to)
    return {};
  const int wanted = count < 0 ? source.count : count;
  if (wanted <= 0 && !Creative)
    return {0, 0, "invalid_count"};
  if (!source.IsCatalogReference() && wanted > source.count)
    return {0, wanted, "insufficient_items"};
  auto &rows = GetHotbarsMutable();
  auto &a = rows[from / 10].slots[from % 10];
  auto &b = rows[to / 10].slots[to % 10];
  if (b.empty || b.entry.empty)
  {
    if (count < 0 || wanted == source.count || source.IsCatalogReference())
    {
      b = a;
      a = {};
      return {source.count, 0, {}};
    }
    InventoryEntryRef part;
    auto r = Take(from, wanted, part);
    if (!r)
      return r;
    b = {false, part};
    return r;
  }
  if (StackLimit(source) > 1 && CanMerge(source, b.entry) && source.count > 0 &&
      b.entry.count > 0)
  {
    const int n = std::min(wanted, StackLimit(source) - b.entry.count);
    if (n <= 0)
      return {0, wanted, "stack_full"};
    b.entry.count += n;
    a.entry.count -= n;
    if (a.entry.count == 0)
      a = {};
    return {n, wanted - n, wanted > n ? "stack_full" : ""};
  }
  if (count >= 0 && count != source.count)
    return {0, wanted, "incompatible_stack"};
  std::swap(a, b);
  return {source.count, 0, {}};
}
InventoryOperationResult UCreatureInventory::Consume(InventoryEntryKind kind,
                                                     const std::string &id,
                                                     int count)
{
  if (count <= 0)
    return {0, 0, "invalid_count"};
  if (Creative)
    return {count, 0, {}};
  const int available = Count(kind, id);
  if (available < count)
    return {0, count, "insufficient_items"};
  int left = count;
  const size_t active = ActiveBarIndex * 10 + ActiveSlotIndex;
  const auto consume = [&](size_t i)
  {
    auto &s = Hotbars[i / 10].slots[i % 10];
    if (left <= 0 || s.empty || s.entry.empty || s.entry.kind != kind ||
        s.entry.Id != id)
      return;
    const int n = std::min(left, s.entry.count);
    s.entry.count -= n;
    left -= n;
    if (s.entry.count == 0)
      s = {};
  };
  consume(active);
  for (size_t i = 0; i < kSlotCount; ++i)
    if (i != active)
      consume(i);
  return {count, 0, {}};
}
bool UCreatureInventory::AssignOwned(size_t bar, size_t slot,
                                     const InventoryEntryRef &entry)
{
  if (bar >= 4 || slot >= 10 || entry.empty)
    return false;
  for (size_t i = 0; i < kSlotCount; ++i)
  {
    const auto &e = GetSlot(i);
    if (e.empty || e.kind != entry.kind || e.Id != entry.Id)
      continue;
    if (entry.instanceId != 0 && e.instanceId != entry.instanceId)
      continue;
    return static_cast<bool>(Move(i, bar * 10 + slot));
  }
  return false;
}
void UCreatureInventory::AddItem(const std::string &id, int count)
{
  InventoryEntryRef e;
  e.empty = false;
  e.Id = id;
  e.count = count;
  e.kind = Items && Items->Get(id) ? InventoryEntryKind::Item
                                   : InventoryEntryKind::Block;
  const auto r = Insert(e);
  if (r.remainder > 0)
  {
    e.count = r.remainder;
    RecoveryForMode().push_back(Prepare(e));
  }
}
void UCreatureInventory::SetItemDefinitions(const UItemDefinitionStorage *items)
{
  if (Items == items)
    return;
  Items = items;
  if (!Items)
    return;
  const bool original = Creative;
  for (bool creative : {false, true})
  {
    Creative = creative;
    std::vector<InventoryEntryRef> excess;
    for (auto &row : GetHotbarsMutable())
      for (auto &slot : row.slots)
      {
        auto &e = slot.entry;
        if (slot.empty || e.empty)
          continue;
        if (e.kind == InventoryEntryKind::Block && Items->Get(e.Id))
          e.kind = InventoryEntryKind::Item;
        if (e.kind == InventoryEntryKind::Item && !Items->Get(e.Id))
        {
          RecoveryForMode().push_back(e);
          slot = {};
          continue;
        }
        if (!e.IsCatalogReference() && e.count > StackLimit(e))
        {
          auto remainder = e;
          remainder.count -= StackLimit(e);
          remainder.instanceId = 0;
          e.count = StackLimit(e);
          excess.push_back(remainder);
        }
      }
    for (auto e : excess)
    {
      auto result = Insert(e);
      if (result.remainder)
      {
        e.count = result.remainder;
        RecoveryForMode().push_back(Prepare(e));
      }
    }
  }
  Creative = original;
  RefreshEquipment(*Items);
}
uint64_t UCreatureInventory::GetRevision() const
{
  nlohmann::json snapshot;
  SerializeToJson(snapshot);
  return std::hash<std::string>{}(snapshot.dump());
}
InventoryOperationResult
UCreatureInventory::TakeEquipment(bool offhand, size_t slot,
                                  InventoryEntryRef &out)
{
  out = {};
  if (!offhand && slot >= 4)
    return {0, 1, "invalid_slot"};
  auto &source = offhand ? Offhand() : Armor()[slot];
  if (source.empty)
    return {0, 1, "empty_slot"};
  out = source;
  source = {};
  if (Items)
    RefreshEquipment(*Items);
  return {1, 0, {}};
}
bool UCreatureInventory::MaterializeActive()
{
  const auto index = ActiveBarIndex * 10 + ActiveSlotIndex;
  if (!GetSlot(index).IsCatalogReference())
    return !GetSlot(index).empty;
  InventoryEntryRef unit;
  if (!Take(index, 1, unit))
    return false;
  GetHotbarsMutable()[index / 10].slots[index % 10] = {false, unit};
  return true;
}
void UCreatureInventory::SetCreative(bool creative)
{
  Creative = creative;
  if (Items)
    RefreshEquipment(*Items);
}
void UCreatureInventory::InitCreativeDefaults()
{
  SetCreative(true);
  if (CreativeHotbars[0].slots[1].empty)
  {
    InventoryEntryRef e;
    e.Id = "wood";
    e.kind = InventoryEntryKind::Block;
    e.empty = false;
    e.form = InventoryEntryForm::CatalogReference;
    e.count = 1;
    AssignToHotbar(0, 1, e);
    SetActiveSlot(0, 1);
  }
}
void UCreatureInventory::EnsureDefaultHotbar()
{
  if (Creative && IsPrimaryHotbarEmpty())
    InitCreativeDefaults();
}
bool UCreatureInventory::IsPrimaryHotbarEmpty() const
{
  for (const auto &s : GetHotbar(0).slots)
    if (!s.empty && !s.entry.empty)
      return false;
  return true;
}
void UCreatureInventory::SetObjectHotbar(const std::vector<std::string> &names)
{
  for (size_t i = 0; i < std::min(size_t{10}, names.size()); ++i)
  {
    InventoryEntryRef e;
    e.Id = names[i];
    e.kind = InventoryEntryKind::Object;
    e.empty = false;
    e.form = InventoryEntryForm::CatalogReference;
    e.count = 1;
    if (Creative)
      AssignToHotbar(1, i, e);
  }
}
const HotbarBar &UCreatureInventory::GetHotbar(size_t bar) const
{
  static const HotbarBar empty;
  return bar < 4 ? GetHotbars()[bar] : empty;
}
bool UCreatureInventory::AssignToHotbar(size_t bar, size_t slot,
                                        const InventoryEntryRef &entry)
{
  if (bar >= 4 || slot >= 10)
    return false;
  auto e = Prepare(entry);
  if (!e.empty && !e.IsCatalogReference() && e.count > StackLimit(e))
    return false;
  const auto previous = GetSlot(bar * 10 + slot);
  if (!previous.empty && !previous.IsCatalogReference() &&
      e.IsCatalogReference())
  {
    auto trial = *this;
    trial.GetHotbarsMutable()[bar].slots[slot] = {false, e};
    if (!trial.Insert(previous))
      return false;
    *this = std::move(trial);
    return true;
  }
  GetHotbarsMutable()[bar].slots[slot] = {e.empty, e};
  return true;
}
void UCreatureInventory::ClearHotbarSlot(size_t bar, size_t slot)
{
  if (bar < 4 && slot < 10)
    GetHotbarsMutable()[bar].slots[slot] = {};
}
bool UCreatureInventory::SetActiveSlot(size_t bar, size_t slot)
{
  if (bar >= 4 || slot >= 10)
    return false;
  ActiveBarIndex = bar;
  ActiveSlotIndex = slot;
  return true;
}
size_t UCreatureInventory::GetActiveSlotIndex(size_t bar) const
{
  return bar == ActiveBarIndex ? ActiveSlotIndex : 10;
}
const InventoryEntryRef *UCreatureInventory::GetActiveEntryRef() const
{
  const auto &e = GetSlot(ActiveBarIndex * 10 + ActiveSlotIndex);
  return e.empty ? nullptr : &e;
}
InventoryEntryRef *UCreatureInventory::GetActiveEntryRef()
{
  return const_cast<InventoryEntryRef *>(
      static_cast<const UCreatureInventory *>(this)->GetActiveEntryRef());
}
const std::string &UCreatureInventory::GetActiveBlockTypeName() const
{
  static const std::string empty;
  const auto *e = GetActiveEntryRef();
  return e && e->kind == InventoryEntryKind::Block ? e->Id : empty;
}
const std::string &UCreatureInventory::GetActiveObjectName() const
{
  static const std::string empty;
  const auto *e = GetActiveEntryRef();
  return e && e->kind == InventoryEntryKind::Object ? e->Id : empty;
}
const InventoryEntryRef &UCreatureInventory::GetEquippedArmor(size_t i) const
{
  static const InventoryEntryRef empty;
  return i < 4 ? Armor()[i] : empty;
}
const InventoryEntryRef &UCreatureInventory::GetEquippedOffhand() const
{
  return Offhand();
}
void UCreatureInventory::WearArmor(size_t slot, float delta,
                                   const UItemDefinitionStorage &items)
{
  if (slot >= 4)
    return;
  auto &e = Armor()[slot];
  const auto *def = !e.empty ? items.Get(e.Id) : nullptr;
  if (def)
    ApplyItemWear(e, *def, delta, true);
  RefreshEquipment(items);
}
void UCreatureInventory::WearOffhand(float delta,
                                     const UItemDefinitionStorage &items)
{
  auto &e = Offhand();
  const auto *def = !e.empty ? items.Get(e.Id) : nullptr;
  if (def)
    ApplyItemWear(e, *def, delta, true);
  RefreshEquipment(items);
}
void UCreatureInventory::RefreshEquipment(const UItemDefinitionStorage &items)
{
  EquippedArmorGroups.Ratings.clear();
  OffhandArmorGroups.Ratings.clear();
  for (size_t i = 0; i < 4; ++i)
  {
    const auto &e = Armor()[i];
    const auto *d = !e.empty && !e.broken ? items.Get(e.Id) : nullptr;
    if (!d || std::find(d->Armor.Slots.begin(), d->Armor.Slots.end(),
                        ArmorId(i)) == d->Armor.Slots.end())
      continue;
    for (const auto &p : d->Armor.ArmorGroups)
    {
      auto [rating, inserted] =
          EquippedArmorGroups.Ratings.emplace(p.first, 100);
      rating->second = static_cast<int>(
          std::lround(rating->second * std::clamp(p.second, 0, 100) / 100.f));
    }
  }
  const auto &off = Offhand();
  const auto *d = !off.empty && !off.broken ? items.Get(off.Id) : nullptr;
  if (d)
    for (const auto &p : d->Armor.ArmorGroups)
      OffhandArmorGroups.Ratings[p.first] += p.second;
}
bool UCreatureInventory::Equip(InventoryEntryRef &target,
                               const InventoryEntryRef &entry, bool consume)
{
  if (entry.empty || entry.Id.empty())
    return false;
  const auto copy = entry;
  if (target.instanceId != 0 && target.instanceId == copy.instanceId)
  {
    target = copy;
    return true;
  }
  auto trial = *this;
  InventoryEntryRef worn;
  if (consume)
  {
    bool found = false;
    for (size_t i = 0; i < kSlotCount; ++i)
    {
      const auto &e = trial.GetSlot(i);
      if (e.empty || e.Id != copy.Id || e.kind != copy.kind ||
          (copy.instanceId && e.instanceId != copy.instanceId))
        continue;
      if (!trial.Take(i, 1, worn))
        return false;
      found = true;
      break;
    }
    if (!found)
      return false;
  }
  else
  {
    worn = copy;
    worn.form = InventoryEntryForm::FiniteStack;
    worn.instanceId = 0;
    worn.count = 1;
    worn = trial.Prepare(worn);
  }
  const auto previous = target;
  if (!previous.empty && !trial.Insert(previous))
    return false;
  Hotbars = std::move(trial.Hotbars);
  CreativeHotbars = std::move(trial.CreativeHotbars);
  NextInstanceId = trial.NextInstanceId;
  target = worn;
  GearSound(AudioEventKind::Equip);
  return true;
}
bool UCreatureInventory::EquipArmor(size_t i, const InventoryEntryRef &e,
                                    const UItemDefinitionStorage &items,
                                    bool consume)
{
  if (i >= 4 || e.kind != InventoryEntryKind::Item)
    return false;
  const auto *def = items.Get(e.Id);
  if (!def || std::find(def->Armor.Slots.begin(), def->Armor.Slots.end(),
                        ArmorId(i)) == def->Armor.Slots.end())
    return false;
  SetItemDefinitions(&items);
  if (!Equip(Armor()[i], e, consume))
    return false;
  RefreshEquipment(items);
  return true;
}
bool UCreatureInventory::ReturnEquipment(InventoryEntryRef &e)
{
  if (e.empty)
    return true;
  {
    auto trial = *this;
    if (!trial.Insert(e))
      return false;
    Hotbars = std::move(trial.Hotbars);
    CreativeHotbars = std::move(trial.CreativeHotbars);
    NextInstanceId = trial.NextInstanceId;
  }
  e = {};
  GearSound(AudioEventKind::Unequip);
  return true;
}
void UCreatureInventory::UnequipArmor(size_t i,
                                      const UItemDefinitionStorage &items)
{
  if (i < 4)
    ReturnEquipment(Armor()[i]);
  RefreshEquipment(items);
}
bool UCreatureInventory::EquipOffhand(const InventoryEntryRef &e)
{
  if (e.kind != InventoryEntryKind::Item && e.kind != InventoryEntryKind::Block)
    return false;
  return Equip(Offhand(), e, false);
}
bool UCreatureInventory::EquipOffhand(const InventoryEntryRef &e,
                                      const UItemDefinitionStorage &items,
                                      bool consume)
{
  if (e.kind != InventoryEntryKind::Item && e.kind != InventoryEntryKind::Block)
    return false;
  SetItemDefinitions(&items);
  if (!Equip(Offhand(), e, consume))
    return false;
  RefreshEquipment(items);
  return true;
}
void UCreatureInventory::UnequipOffhand()
{
  ReturnEquipment(Offhand());
  OffhandArmorGroups.Ratings.clear();
}
void UCreatureInventory::UnequipOffhand(const UItemDefinitionStorage &items)
{
  ReturnEquipment(Offhand());
  RefreshEquipment(items);
}
bool UCreatureInventory::RepairActive(const UItemDefinitionStorage &items,
                                      std::string &reason)
{
  const auto *e = GetActiveEntryRef();
  const auto *d =
      e && e->kind == InventoryEntryKind::Item ? items.Get(e->Id) : nullptr;
  if (!d || d->Repair.Materials.empty() || (e->wear <= 0.f && !e->broken))
  {
    reason = "not_repairable";
    return false;
  }
  const auto id = e->instanceId;
  auto trial = *this;
  if (!Creative)
  {
    bool paid = false;
    for (const auto &material : d->Repair.Materials)
    {
      const auto kind = items.Get(material) ? InventoryEntryKind::Item
                                            : InventoryEntryKind::Block;
      if (trial.Consume(kind, material))
      {
        paid = true;
        break;
      }
    }
    if (!paid)
    {
      reason = "missing_repair_material";
      return false;
    }
  }
  auto *fixed = trial.GetActiveEntryRef();
  if (!fixed || fixed->instanceId != id)
  {
    reason = "repair_source_changed";
    return false;
  }
  fixed->wear = std::max(0.f, fixed->wear - d->Repair.Amount);
  fixed->broken = fixed->wear >= 1.f;
  *this = std::move(trial);
  reason.clear();
  return true;
}
InventoryOperationResult UCreatureInventory::QuickMove(size_t slot)
{
  if (slot >= kSlotCount || GetSlot(slot).empty)
    return {0, 0, "empty_slot"};
  const auto original = GetSlot(slot);
  const size_t activeRow = GetActiveBarIndex();
  int moved = 0;
  for (int pass = 0; pass < 2; ++pass)
    for (size_t to = 0; to < kSlotCount; ++to)
    {
      if (GetSlot(slot).empty)
        break;
      if (slot / 10 == activeRow ? to / 10 == activeRow : to / 10 != activeRow)
        continue;
      if (pass == 0 &&
          (GetSlot(to).empty || !CanMerge(GetSlot(slot), GetSlot(to))))
        continue;
      if (pass == 1 && !GetSlot(to).empty)
        continue;
      auto result = Move(slot, to);
      moved += result.transferred;
    }
  return {moved, std::max(0, original.count - moved),
          moved ? "" : "inventory_full"};
}
void UCreatureInventory::CollectMatching(size_t slot)
{
  if (slot >= kSlotCount || GetSlot(slot).empty ||
      StackLimit(GetSlot(slot)) <= 1)
    return;
  for (size_t from = 0; from < kSlotCount; ++from)
    if (from != slot && CanMerge(GetSlot(from), GetSlot(slot)))
      Move(from, slot);
}
void UCreatureInventory::Sort()
{
  std::vector<InventoryEntryRef> movable;
  for (size_t i = 0; i < kSlotCount; ++i)
    if (!GetSlot(i).empty && !GetSlot(i).pinned)
      movable.push_back(GetSlot(i));
  std::stable_sort(movable.begin(), movable.end(),
                   [](const auto &a, const auto &b)
                   {
                     return std::tie(a.kind, a.Id, a.wear, a.instanceId) <
                            std::tie(b.kind, b.Id, b.wear, b.instanceId);
                   });
  size_t next = 0;
  for (size_t i = 0; i < kSlotCount; ++i)
  {
    if (GetSlot(i).pinned)
      continue;
    auto &s = GetHotbarsMutable()[i / 10].slots[i % 10];
    s = next < movable.size() ? HotbarSlot{false, movable[next++]}
                              : HotbarSlot{};
  }
}
InventoryOperationResult UCreatureInventory::Recover(size_t index)
{
  if (index >= RecoveryForMode().size())
    return {0, 0, "invalid_recovery_slot"};
  auto e = RecoveryForMode()[index];
  if (e.kind == InventoryEntryKind::Item && (!Items || !Items->Get(e.Id)))
    return {0, e.count, "unknown_definition"};
  const auto r = Insert(e);
  if (r.remainder)
  {
    RecoveryForMode()[index].count = r.remainder;
    if (r.transferred > 0)
    {
      auto &remaining = RecoveryForMode()[index];
      remaining.instanceId = 0;
      remaining = Prepare(remaining);
    }
  }
  else
    RecoveryForMode().erase(RecoveryForMode().begin() + index);
  return r;
}
void UCreatureInventory::SerializeToJson(nlohmann::json &out) const
{
  out["inventory_version"] = 4;
  out["inventory_creative"] = Creative;
  out["next_instance_id"] = NextInstanceId;
  out["active_bar"] = ActiveBarIndex;
  out["active_slot"] = ActiveSlotIndex;
  const auto rows = [](const auto &bars)
  {
    auto a = nlohmann::json::array();
    for (const auto &row : bars)
      for (const auto &s : row.slots)
        a.push_back(WriteEntry(s.entry));
    return a;
  };
  out["owned_slots"] = rows(Hotbars);
  out["creative_slots"] = rows(CreativeHotbars);
  const auto armor = [](const auto &set)
  {
    auto a = nlohmann::json::array();
    for (const auto &e : set)
      a.push_back(WriteEntry(e));
    return a;
  };
  out["equipped_armor"] = armor(EquippedArmor);
  out["creative_armor"] = armor(CreativeArmor);
  out["equipped_offhand"] = WriteEntry(EquippedOffhand);
  out["creative_offhand"] = WriteEntry(CreativeOffhand);
  out["inventory_recovery"] = armor(Recovery);
  out["creative_recovery"] = armor(CreativeRecovery);
  out["inventory_migration_warnings"] = MigrationWarnings;
  out.erase("storage");
  out.erase("hotbars");
  out.erase("equipped_armor_groups");
}
void UCreatureInventory::DeserializeFromJson(const nlohmann::json &data, size_t)
{
  const auto *defs = Items;
  *this = UCreatureInventory{};
  Items = defs;
  auto readGear = [&](const nlohmann::json &j)
  {
    auto e = ReadEntry(j);
    if (e.IsCatalogReference())
    {
      e.form = InventoryEntryForm::FiniteStack;
      e.count = 1;
      e.instanceId = 0;
    }
    return Prepare(e);
  };
  auto readArmor = [&](const char *key, auto &set)
  {
    if (!data.contains(key) || !data[key].is_array())
      return;
    for (size_t i = 0; i < std::min(set.size(), data[key].size()); ++i)
      set[i] = readGear(data[key][i]);
  };
  if (data.value("inventory_version", 0) >= 3)
  {
    NextInstanceId = std::max(std::uint64_t{1},
                              data.value("next_instance_id", std::uint64_t{1}));
    auto readRows = [&](const char *key, auto &rows, bool creative = false)
    {
      if (!data.contains(key) || !data[key].is_array())
        return;
      size_t i = 0;
      for (const auto &j : data[key])
      {
        auto e = Prepare(ReadEntry(j));
        if (i < 40)
          rows[i / 10].slots[i % 10] = {e.empty, e};
        else if (!e.empty)
          (creative ? CreativeRecovery : Recovery).push_back(e);
        ++i;
      }
    };
    readRows("owned_slots", Hotbars);
    readRows("creative_slots", CreativeHotbars, true);
    readArmor("equipped_armor", EquippedArmor);
    readArmor("creative_armor", CreativeArmor);
    EquippedOffhand =
        readGear(data.value("equipped_offhand", nlohmann::json::object()));
    CreativeOffhand =
        readGear(data.value("creative_offhand", nlohmann::json::object()));
    if (data.contains("inventory_recovery"))
      for (const auto &j : data["inventory_recovery"])
      {
        auto e = Prepare(ReadEntry(j));
        if (!e.empty)
          RecoveryForMode().push_back(e);
      }
    if (data.contains("creative_recovery"))
      for (const auto &j : data["creative_recovery"])
      {
        auto e = Prepare(ReadEntry(j));
        if (!e.empty)
          CreativeRecovery.push_back(e);
      }
    MigrationWarnings =
        data.value("inventory_migration_warnings", std::vector<std::string>{});
    Creative = data.value("inventory_creative", false);
  }
  else
  {
    LegacyMigration = true;
    std::map<std::string, InventoryEntryRef> prototypes;
    std::map<std::string, int> quantities;
    if (data.contains("storage") && data["storage"].is_object())
      for (auto it = data["storage"].begin(); it != data["storage"].end(); ++it)
        if (it.value().is_number_integer())
          quantities[it.key()] = it.value().get<int>();
    std::vector<std::pair<size_t, InventoryEntryRef>> placements;
    size_t i = 0;
    if (data.contains("hotbars") && data["hotbars"].is_array())
      for (const auto &row : data["hotbars"])
      {
        if (!row.is_array())
          continue;
        for (const auto &j : row)
        {
          auto e = ReadEntry(j);
          if (!e.empty)
          {
            if (e.kind == InventoryEntryKind::UCreature)
            {
              if (e.Id == "test_mob" || e.Id == "scout")
                e.Id = "sheep";
              else if (e.Id == "brute")
                e.Id = "sand_monster";
              else if (e.Id == "drifter")
                e.Id = "wolf";
            }
            auto it = prototypes.find(e.Id);
            if (it != prototypes.end() &&
                (it->second.wear != e.wear || it->second.broken != e.broken))
              MigrationWarnings.push_back("ambiguous_wear:" + e.Id);
            if (it == prototypes.end() || e.wear > it->second.wear || e.broken)
              prototypes[e.Id] = e;
            if (!quantities.count(e.Id))
              quantities[e.Id] = e.count < 0 ? -1 : std::max(1, e.count);
            if (i < 40)
              placements.push_back({i, e});
          }
          ++i;
        }
      }
    for (auto &q : quantities)
    {
      auto e =
          prototypes.count(q.first) ? prototypes[q.first] : InventoryEntryRef{};
      e.Id = q.first;
      e.empty = false;
      e.instanceId = 0;
      if (!prototypes.count(q.first))
        e.kind = Items && Items->Get(q.first) ? InventoryEntryKind::Item
                                              : InventoryEntryKind::Block;
      if (q.second < 0)
      {
        for (const auto &p : placements)
          if (p.second.Id == q.first)
          {
            auto c = e;
            c.count = -1;
            CreativeHotbars[p.first / 10].slots[p.first % 10] = {false,
                                                                 Prepare(c)};
          }
        continue;
      }
      int left = q.second;
      std::set<size_t> assigned;
      for (const auto &p : placements)
        if (left > 0 && p.second.Id == q.first && assigned.empty())
        {
          e.count = std::min(StackLimit(e), left);
          Hotbars[p.first / 10].slots[p.first % 10] = {false, Prepare(e)};
          left -= e.count;
          assigned.insert(p.first);
        }
      if (left > 0)
      {
        e.count = left;
        const auto r = Insert(e);
        if (r.remainder)
        {
          e.count = r.remainder;
          RecoveryForMode().push_back(Prepare(e));
        }
      }
    }
    if (data.contains("equipped_armor") && data["equipped_armor"].is_array())
    {
      const auto &a = data["equipped_armor"];
      for (size_t s = 0; s < 4; ++s)
      {
        size_t old = a.size() >= 6 && s >= 2 ? s + 2 : s;
        if (old < a.size())
          EquippedArmor[s] = Prepare(ReadEntry(a[old]));
      }
      if (a.size() >= 6)
        for (size_t s : {size_t{2}, size_t{3}})
        {
          auto e = Prepare(ReadEntry(a[s]));
          if (!e.empty)
            RecoveryForMode().push_back(e);
        }
    }
    EquippedOffhand = Prepare(
        ReadEntry(data.value("equipped_offhand", nlohmann::json::object())));
  }
  ActiveBarIndex = std::min(size_t{3}, data.value("active_bar", size_t{0}));
  ActiveSlotIndex = std::min(size_t{9}, data.value("active_slot", size_t{0}));
  // Repair duplicate identities and invalid owned counts without deleting
  // payloads.
  std::set<std::uint64_t> seen;
  auto unique = [&](InventoryEntryRef &e)
  {
    if (e.empty)
      return;
    if (!seen.insert(e.instanceId).second)
    {
      e.instanceId = 0;
      e = Prepare(e);
      seen.insert(e.instanceId);
    }
  };
  for (auto &row : Hotbars)
    for (auto &s : row.slots)
    {
      if (s.entry.IsCatalogReference())
      {
        auto e = s.entry;
        e.form = InventoryEntryForm::CatalogReference;
        e.count = 1;
        auto &destination =
            CreativeHotbars[&row - &Hotbars[0]].slots[&s - &row.slots[0]];
        if (!destination.empty && !destination.entry.empty)
          CreativeRecovery.push_back(e);
        else
          destination = {false, e};
        s = {};
      }
      unique(s.entry);
    }
  for (auto &e : EquippedArmor)
    unique(e);
  unique(EquippedOffhand);
  for (auto &row : CreativeHotbars)
    for (auto &slot : row.slots)
      unique(slot.entry);
  for (auto &e : CreativeArmor)
    unique(e);
  unique(CreativeOffhand);
  for (auto &e : CreativeRecovery)
    unique(e);
  for (auto &e : Recovery)
    unique(e);
  Items = nullptr;
  SetItemDefinitions(defs);
}
} // namespace cutum

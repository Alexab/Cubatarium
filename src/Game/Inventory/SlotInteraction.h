#ifndef SLOT_INTERACTION_H
#define SLOT_INTERACTION_H

#include "Game/Inventory/InventoryTypes.h"
#include "Gui/Interfaces/IUContentCatalog.h"
#include <cstddef>
#include <string>

namespace cutum
{

enum class SlotSurface
{
  None,
  PaletteGrid,
  BackpackReturn,
  CreativeDelete,
  Hotbar,
  CharacterArmor,
  CharacterOffhand,
  /// Drop on the paper-doll preview: equip into the item's own armor slot.
  CharacterDoll
};

struct SlotAddress
{
  SlotSurface surface{SlotSurface::None};
  size_t bar{0};
  size_t slot{0};
  ContentKind paletteKind{ContentKind::Block};
  std::string entryId;
};

struct DragState
{
  bool Active{false};
  uint64_t sourceRevision{0}, sourceOwner{0};
  bool sourceCreative{false};
  InventoryEntryRef entry;
  SlotAddress source;
};

} // namespace cutum

#endif

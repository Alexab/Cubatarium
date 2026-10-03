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
  Hotbar,
  CharacterArmor,
  CharacterOffhand
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
  InventoryEntryRef entry;
  SlotAddress source;
};

} // namespace cutum

#endif

#ifndef SLOT_INTERACTION_H
#define SLOT_INTERACTION_H

#include "Gui/Interfaces/IContentCatalog.h"
#include "InventoryTypes.h"
#include <cstddef>
#include <string>

namespace cutum
{

enum class SlotSurface
{
  None,
  PaletteGrid,
  Hotbar
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
  bool active{false};
  InventoryEntryRef entry;
  SlotAddress source;
};

} // namespace cutum

#endif

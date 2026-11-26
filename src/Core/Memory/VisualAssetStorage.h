#pragma once

#include "Core/Memory/StlSourceStorage.h"

namespace cutum
{

struct VisualAssetStorage
{
  size_t inlineBytes{}, tableBytes{}, controlBytes{}, payloadBytes{}, cpuBytes{};
  size_t live{}, expired{}, negative{}, aliases{};
  bool supported{};
};

template<class T> void AddVisualAssetControl(StlSourceStorage &control) noexcept
{
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  control.Add(sizeof(std::_Ref_count_obj2<T>));
#else
  (void)control;
#endif
}

inline void FinishVisualAssetStorage(VisualAssetStorage &out, const StlSourceStorage &table,
                                      const StlSourceStorage &control, const StlSourceStorage &payload) noexcept
{
  StlSourceStorage total;
  out.tableBytes = table.bytes.requested;
  out.controlBytes = control.bytes.requested;
  out.payloadBytes = payload.bytes.requested;
  for (size_t bytes : {out.tableBytes, out.controlBytes, out.payloadBytes})
    total.Add(bytes);
  out.cpuBytes = total.bytes.requested;
  out.supported = total.bytes.supported && table.bytes.supported && control.bytes.supported && payload.bytes.supported;
}

// Each key is published by its loader with an independent make_shared object.
// An expired weak key still retains that coallocation, including its dead shell.
template<class Asset, class Table, class Negative, class Payload>
VisualAssetStorage InspectWeakVisualAssets(size_t inlineBytes, const std::string &root,
    const Table &entries, const Negative &failed, Payload measure) noexcept
{
  VisualAssetStorage out;
  out.inlineBytes = inlineBytes;
  StlSourceStorage table, control, payload;
  table.Text(root);
  table.Hash(entries);
  table.Hash(failed);
  for (const auto &key : failed)
    table.Text(key);
  out.negative = failed.size();
  for (const auto &[key, weak] : entries)
  {
    table.Text(key);
    AddVisualAssetControl<Asset>(control);
    if (const auto asset = weak.lock())
    {
      ++out.live;
      measure(*asset, payload);
    }
    else
      ++out.expired;
  }
  FinishVisualAssetStorage(out, table, control, payload);
  return out;
}

} // namespace cutum

#include "WorldGen/Core/WorldGenContentPin.h"
#include "WorldGen/Core/WorldGenContentPinTls.h"

namespace cutum
{

WorldGenContentSnapshot CaptureWorldGenContentSnapshot()
{
  WorldGenContentSnapshot snap;
  snap.Pack = UWorldGenPack::GetSnapshot();
  snap.Features = UObjectFeatureConfigStorage::GetSnapshot();
  snap.Refs = UWorldGenRefs::GetSnapshot();
  return snap;
}

WorldGenContentPinScope::WorldGenContentPinScope(WorldGenContentSnapshot snapshot)
    : Snapshot(std::move(snapshot))
{
  SetPinnedWorldGenContent(&Snapshot);
}

WorldGenContentPinScope::~WorldGenContentPinScope()
{
  if (GetPinnedWorldGenContent() == &Snapshot)
  {
    SetPinnedWorldGenContent(nullptr);
  }
}

} // namespace cutum

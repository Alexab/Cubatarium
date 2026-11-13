#include "WorldGen/Core/WorldGenContentPin.h"
#include "WorldGen/Core/WorldGenContentPinTls.h"

namespace cutum
{

WorldGenContentSnapshot CaptureWorldGenContentSnapshot()
{
  WorldGenContentSnapshot snap;
  snap.Pack = UWorldGenPack::GetSnapshot();
  if (snap.Pack)
  {
    snap.Biomes = snap.Pack->BiomeRegistry;
  }
  snap.Features = UObjectFeatureConfigStorage::GetSnapshot();
  snap.Refs = UWorldGenRefs::GetSnapshot();
  return snap;
}

UWorldGenContentPinScope::UWorldGenContentPinScope(
    WorldGenContentSnapshot snapshot)
    : Snapshot(std::move(snapshot))
{
  SetPinnedWorldGenContent(&Snapshot);
}

UWorldGenContentPinScope::~UWorldGenContentPinScope()
{
  if (GetPinnedWorldGenContent() == &Snapshot)
  {
    SetPinnedWorldGenContent(nullptr);
  }
}

} // namespace cutum

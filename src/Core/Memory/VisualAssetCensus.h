#pragma once

#include "Core/Memory/RootStorageSource.h"
#include "Core/Memory/VisualAssetStorage.h"
#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Visual/CreatureDrawQueue.h"
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include "Items/ItemDrawRequest.h"
#include <array>
#include <map>

namespace cutum
{

struct LegacyItemPartsMesh
{
  std::vector<BoneSkeletonCubeMeshCpu> meshes;
  glm::mat4 fit{1.f};
};

enum class VisualAssetKind { Rigid, Gltf, Skeleton, Item, LegacyParts, Palette, ItemDraw, Count };
enum class VisualAssetOwner { Cache, World, Preview, Queue, Viewmodel, Equipment, Diagnostic, Count };

struct VisualAssetCensusRow
{
  size_t cached{}, detached{}, shared{}, detachedControlBytes{}, detachedPayloadBytes{};
  std::array<size_t, static_cast<size_t>(VisualAssetOwner::Count)> references{};
};

struct VisualAssetCensusResult
{
  std::array<VisualAssetCensusRow, static_cast<size_t>(VisualAssetKind::Count)> rows;
  size_t observerBytes{}, legacyTableBytes{}, legacyInlineBytes{}, queueBytes{};
  size_t unmeasuredReferences{}, nonowningMeshes{};
  bool supported{};
};

void MeasureVisualPayload(const CreatureRigidModelParts &, StlSourceStorage &) noexcept;
void MeasureVisualPayload(const CreatureGltfMeshAsset &, StlSourceStorage &) noexcept;
void MeasureVisualPayload(const CreatureBoneSkeletonMeshAsset &, StlSourceStorage &) noexcept;
void MeasureVisualPayload(const ItemRenderAsset &, StlSourceStorage &) noexcept;

inline void MeasureVisualPayload(const LegacyItemPartsMesh &parts, StlSourceStorage &out) noexcept
{
  out.Buffer(parts.meshes.capacity(), sizeof(BoneSkeletonCubeMeshCpu));
  for (const auto &mesh : parts.meshes)
  {
    out.Buffer(mesh.interleavedPosUv.capacity(), sizeof(float));
    out.Buffer(mesh.indices.capacity(), sizeof(unsigned));
  }
}

inline void MeasureVisualPayload(const std::vector<glm::mat4> &palette, StlSourceStorage &out) noexcept
{
  out.Buffer(palette.capacity(), sizeof(glm::mat4));
}

inline void MeasureVisualPayload(const ItemDrawRequest &draw, StlSourceStorage &out) noexcept
{
  out.Text(draw.Path.native());
  out.Hash(draw.State.NodeTransforms);
  out.Hash(draw.State.HiddenNodes);
  out.Hash(draw.State.VisibleRoots);
  for (const auto &entry : draw.State.NodeTransforms)
    out.Text(entry.first);
  for (const auto &id : draw.State.HiddenNodes)
    out.Text(id);
  for (const auto &id : draw.State.VisibleRoots)
    out.Text(id);
}

// A snapshot pins each observed control once. The observer's own allocation
// is reported separately and is released with the snapshot.
class VisualAssetCensus
{
public:
  template<class T>
  void Add(const std::shared_ptr<T> &owner, VisualAssetOwner role)
  {
    if (!owner)
      return;
    using Asset = std::remove_const_t<T>;
    const auto *asset = Coallocated<Asset>(RootOwnerWords(owner)[1]);
    if (asset != owner.get())
    {
      ++UnmeasuredReferences;
      return;
    }
    Record(owner, *asset, role);
  }

  void AddSkeletal(const std::shared_ptr<const BoneSkeletonCubeMeshCpu> &mesh)
  {
    if (!mesh)
      return;
    if (mesh.use_count() == 0)
    {
      ++NonowningMeshes;
      return;
    }
    const auto control = RootOwnerWords(mesh)[1];
    if (const auto *gltf = Coallocated<CreatureGltfMeshAsset>(control))
      Record(mesh, *gltf, VisualAssetOwner::Queue);
    else if (const auto *skeleton = Coallocated<CreatureBoneSkeletonMeshAsset>(control))
      Record(mesh, *skeleton, VisualAssetOwner::Queue);
    else if (const auto *parts = Coallocated<LegacyItemPartsMesh>(control))
      Record(mesh, *parts, VisualAssetOwner::Queue);
    else
      ++UnmeasuredReferences;
  }

  void AddSkinned(const std::shared_ptr<const GltfPrimitiveCpu> &mesh)
  {
    if (!mesh)
      return;
    if (const auto *asset = Coallocated<CreatureGltfMeshAsset>(RootOwnerWords(mesh)[1]))
      Record(mesh, *asset, VisualAssetOwner::Queue);
    else
      ++UnmeasuredReferences;
  }

  template<class Map>
  void AddLegacyCache(size_t inlineBytes, const Map *table, VisualAssetOwner role)
  {
    LegacyInline.Add(inlineBytes);
    if (!table)
      return;
    LegacyTables.Hash(*table);
    for (const auto &[key, asset] : *table)
    {
      LegacyTables.Text(key);
      Add(asset, role);
    }
  }

  void AddQueue(const CreatureDrawQueue &queue)
  {
    Queues.Buffer(queue.InspectRequests().capacity(), sizeof(CreatureDrawRequest));
    for (const auto &draw : queue.InspectRequests())
    {
      AddSkeletal(draw.SkeletalMesh);
      AddSkinned(draw.SkinnedPrimitive);
      Add(draw.BoneMatrices, VisualAssetOwner::Queue);
      Add(draw.ItemSurface, VisualAssetOwner::Queue);
      if (draw.ItemSurface)
        Add(draw.ItemSurface->Asset, VisualAssetOwner::Queue);
    }
  }

  VisualAssetCensusResult Result() const noexcept
  {
    VisualAssetCensusResult out{};
    StlSourceStorage observer;
    observer.Tree(Assets);
    out.observerBytes = observer.bytes.requested;
    out.legacyTableBytes = LegacyTables.bytes.requested;
    out.legacyInlineBytes = LegacyInline.bytes.requested;
    out.queueBytes = Queues.bytes.requested;
    out.unmeasuredReferences = UnmeasuredReferences;
    out.nonowningMeshes = NonowningMeshes;
    out.supported = RootOwnerRepresentationSupported && observer.bytes.supported &&
        LegacyTables.bytes.supported && LegacyInline.bytes.supported && Queues.bytes.supported &&
        UnmeasuredReferences == 0;
    std::array<StlSourceStorage, static_cast<size_t>(VisualAssetKind::Count)> controls, payloads;
    for (const auto &[owner, asset] : Assets)
    {
      const auto kind = static_cast<size_t>(asset.kind);
      auto &row = out.rows[kind];
      for (size_t role = 0; role < row.references.size(); ++role)
        row.references[role] += asset.references[role];
      if (asset.references[static_cast<size_t>(VisualAssetOwner::Cache)])
      {
        ++row.cached;
        if (asset.references[static_cast<size_t>(VisualAssetOwner::Cache)] != asset.totalReferences)
          ++row.shared;
      }
      else
      {
        ++row.detached;
        controls[kind].Add(asset.controlBytes);
        asset.measure(owner.get(), payloads[kind]);
      }
    }
    for (size_t kind = 0; kind < out.rows.size(); ++kind)
    {
      out.rows[kind].detachedControlBytes = controls[kind].bytes.requested;
      out.rows[kind].detachedPayloadBytes = payloads[kind].bytes.requested;
      out.supported &= controls[kind].bytes.supported && payloads[kind].bytes.supported;
    }
    return out;
  }

private:
  template<class T>
  static const T *Coallocated(uintptr_t address) noexcept
  {
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
    using Control = std::_Ref_count_obj2<T>;
    if constexpr (RootOwnerRepresentationSupported && alignof(Control) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__)
    {
      const auto *base = reinterpret_cast<const std::_Ref_count_base *>(address);
      if (base && typeid(*base) == typeid(Control))
        return std::addressof(static_cast<const Control *>(base)->_Storage._Value);
    }
#else
    (void)address;
#endif
    return nullptr;
  }

  template<class T> static constexpr VisualAssetKind Kind()
  {
    if constexpr (std::is_same_v<T, CreatureRigidModelParts>) return VisualAssetKind::Rigid;
    else if constexpr (std::is_same_v<T, CreatureGltfMeshAsset>) return VisualAssetKind::Gltf;
    else if constexpr (std::is_same_v<T, CreatureBoneSkeletonMeshAsset>) return VisualAssetKind::Skeleton;
    else if constexpr (std::is_same_v<T, ItemRenderAsset>) return VisualAssetKind::Item;
    else if constexpr (std::is_same_v<T, LegacyItemPartsMesh>) return VisualAssetKind::LegacyParts;
    else if constexpr (std::is_same_v<T, std::vector<glm::mat4>>) return VisualAssetKind::Palette;
    else
    {
      static_assert(std::is_same_v<T, ItemDrawRequest>);
      return VisualAssetKind::ItemDraw;
    }
  }

  template<class Owner, class Asset>
  void Record(const std::shared_ptr<Owner> &owner, const Asset &asset, VisualAssetOwner role)
  {
    auto found = Assets.find(owner);
    if (found == Assets.end())
    {
      StlSourceStorage control;
      AddVisualAssetControl<Asset>(control);
      found = Assets.try_emplace(std::shared_ptr<const void>(owner, &asset),
          Entry{Kind<Asset>(), control.bytes.requested,
              [](const void *value, StlSourceStorage &out) noexcept
              {
                MeasureVisualPayload(*static_cast<const Asset *>(value), out);
              }}).first;
    }
    ++found->second.references[static_cast<size_t>(role)];
    ++found->second.totalReferences;
  }

  struct Entry
  {
    VisualAssetKind kind;
    size_t controlBytes;
    void (*measure)(const void *, StlSourceStorage &) noexcept;
    std::array<size_t, static_cast<size_t>(VisualAssetOwner::Count)> references{};
    size_t totalReferences{};
  };
  std::map<std::shared_ptr<const void>, Entry, std::owner_less<>> Assets;
  StlSourceStorage LegacyTables, LegacyInline, Queues;
  size_t UnmeasuredReferences{}, NonowningMeshes{};
};

} // namespace cutum

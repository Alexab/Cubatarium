#pragma once

#include "Core/Memory/StlSourceStorage.h"
#include <climits>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <vector>

namespace cutum
{

struct ItemSceneTraversalStorage
{
  size_t nodes{}, visited{}, maxDepth{}, frameCapacity{};
  size_t visitedRequested{}, frameGrowthRequested{}, peakRequested{};
  bool supported{StlSourceStorage::SupportedProfile()};
};

struct ItemSceneTraversalSnapshot
{
  size_t started{}, completed{}, unsupported{};
  ItemSceneTraversalStorage largest;
};

namespace item_scene_detail
{
inline std::mutex Mutex;
inline ItemSceneTraversalSnapshot Snapshot;
}

inline ItemSceneTraversalSnapshot GetItemSceneTraversals()
{
  const std::lock_guard lock(item_scene_detail::Mutex);
  return item_scene_detail::Snapshot;
}

// Advance the original scene preorder without a call frame per ancestor.
// A frame holds only the next sibling range and the emitted parent's index.
class ItemSceneCursor
{
public:
  struct Step
  {
    const nlohmann::json *Node;
    int Parent;
  };

  ItemSceneCursor(const nlohmann::json &nodes, const nlohmann::json &roots)
      : Nodes(nodes), Visited(nodes.size(), false)
  {
    Storage.nodes = Visited.size();
    StlSourceStorage visited;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
    visited.Buffer(Visited.capacity() / (sizeof(std::_Vbase) * CHAR_BIT),
                   sizeof(std::_Vbase));
#endif
    Storage.visitedRequested = visited.bytes.requested;
    Storage.peakRequested = Storage.visitedRequested;
    Storage.supported &= visited.bytes.supported;
    if (!roots.empty())
      Push(roots, -1);
    const std::lock_guard lock(item_scene_detail::Mutex);
    ++item_scene_detail::Snapshot.started;
  }

  ItemSceneCursor(const ItemSceneCursor &) = delete;
  ItemSceneCursor &operator=(const ItemSceneCursor &) = delete;
  ~ItemSceneCursor()
  {
    const std::lock_guard lock(item_scene_detail::Mutex);
    auto &stats = item_scene_detail::Snapshot;
    stats.completed += Complete;
    stats.unsupported += !Storage.supported;
    if (Storage.peakRequested > stats.largest.peakRequested)
      stats.largest = Storage;
  }

  std::optional<Step> Next()
  {
    while (!Frames.empty())
    {
      auto &frame = Frames.back();
      if (frame.Next == frame.End)
      {
        Frames.pop_back();
        continue;
      }
      const auto index = (frame.Next++)->get<size_t>();
      if (index >= Visited.size() || Visited[index])
        throw std::runtime_error("Item glTF: cyclic or shared scene node");
      if (Storage.visited > static_cast<size_t>(INT_MAX))
        throw std::runtime_error("Item glTF: node index exceeds the scene representation");
      Visited[index] = true;
      const auto &node = Nodes.at(index);
      const int parent = frame.Parent;
      const auto emitted = static_cast<int>(Storage.visited++);
      Storage.maxDepth = std::max(Storage.maxDepth, Frames.size());
      if (const auto children = node.find("children"); children != node.end() && !children->empty())
        Push(*children, emitted);
      return Step{&node, parent};
    }
    Complete = true;
    return {};
  }

  ItemSceneTraversalStorage InspectStorage() const noexcept { return Storage; }

private:
  struct Frame
  {
    nlohmann::json::const_iterator Next, End;
    int Parent;
  };

  void Push(const nlohmann::json &siblings, int parent)
  {
    if (Frames.size() == Frames.capacity())
    {
      // One root range and at most one range for each individually admitted
      // node. The node array's element count fits before the extra root slot.
      const auto limit = std::min(Visited.size() + 1, Frames.max_size());
      const auto old = Frames.capacity();
      if (old == limit)
        throw std::length_error("Item glTF: scene traversal exceeds its node storage");
      const auto next = old ? old + std::min(old, limit - old) : size_t{1};
      Frames.reserve(next);
      Storage.frameCapacity = Frames.capacity();

      StlSourceStorage live;
      live.Buffer(old, sizeof(Frame));
      live.Buffer(Frames.capacity(), sizeof(Frame));
      Storage.frameGrowthRequested = std::max(Storage.frameGrowthRequested, live.bytes.requested);
      live.Add(Storage.visitedRequested);
      Storage.peakRequested = std::max(Storage.peakRequested, live.bytes.requested);
      Storage.supported &= live.bytes.supported;
    }
    Frames.push_back({siblings.begin(), siblings.end(), parent});
  }

  const nlohmann::json &Nodes;
  std::vector<bool> Visited;
  std::vector<Frame> Frames;
  ItemSceneTraversalStorage Storage;
  bool Complete{};
};

} // namespace cutum

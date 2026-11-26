#ifndef ICONWARMUPQUEUE_H
#define ICONWARMUPQUEUE_H

#include "Core/Memory/StlSourceStorage.h"
#include <list>
#include <string>
#include <string_view>
#include <unordered_set>

namespace cutum
{

/// FIFO work queue for budgeted icon generation, shared by the icon caches
/// Push deduplicates and RunFront retires work after the consumer completes.
/// Rebuilding a warmup list = Clear() + Push loop.
class UGuiIconWarmupQueue
{
public:
  UGuiIconWarmupQueue() = default;
  UGuiIconWarmupQueue(const UGuiIconWarmupQueue &) = delete;
  UGuiIconWarmupQueue &operator=(const UGuiIconWarmupQueue &) = delete;

  /// Returns false when the id is already queued.
  bool Push(const std::string &id)
  {
    if (id.empty() || Queued.find(id) != Queued.end())
      return false;
    const auto &name = Queue.emplace_back(id);
    try
    {
      Queued.emplace(name);
    }
    catch (...)
    {
      Queue.pop_back();
      throw;
    }
    return true;
  }

  template<class Consumer> bool RunFront(Consumer &&consume)
  {
    if (Queue.empty())
      return false;
    // The consumer may enqueue work, but cannot clear or drain this queue.
    // On an exception the original front and its deduplication entry remain.
    consume(static_cast<const std::string &>(Queue.front()));
    Queued.erase(Queue.front());
    Queue.pop_front();
    return true;
  }

  bool Empty() const { return Queue.empty(); }
  size_t Size() const { return Queue.size(); }
  bool Contains(std::string_view name) const { return Queued.find(name) != Queued.end(); }
  NameStorageEnvelope InspectOwnedStorage() const noexcept
  {
    StlSourceStorage storage;
    storage.List(Queue);
    storage.Hash(Queued);
    storage.bytes.supported &= Queue.size() == Queued.size();
    for (const auto &name : Queue)
    {
      const auto view = Queued.find(name);
      storage.bytes.supported &= view != Queued.end() && view->data() == name.data();
      storage.Text(name);
    }
    return storage.bytes;
  }
  void Swap(UGuiIconWarmupQueue &other) noexcept
  {
    Queue.swap(other.Queue);
    Queued.swap(other.Queued);
  }
  void Clear()
  {
    Queued.clear();
    Queue.clear();
  }

private:
  std::list<std::string> Queue;
  std::unordered_set<std::string_view> Queued;
};

} // namespace cutum

#endif

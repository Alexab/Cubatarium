#ifndef ICONWARMUPQUEUE_H
#define ICONWARMUPQUEUE_H

#include <deque>
#include <string>
#include <unordered_set>

namespace cutum
{

/// FIFO work queue for budgeted icon generation, shared by the icon caches
/// Push deduplicates and RunFront retires work after the consumer completes.
/// Rebuilding a warmup list = Clear() + Push loop.
class UGuiIconWarmupQueue
{
public:
  /// Returns false when the id is already queued.
  bool Push(const std::string &id)
  {
    if (id.empty())
      return false;
    const auto [entry, inserted] = Queued.insert(id);
    if (!inserted)
      return false;
    try
    {
      Queue.push_back(id);
    }
    catch (...)
    {
      Queued.erase(entry);
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
  void Swap(UGuiIconWarmupQueue &other) noexcept
  {
    Queue.swap(other.Queue);
    Queued.swap(other.Queued);
  }
  void Clear()
  {
    Queue.clear();
    Queued.clear();
  }

private:
  std::deque<std::string> Queue;
  std::unordered_set<std::string> Queued;
};

} // namespace cutum

#endif

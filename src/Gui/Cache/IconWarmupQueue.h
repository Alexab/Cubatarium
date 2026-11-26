#ifndef ICONWARMUPQUEUE_H
#define ICONWARMUPQUEUE_H

#include <deque>
#include <optional>
#include <string>
#include <unordered_set>

namespace cutum
{

/// FIFO work queue for budgeted icon generation, shared by the icon caches
/// (UX R2 unification): Push deduplicates, PopFront yields the next id.
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

  std::optional<std::string> PopFront()
  {
    if (Queue.empty())
    {
      return std::nullopt;
    }
    std::string id = std::move(Queue.front());
    Queue.pop_front();
    Queued.erase(id);
    return id;
  }

  bool Empty() const { return Queue.empty(); }
  size_t Size() const { return Queue.size(); }
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

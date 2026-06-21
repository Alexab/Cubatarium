#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace cutum
{

class UJobThreadPool
{
public:
  explicit UJobThreadPool(std::size_t threadCount = 0);
  ~UJobThreadPool();

  UJobThreadPool(const UJobThreadPool &) = delete;
  UJobThreadPool &operator=(const UJobThreadPool &) = delete;

  void Enqueue(std::function<void()> job);
  void WaitIdle();

private:
  void WorkerLoop();

  std::vector<std::thread> Workers;
  std::mutex QueueMutex;
  std::condition_variable QueueCv;
  std::deque<std::function<void()>> Jobs;
  std::size_t ActiveJobs{0};
  bool Stop{false};
};

template <typename T>
class CompletedJobQueue
{
public:
  void Push(T value)
  {
    std::lock_guard<std::mutex> lock(Mutex);
    Items.push_back(std::move(value));
  }

  std::vector<T> DrainAll()
  {
    std::lock_guard<std::mutex> lock(Mutex);
    std::vector<T> drained;
    drained.swap(Items);
    return drained;
  }

  bool Empty() const
  {
    std::lock_guard<std::mutex> lock(Mutex);
    return Items.empty();
  }

private:
  mutable std::mutex Mutex;
  std::vector<T> Items;
};

} // namespace cutum

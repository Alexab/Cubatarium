#include "Audio/AudioEventQueue.h"

#include <atomic>
#include <iostream>
#include <thread>

namespace
{

constexpr const char *kTestName = "audio_event_queue_test";

void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << kTestName << " FAIL: " << msg << "\n";
    std::exit(1);
  }
}

} // namespace

int main()
{
  cutum::UAudioEventQueue queue(4);
  Expect(queue.Capacity() == 4, "capacity");
  Expect(queue.DiscardedOverflow() == 0, "no overflow at start");

  cutum::UAudioEvent event;
  event.Kind = cutum::AudioEventKind::Footstep;
  event.SetId = 7;
  queue.Post(event);
  queue.Post(event);

  std::vector<cutum::UAudioEvent> drained;
  Expect(queue.DrainWait(drained, 0), "drain with events");
  Expect(drained.size() == 2, "drained two");
  Expect(drained[0].Kind == cutum::AudioEventKind::Footstep, "kind intact");
  Expect(drained[0].SetId == 7, "set id intact");
  Expect(!queue.DrainWait(drained, 10), "timeout drain empty");

  // Overflow evicts the oldest and keeps counting.
  for (int i = 0; i < 6; ++i)
  {
    event.SetId = static_cast<std::uint32_t>(i);
    queue.Post(event);
  }
  Expect(queue.DrainWait(drained, 0), "drain after overflow");
  Expect(drained.size() == 4, "ring keeps capacity");
  Expect(drained.front().SetId == 2, "oldest evicted");
  Expect(queue.DiscardedOverflow() == 2, "two dropped");

  // A blocked consumer wakes up on the next post.
  std::atomic<bool> consumerDone{false};
  std::atomic<int> consumedCount{0};
  std::thread consumer(
      [&]()
      {
        std::vector<cutum::UAudioEvent> batch;
        if (queue.DrainWait(batch, 2000))
        {
          consumedCount.store(static_cast<int>(batch.size()));
        }
        consumerDone.store(true);
      });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  Expect(!consumerDone.load(), "consumer still waiting");
  queue.Post(event);
  consumer.join();
  Expect(consumerDone.load(), "consumer woke");
  Expect(consumedCount.load() == 1, "consumer got the event");

  std::cout << kTestName << " OK\n";
  return 0;
}

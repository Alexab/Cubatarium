#include "Core/Jobs/JobThreadPool.h"

#include <iostream>
#include <string>

namespace
{

constexpr const char *kTestName = "completed_job_queue_test";

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
  cutum::UCompletedJobQueue<int> q;
  q.SetCapacity(3);
  Expect(q.Capacity() == 3, "capacity set");
  Expect(q.Size() == 0, "empty start");

  Expect(!q.PushDropOldest(1), "push 1 no drop");
  Expect(!q.PushDropOldest(2), "push 2 no drop");
  Expect(!q.PushDropOldest(3), "push 3 no drop");
  Expect(q.Size() == 3, "full size");

  int dropped = -1;
  Expect(q.PushDropOldest(4, &dropped), "push 4 drops oldest");
  Expect(dropped == 1, "dropped was 1");
  Expect(q.Size() == 3, "size stays at cap");
  Expect(q.DiscardedOverflow() == 1, "discarded++");

  Expect(q.PushDropOldest(5, &dropped), "push 5 drops");
  Expect(dropped == 2, "dropped was 2");
  Expect(q.DiscardedOverflow() == 2, "discarded==2");

  auto drained = q.DrainAll();
  Expect(drained.size() == 3, "drain 3");
  Expect(drained[0] == 3 && drained[1] == 4 && drained[2] == 5,
         "kept newest 3,4,5");
  Expect(q.Size() == 0, "empty after drain");
  Expect(q.Empty(), "Empty()");

  Expect(!q.Any([](int) { return true; }), "Any empty false");
  Expect(!q.PushDropOldest(10), "push for Any");
  Expect(!q.PushDropOldest(20), "push for Any 2");
  Expect(q.Any([](int v) { return v == 20; }), "Any finds 20");
  Expect(!q.Any([](int v) { return v == 99; }), "Any misses 99");
  (void)q.DrainAll();

  // Population invalidations must survive the per-frame drain limit. New
  // arrivals while a suffix is pending must also preserve FIFO across wrap.
  cutum::UCompletedJobQueue<int> population;
  for (int i = 0; i < 1800; ++i)
    population.Push(i);
  auto first_frame = population.DrainUpTo(512);
  Expect(first_frame.size() == 512 && population.Size() == 1288,
         "bounded drain retains events beyond the frame budget");
  for (int i = 1800; i < 2500; ++i)
    population.Push(i);
  int expected = 0;
  for (int value : first_frame)
    Expect(value == expected++, "initial population batch ordered");
  while (!population.Empty())
  {
    const auto frame = population.DrainUpTo(512);
    Expect(frame.size() <= 512, "population drain work remains bounded");
    for (int value : frame)
      Expect(value == expected++, "no lost or duplicated population event");
  }
  Expect(expected == 2500 && population.DiscardedOverflow() == 0,
         "all population events eventually delivered");

  std::cout << kTestName << " OK\n";
  return 0;
}

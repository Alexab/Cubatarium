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

  std::cout << kTestName << " OK\n";
  return 0;
}

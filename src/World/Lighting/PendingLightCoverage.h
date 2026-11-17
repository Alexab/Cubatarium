#pragma once

#include "World/Chunks/Chunk.h"
#include <algorithm>
#include <set>
#include <vector>

namespace cutum
{
/// Durable section obligations. Queue membership and mesh publication are not
/// evidence of completion. Call Reconcile only with a current input proof.
class PendingLightCoverage
{
public:
  // Envelope for legacy scheduling adapters; holes remain exact in Sections.
  int min_y{0};
  int max_y{0};

  std::vector<int> Request(int first_y, int last_y)
  {
    return RequestIf(first_y, last_y, [](int) { return true; });
  }

  template <typename NeedsWork>
  std::vector<int> RequestIf(int first_y, int last_y, NeedsWork needs_work)
  {
    std::vector<int> added;
    if (last_y < first_y)
      return added;
    const int first = SectionY(first_y);
    const int last = SectionY(last_y);
    for (int cy = first;; ++cy)
    {
      if (needs_work(cy) && Sections.insert(cy).second)
        added.push_back(cy);
      if (cy == last)
        break;
    }
    UpdateEnvelope();
    return added;
  }

  template <typename CurrentProof> std::size_t Reconcile(CurrentProof proof)
  {
    std::size_t completed = 0;
    for (auto it = Sections.begin(); it != Sections.end();)
    {
      if (proof(*it))
      {
        it = Sections.erase(it);
        ++completed;
      }
      else
        ++it;
    }
    UpdateEnvelope();
    return completed;
  }

  // Only exact, non-clippable queue work can own the entire remaining debt.
  bool IsOwnedByBand(bool exact, bool defined, int first_y, int last_y) const
  {
    return !Empty() && exact && defined && first_y <= min_y && last_y >= max_y;
  }

  bool Contains(int cy) const { return Sections.count(cy) != 0; }
  bool Empty() const { return Sections.empty(); }
  std::size_t Size() const { return Sections.size(); }
  const std::set<int> &PendingSections() const { return Sections; }

private:
  static int SectionY(int y)
  {
    const int quotient = y / CHUNK_SIZE;
    return quotient - (y % CHUNK_SIZE < 0 ? 1 : 0);
  }
  void UpdateEnvelope()
  {
    if (Sections.empty())
    {
      min_y = max_y = 0;
      return;
    }
    min_y = *Sections.begin() * CHUNK_SIZE;
    max_y = *Sections.rbegin() * CHUNK_SIZE + CHUNK_SIZE - 1;
  }
  std::set<int> Sections;
};
} // namespace cutum

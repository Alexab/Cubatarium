#include "Core/Progress/IUProgressSink.h"

namespace cutum
{

void ULatestProgressSink::Begin(WorldOperationKind kind)
{
  Snapshot = ProgressSnapshot{};
  Snapshot.kind = kind;
  Snapshot.active = true;
  Snapshot.succeeded = true;
  Snapshot.fraction = -1.f;
  MaxFractionSeen = -1.f;
}

void ULatestProgressSink::Report(const std::string &phaseId, float fraction,
                                 const std::string &message, int subDone,
                                 int subTotal)
{
  Snapshot.phaseId = phaseId;
  Snapshot.message = message;
  Snapshot.subDone = subDone;
  Snapshot.subTotal = subTotal;
  if (fraction >= 0.f)
  {
    if (fraction > MaxFractionSeen)
    {
      MaxFractionSeen = fraction;
    }
    Snapshot.fraction = MaxFractionSeen;
  }
  else
  {
    Snapshot.fraction = fraction;
  }
  Snapshot.active = true;
}

void ULatestProgressSink::End(bool success, const std::string &errorMessage)
{
  Snapshot.active = false;
  Snapshot.succeeded = success;
  Snapshot.errorMessage = errorMessage;
  if (!success && !errorMessage.empty())
  {
    Snapshot.message = errorMessage;
  }
}

} // namespace cutum

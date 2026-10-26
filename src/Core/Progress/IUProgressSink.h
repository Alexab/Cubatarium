#pragma once

#include "Core/Progress/ProgressTypes.h"
#include <string>

namespace cutum
{

class IUProgressSink
{
public:
  virtual ~IUProgressSink() = default;

  virtual void Begin(WorldOperationKind kind) = 0;
  /// subDone/subTotal are optional structured substage counters (-1 when a
  /// phase has no meaningful "i/N"); screens render them without parsing
  /// message strings.
  virtual void Report(const std::string &phaseId, float fraction,
                      const std::string &message, int subDone = -1,
                      int subTotal = -1) = 0;
  virtual void End(bool success, const std::string &errorMessage = {}) = 0;
};

class UNullProgressSink : public IUProgressSink
{
public:
  void Begin(WorldOperationKind /*kind*/) override {}
  void Report(const std::string & /*phaseId*/, float /*fraction*/,
              const std::string & /*message*/, int /*subDone*/,
              int /*subTotal*/) override
  {
  }
  void End(bool /*success*/, const std::string & /*errorMessage*/) override {}
};

class ULatestProgressSink : public IUProgressSink
{
public:
  void Begin(WorldOperationKind kind) override;
  void Report(const std::string &phaseId, float fraction,
              const std::string &message, int subDone = -1,
              int subTotal = -1) override;
  void End(bool success, const std::string &errorMessage = {}) override;

  const ProgressSnapshot &Get() const { return Snapshot; }

private:
  ProgressSnapshot Snapshot;
  /// Determinate fractions never regress on screen; raw producer values
  /// stay available in the logs.
  float MaxFractionSeen{-1.f};
};

} // namespace cutum

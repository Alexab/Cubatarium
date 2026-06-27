#pragma once

#include "Core/Progress/ProgressTypes.h"
#include <string>

namespace cutum
{

class IProgressSink
{
public:
  virtual ~IProgressSink() = default;

  virtual void Begin(WorldOperationKind kind) = 0;
  virtual void Report(const std::string &phaseId, float fraction,
                      const std::string &message) = 0;
  virtual void End(bool success, const std::string &errorMessage = {}) = 0;
};

class UNullProgressSink : public IProgressSink
{
public:
  void Begin(WorldOperationKind /*kind*/) override {}
  void Report(const std::string & /*phaseId*/, float /*fraction*/,
              const std::string & /*message*/) override
  {
  }
  void End(bool /*success*/, const std::string & /*errorMessage*/) override {}
};

class ULatestProgressSink : public IProgressSink
{
public:
  void Begin(WorldOperationKind kind) override;
  void Report(const std::string &phaseId, float fraction,
              const std::string &message) override;
  void End(bool success, const std::string &errorMessage = {}) override;

  const ProgressSnapshot &Get() const { return Snapshot; }

private:
  ProgressSnapshot Snapshot;
};

} // namespace cutum

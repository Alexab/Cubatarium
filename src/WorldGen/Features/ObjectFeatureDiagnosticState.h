#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace cutum
{
struct ObjectFeatureRule;
struct ObjectFeatureRuleCounters
{
  uint64_t evaluated{0},candidates{0},placementAttempts{0},placed{0};
  double evaluationMs{0},placementMs{0};
  uint64_t prefabVoxelVisits{0},prefabSurfaceScans{0};
  void Add(const ObjectFeatureRuleCounters &other) noexcept {
    evaluated+=other.evaluated; candidates+=other.candidates;
    placementAttempts+=other.placementAttempts; placed+=other.placed;
    evaluationMs+=other.evaluationMs; placementMs+=other.placementMs;
    prefabVoxelVisits+=other.prefabVoxelVisits; prefabSurfaceScans+=other.prefabSurfaceScans;
  }
};

// Derived numeric payload belongs to this immutable catalog, never a global
// name/history map. There are exactly N cells/aliases/flags for N decor rules.
class ObjectFeatureDiagnosticCatalog
{
public:
  explicit ObjectFeatureDiagnosticCatalog(const std::vector<ObjectFeatureRule> &rules);
  const uint64_t revision;
  size_t RuleCount() const noexcept {return Totals.size();}
  size_t CapacityBytes() const noexcept {
    return Totals.capacity()*sizeof(ObjectFeatureRuleCounters)+
        WarningAliases.capacity()*sizeof(size_t)+Warned.capacity()*sizeof(uint8_t);
  }
  void Accumulate(const ObjectFeatureRuleCounters *cells,size_t count) noexcept;
  std::vector<ObjectFeatureRuleCounters> CopyTotals() const;
  bool MarkMissing(size_t index);
  size_t WarningCount() const;
private:
  mutable std::mutex Mutex;
  std::vector<ObjectFeatureRuleCounters> Totals;
  std::vector<size_t> WarningAliases;
  std::vector<uint8_t> Warned;
};

// Prepare after finalizing the immutable rules. Copies start a fresh revision;
// derived counters must not follow a copied builder that changes its rules.
class ObjectFeatureDiagnosticStorage
{
public:
  ObjectFeatureDiagnosticStorage()=default;
  ObjectFeatureDiagnosticStorage(const ObjectFeatureDiagnosticStorage &) noexcept {}
  ObjectFeatureDiagnosticStorage &operator=(const ObjectFeatureDiagnosticStorage &) noexcept;
  std::shared_ptr<ObjectFeatureDiagnosticCatalog> Get(const std::vector<ObjectFeatureRule> &rules) const;
private:
  mutable std::shared_ptr<ObjectFeatureDiagnosticCatalog> State;
};
} // namespace cutum

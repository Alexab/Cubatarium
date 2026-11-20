#pragma once
#include <system_error>

#include "World/Chunks/ChunkManager.h"
#include "World/IO/BinaryChunkSerializer.h"
#include "World/IO/ChunkStorageTypes.h"
#include "World/IO/JsonChunkSerializer.h"
#include "World/Lighting/PersistedSectionLightTypes.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace cutum
{

inline constexpr int kNoChunkSliceOnDisk = std::numeric_limits<int>::min();

class UBlockRegistry;
class UBlockWorld;
class UChunk;

struct ColumnLightCaptureStats
{
  size_t fields{0}, padded{0}, current{0}, retained_current{0},
      equivalent_catalog{0}, stale_inputs{0}, no_context{0}, exported{0},
      bytes{0};
  size_t frozen_context{0}, complete_context{0}, recorded_output{0};
  bool has_stale_sample{false};
  glm::ivec3 stale_coord{0};
  int stale_reason{0}, absent_before{0}, absent_now{0};
  uint64_t incarnation_before{0}, incarnation_now{0}, content_before{0},
      content_now{0};
};

class UChunkStorageService
{
public:
  explicit UChunkStorageService(ChunkStorageSettings settings = {});
  /// Optional main-thread capture diagnostics, independent of acceptance.
  std::function<void(glm::ivec3, const ColumnLightCaptureStats &)>
      ObserveLightCapture;

  void SetSettings(const ChunkStorageSettings &settings);
  const ChunkStorageSettings &GetSettings() const { return Settings; }
  void SetWriteFormat(ChunkWriteFormat format)
  {
    Settings.writeFormat = format;
  }

  static bool HasChunkFilesOnDisk(const std::string &worldFolder);
  static std::string ChunksDir(const std::string &worldFolder);

  std::string ChunkFilePath(const std::string &worldFolder, glm::ivec3 coord,
                            ChunkDiskFormat format) const;
  ChunkDiskFormat DetectFormatOnDisk(const std::string &worldFolder,
                                     glm::ivec3 coord) const;

  /// Main-thread recheck for a missing internal slice of a saved column.
  /// Ungenerated columns, pending saves and filesystem errors remain unknown.
  std::optional<LoadedAirSlice>
  ReadKnownAbsentTerrainSlice(const std::string &worldFolder,
                              glm::ivec3 coord) const;

  bool SaveChunk(glm::ivec3 chunkCoord, const UChunk &chunk,
                 const std::string &worldFolder, UBlockRegistry &registry,
                 std::error_code *failure = nullptr);
  int LoadChunk(glm::ivec3 chunkCoord, UBlockWorld &world,
                const std::string &worldFolder, UBlockRegistry &registry);

  int GetHighestChunkSliceOnDisk(const std::string &worldFolder,
                                 glm::ivec3 groundCoord) const;
  /// Exact membership from the same complete directory index as the highest
  /// slice query. Absence alone does not certify terrain generation or AIR.
  bool IsChunkSliceAbsentOnDisk(const std::string &worldFolder,
                                glm::ivec3 chunkCoord) const;
  /// Update the initialized disk index after an asynchronous slice write.
  void RecordChunkSliceSaved(const std::string &worldFolder,
                             glm::ivec3 chunkCoord) const;
  /// Build the per-world directory index before terrain streaming starts.
  /// Call from a background I/O job; later lookups are constant-time.
  void PrepareHighestChunkSliceIndex(const std::string &worldFolder) const;
  bool RemoveChunkSliceFromDisk(const std::string &worldFolder,
                                glm::ivec3 chunkCoord,
                                std::string *outError = nullptr) const;
  /// Delete all Y-slices for a ground column (stale/incomplete purge).
  bool RemoveTerrainColumnFromDisk(const std::string &worldFolder,
                                   glm::ivec3 groundCoord, int maxWorldY,
                                   std::string *outError = nullptr,
                                   int minWorldY = 0) const;

  /// Refuse unknown slices before any metadata/terrain write; false also
  /// reports a filesystem write failure. Existing disk height is mandatory.
  bool SaveTerrainColumn(glm::ivec3 groundCoord, const UBlockWorld &world,
                         const std::string &worldFolder,
                         UBlockRegistry &registry, int maxWorldY,
                         int minWorldY = 0,
                         std::error_code *failure = nullptr);
  static std::string ColumnLightFilePath(const std::string &worldFolder,
                                         glm::ivec3 column);
  std::shared_ptr<const PersistedColumnLight>
  CaptureColumnLight(glm::ivec3 column, const UBlockWorld &world,
                     UBlockRegistry &registry) const;
  bool WriteColumnLight(const std::string &worldFolder, glm::ivec3 column,
                        const PersistedColumnLight &receipts,
                        std::error_code *failure = nullptr) const;
  std::shared_ptr<const PersistedColumnLight>
  ReadColumnLight(const std::string &worldFolder, glm::ivec3 column) const;
  int LoadTerrainColumn(glm::ivec3 groundCoord, UBlockWorld &world,
                        const std::string &worldFolder,
                        UBlockRegistry &registry, int maxWorldY,
                        int minWorldY = 0);

  SerializedChunk SerializeChunk(glm::ivec3 chunkCoord, const UChunk &chunk,
                                 UBlockRegistry &registry) const;
  UChunkBuffer DeserializeChunk(const std::vector<uint8_t> &bytes,
                                glm::ivec3 chunkCoord, ChunkDiskFormat format,
                                UBlockRegistry &registry) const;
  int ApplyBufferToWorld(const UChunkBuffer &buffer, UBlockWorld &world) const;

  bool WriteBytesAtomically(const std::string &filePath,
                            const std::vector<uint8_t> &bytes,
                            std::error_code *failure = nullptr) const;
  bool ReadBytesFromFile(const std::string &filePath,
                         std::vector<uint8_t> &outBytes) const;

  bool WriteStorageMarker(const std::string &worldFolder,
                          std::error_code *failure = nullptr) const;
  void ApplyStorageMarkerFromDisk(const std::string &worldFolder);

  uint64_t GetKnownAirEvidenceRevision() const
  {
    return KnownAirEvidenceRevision.load(std::memory_order_acquire);
  }
  uint64_t GetKnownAirEvidenceIdentity() const
  {
    return KnownAirEvidenceIdentity;
  }
  bool IsColumnSavePending(glm::ivec3 groundCoord) const;
  void MarkColumnSavePending(glm::ivec3 groundCoord);
  void ClearColumnSavePending(glm::ivec3 groundCoord);

  const IUChunkSerializer &GetSerializer(ChunkDiskFormat format) const;
  const IUChunkSerializer &GetWriteSerializer() const;

private:
  friend class UWorldPersistence;
  /// Caller has just completed all reads without failure. Each indexed frame
  /// must also have a complete resident base; allocations cannot clear a fault.
  bool ResolveTerrainColumnRead(UBlockWorld &world,
                                const std::string &worldFolder,
                                glm::ivec3 column, int maxWorldY,
                                int minWorldY);
  struct DiskTerrainColumnKey
  {
    int x{0};
    int z{0};

    bool operator==(const DiskTerrainColumnKey &other) const noexcept
    {
      return x == other.x && z == other.z;
    }
  };

  struct DiskTerrainColumnKeyHash
  {
    size_t operator()(const DiskTerrainColumnKey &key) const noexcept
    {
      const uint64_t packed = (static_cast<uint64_t>(
                                   static_cast<uint32_t>(key.x))
                               << 32) |
                              static_cast<uint32_t>(key.z);
      return std::hash<uint64_t>{}(packed);
    }
  };

  struct DiskTerrainColumnIndex
  {
    bool initialized{false};
    std::unordered_map<DiskTerrainColumnKey, int,
                       DiskTerrainColumnKeyHash>
        highest_cy;
    std::unordered_map<DiskTerrainColumnKey, std::unordered_set<int>,
                       DiskTerrainColumnKeyHash>
        slice_cys;
    std::unordered_set<DiskTerrainColumnKey, DiskTerrainColumnKeyHash>
        dirty_columns;
  };

  IUChunkSerializer &MutableSerializer(ChunkDiskFormat format);

  std::string HighestChunkSliceIndexKey(const std::string &worldFolder) const;
  void BuildHighestChunkSliceIndex(const std::string &worldFolder,
                                   DiskTerrainColumnIndex &index) const;
  int ScanHighestChunkSliceOnDisk(
      const std::string &worldFolder, glm::ivec3 groundCoord,
      std::unordered_set<int> *slices = nullptr) const;
  void RefreshDiskColumnIndex(const std::string &worldFolder,
                              glm::ivec3 groundCoord,
                              DiskTerrainColumnIndex &index) const;

  inline static std::atomic<uint64_t> NextKnownAirEvidenceIdentity{1};
  uint64_t KnownAirEvidenceIdentity{NextKnownAirEvidenceIdentity.fetch_add(1)};
  mutable std::atomic<uint64_t> KnownAirEvidenceRevision{0};
  ChunkStorageSettings Settings;
  UJsonChunkSerializer JsonSerializer;
  UBinaryChunkSerializer BinarySerializer;
  std::unordered_set<glm::ivec3, IVec3Hash> PendingSaveColumns;
  mutable std::mutex HighestChunkSliceCacheMutex;
  mutable std::unordered_map<std::string, DiskTerrainColumnIndex>
      HighestChunkSliceIndexByFolder;
};

} // namespace cutum

#pragma once

#include "World/Chunks/ChunkManager.h"
#include "World/IO/BinaryChunkSerializer.h"
#include "World/IO/ChunkStorageTypes.h"
#include "World/IO/JsonChunkSerializer.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;
class UChunk;

class UChunkStorageService
{
public:
  explicit UChunkStorageService(ChunkStorageSettings settings = {});

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

  bool SaveChunk(glm::ivec3 chunkCoord, const UChunk &chunk,
                 const std::string &worldFolder, UBlockRegistry &registry);
  int LoadChunk(glm::ivec3 chunkCoord, UBlockWorld &world,
                const std::string &worldFolder, UBlockRegistry &registry);

  int GetHighestChunkSliceOnDisk(const std::string &worldFolder,
                                 glm::ivec3 groundCoord) const;
  /// Build the per-world directory index before terrain streaming starts.
  /// Call from a background I/O job; later lookups are constant-time.
  void PrepareHighestChunkSliceIndex(const std::string &worldFolder) const;
  void RemoveChunkSliceFromDisk(const std::string &worldFolder,
                                glm::ivec3 chunkCoord) const;
  /// Delete all Y-slices for a ground column (stale/incomplete purge).
  void RemoveTerrainColumnFromDisk(const std::string &worldFolder,
                                   glm::ivec3 groundCoord,
                                   int maxWorldY) const;

  void SaveTerrainColumn(glm::ivec3 groundCoord, const UBlockWorld &world,
                         const std::string &worldFolder,
                         UBlockRegistry &registry, int maxWorldY);
  int LoadTerrainColumn(glm::ivec3 groundCoord, UBlockWorld &world,
                        const std::string &worldFolder,
                        UBlockRegistry &registry, int maxWorldY);

  SerializedChunk SerializeChunk(glm::ivec3 chunkCoord, const UChunk &chunk,
                                 UBlockRegistry &registry) const;
  UChunkBuffer DeserializeChunk(const std::vector<uint8_t> &bytes,
                                glm::ivec3 chunkCoord, ChunkDiskFormat format,
                                UBlockRegistry &registry) const;
  int ApplyBufferToWorld(const UChunkBuffer &buffer, UBlockWorld &world) const;

  bool WriteBytesAtomically(const std::string &filePath,
                            const std::vector<uint8_t> &bytes) const;
  bool ReadBytesFromFile(const std::string &filePath,
                         std::vector<uint8_t> &outBytes) const;

  void WriteStorageMarker(const std::string &worldFolder) const;
  void ApplyStorageMarkerFromDisk(const std::string &worldFolder);

  bool IsColumnSavePending(glm::ivec3 groundCoord) const;
  void MarkColumnSavePending(glm::ivec3 groundCoord);
  void ClearColumnSavePending(glm::ivec3 groundCoord);

  const IUChunkSerializer &GetSerializer(ChunkDiskFormat format) const;
  const IUChunkSerializer &GetWriteSerializer() const;

private:
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
    std::unordered_set<DiskTerrainColumnKey, DiskTerrainColumnKeyHash>
        dirty_columns;
  };

  IUChunkSerializer &MutableSerializer(ChunkDiskFormat format);

  std::string HighestChunkSliceIndexKey(const std::string &worldFolder) const;
  void BuildHighestChunkSliceIndex(const std::string &worldFolder,
                                   DiskTerrainColumnIndex &index) const;
  int ScanHighestChunkSliceOnDisk(const std::string &worldFolder,
                                  glm::ivec3 groundCoord) const;

  ChunkStorageSettings Settings;
  UJsonChunkSerializer JsonSerializer;
  UBinaryChunkSerializer BinarySerializer;
  std::unordered_set<glm::ivec3, IVec3Hash> PendingSaveColumns;
  mutable std::mutex HighestChunkSliceCacheMutex;
  mutable std::unordered_map<std::string, DiskTerrainColumnIndex>
      HighestChunkSliceIndexByFolder;
};

} // namespace cutum

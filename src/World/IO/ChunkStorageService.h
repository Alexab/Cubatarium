#pragma once

#include "World/Chunks/ChunkManager.h"
#include "World/IO/BinaryChunkSerializer.h"
#include "World/IO/ChunkStorageTypes.h"
#include "World/IO/JsonChunkSerializer.h"
#include <memory>
#include <string>
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

  const IChunkSerializer &GetSerializer(ChunkDiskFormat format) const;
  const IChunkSerializer &GetWriteSerializer() const;

private:
  IChunkSerializer &MutableSerializer(ChunkDiskFormat format);

  ChunkStorageSettings Settings;
  UJsonChunkSerializer JsonSerializer;
  UBinaryChunkSerializer BinarySerializer;
  std::unordered_set<glm::ivec3, IVec3Hash> PendingSaveColumns;
};

} // namespace cutum

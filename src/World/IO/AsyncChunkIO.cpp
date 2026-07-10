#include "World/IO/AsyncChunkIO.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include "World/IO/ChunkStorageService.h"
#include "World/IO/JsonChunkSerializer.h"
#include <fstream>

namespace cutum
{

void UAsyncChunkIO::RequestLoad(glm::ivec3 coord, UChunkStorageService &storage,
                                const std::string &worldFolder,
                                ChunkGenerationToken token)
{
  const ChunkDiskFormat format = storage.DetectFormatOnDisk(worldFolder, coord);
  if (format == ChunkDiskFormat::Absent)
  {
    AsyncChunkLoadResult result;
    result.coord = coord;
    result.token = token;
    CompletedLoads.Push(std::move(result));
    return;
  }

  const std::string filePath =
      storage.ChunkFilePath(worldFolder, coord, format);
  Pool.Enqueue(
      [this, coord, filePath, token, format]()
      {
        AsyncChunkLoadResult result;
        result.coord = coord;
        result.token = token;
        result.format = format;
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
          CompletedLoads.Push(std::move(result));
          return;
        }
        result.payload.assign(std::istreambuf_iterator<char>(file),
                              std::istreambuf_iterator<char>());
        result.success = !result.payload.empty();
        CompletedLoads.Push(std::move(result));
      });
}

void UAsyncChunkIO::RequestSave(glm::ivec3 coord, UChunkStorageService &storage,
                                const std::string &worldFolder,
                                const UBlockWorld &world,
                                UBlockRegistry &registry,
                                ChunkGenerationToken token)
{
  const UChunk *chunk = world.GetChunkManager().GetChunk(coord);
  if (!chunk)
  {
    return;
  }
  const SerializedChunk serialized =
      storage.SerializeChunk(coord, *chunk, registry);
  const std::string filePath =
      storage.ChunkFilePath(worldFolder, coord, serialized.format);
  const glm::ivec3 ground(coord.x, 0, coord.z);
  (void)token;
  Pool.Enqueue(
      [this, filePath, serialized, coord, ground]()
      {
        std::filesystem::create_directories(
            std::filesystem::path(filePath).parent_path());
        const std::string tempPath = filePath + ".tmp";
        {
          std::ofstream file(tempPath, std::ios::binary);
          if (!file.is_open())
          {
            return;
          }
          file.write(reinterpret_cast<const char *>(serialized.bytes.data()),
                     static_cast<std::streamsize>(serialized.bytes.size()));
          if (!file.good())
          {
            std::filesystem::remove(tempPath);
            return;
          }
        }
        std::error_code ec;
        std::filesystem::rename(tempPath, filePath, ec);
        if (ec)
        {
          std::filesystem::remove(filePath, ec);
          ec.clear();
          std::filesystem::rename(tempPath, filePath, ec);
        }
        AsyncChunkSaveRequest done;
        done.coord = coord;
        done.groundCoord = ground;
        done.payload = serialized.bytes;
        done.filePath = filePath;
        done.format = serialized.format;
        CompletedSaves.Push(std::move(done));
      });
}

std::vector<AsyncChunkLoadResult> UAsyncChunkIO::DrainLoads()
{
  return CompletedLoads.DrainAll();
}

std::vector<AsyncChunkLoadResult> UAsyncChunkIO::DrainLoadsUpTo(
    std::size_t max_count)
{
  return CompletedLoads.DrainUpTo(max_count);
}

std::vector<AsyncChunkSaveRequest> UAsyncChunkIO::DrainSaves()
{
  return CompletedSaves.DrainAll();
}

void UAsyncChunkIO::WaitIdle()
{
  Pool.WaitIdle();
}

bool UAsyncChunkIO::CompletedLoadsEmpty() const
{
  return CompletedLoads.Empty();
}

bool UAsyncChunkIO::CompletedSavesEmpty() const
{
  return CompletedSaves.Empty();
}

UChunkBuffer ParseChunkJsonToBuffer(const std::string &jsonText,
                                    glm::ivec3 chunkCoord,
                                    UBlockRegistry &registry)
{
  UJsonChunkSerializer serializer;
  std::vector<uint8_t> bytes(jsonText.begin(), jsonText.end());
  return serializer.Deserialize(bytes, chunkCoord, registry);
}

std::string SerializeChunkToJson(glm::ivec3 chunkCoord, const UChunk &chunk,
                                 UBlockRegistry &registry)
{
  UJsonChunkSerializer serializer;
  const SerializedChunk serialized =
      serializer.Serialize(chunkCoord, chunk, registry);
  return std::string(serialized.bytes.begin(), serialized.bytes.end());
}

} // namespace cutum

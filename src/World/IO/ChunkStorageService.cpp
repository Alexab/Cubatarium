#include "World/IO/ChunkStorageService.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Core/BlockWorld.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace cutum
{

using json = nlohmann::json;

namespace
{

std::string CoordStem(glm::ivec3 coord)
{
  return std::to_string(coord.x) + "_" + std::to_string(coord.y) + "_" +
         std::to_string(coord.z);
}

bool HasExtensionFiles(const std::filesystem::path &chunks_dir,
                       const char *extension)
{
  if (!std::filesystem::exists(chunks_dir) ||
      !std::filesystem::is_directory(chunks_dir))
  {
    return false;
  }
  for (const auto &entry : std::filesystem::directory_iterator(chunks_dir))
  {
    if (entry.path().extension() == extension)
    {
      return true;
    }
  }
  return false;
}

} // namespace

UChunkStorageService::UChunkStorageService(ChunkStorageSettings settings)
    : Settings(std::move(settings))
{
}

void UChunkStorageService::SetSettings(const ChunkStorageSettings &settings)
{
  Settings = settings;
}

bool UChunkStorageService::HasChunkFilesOnDisk(const std::string &worldFolder)
{
  const std::filesystem::path chunks_dir = ChunksDir(worldFolder);
  return HasExtensionFiles(chunks_dir, ".cchunk") ||
         HasExtensionFiles(chunks_dir, ".json");
}

std::string UChunkStorageService::ChunksDir(const std::string &worldFolder)
{
  return worldFolder + "/chunks";
}

std::string UChunkStorageService::ChunkFilePath(const std::string &worldFolder,
                                                glm::ivec3 coord,
                                                ChunkDiskFormat format) const
{
  const std::string ext = format == ChunkDiskFormat::Json
                              ? JsonSerializer.FileExtension()
                              : BinarySerializer.FileExtension();
  return ChunksDir(worldFolder) + "/" + CoordStem(coord) + ext;
}

ChunkDiskFormat
UChunkStorageService::DetectFormatOnDisk(const std::string &worldFolder,
                                         glm::ivec3 coord) const
{
  const std::string binaryPath =
      ChunkFilePath(worldFolder, coord, ChunkDiskFormat::Binary);
  if (std::filesystem::exists(binaryPath))
  {
    return ChunkDiskFormat::Binary;
  }
  const std::string jsonPath =
      ChunkFilePath(worldFolder, coord, ChunkDiskFormat::Json);
  if (std::filesystem::exists(jsonPath))
  {
    return ChunkDiskFormat::Json;
  }
  return ChunkDiskFormat::Absent;
}

const IUChunkSerializer &
UChunkStorageService::GetSerializer(ChunkDiskFormat format) const
{
  return format == ChunkDiskFormat::Json
             ? static_cast<const IUChunkSerializer &>(JsonSerializer)
             : static_cast<const IUChunkSerializer &>(BinarySerializer);
}

IUChunkSerializer &
UChunkStorageService::MutableSerializer(ChunkDiskFormat format)
{
  return format == ChunkDiskFormat::Json
             ? static_cast<IUChunkSerializer &>(JsonSerializer)
             : static_cast<IUChunkSerializer &>(BinarySerializer);
}

const IUChunkSerializer &UChunkStorageService::GetWriteSerializer() const
{
  return Settings.writeFormat == ChunkWriteFormat::Json
             ? static_cast<const IUChunkSerializer &>(JsonSerializer)
             : static_cast<const IUChunkSerializer &>(BinarySerializer);
}

SerializedChunk
UChunkStorageService::SerializeChunk(glm::ivec3 chunkCoord, const UChunk &chunk,
                                     UBlockRegistry &registry) const
{
  return GetWriteSerializer().Serialize(chunkCoord, chunk, registry);
}

UChunkBuffer UChunkStorageService::DeserializeChunk(
    const std::vector<uint8_t> &bytes, glm::ivec3 chunkCoord,
    ChunkDiskFormat format, UBlockRegistry &registry) const
{
  if (format == ChunkDiskFormat::Absent || bytes.empty())
  {
    return {};
  }
  return GetSerializer(format).Deserialize(bytes, chunkCoord, registry);
}

int UChunkStorageService::ApplyBufferToWorld(const UChunkBuffer &buffer,
                                             UBlockWorld &world) const
{
  if (buffer.IsEmpty())
  {
    return 0;
  }
  const size_t before = world.CountNonAir();
  buffer.ApplyTo(world);
  const size_t after = world.CountNonAir();
  return static_cast<int>(after > before ? after - before : 0);
}

bool UChunkStorageService::WriteBytesAtomically(
    const std::string &filePath, const std::vector<uint8_t> &bytes) const
{
  if (bytes.empty())
  {
    return false;
  }
  const std::filesystem::path target(filePath);
  std::filesystem::create_directories(target.parent_path());
  const std::filesystem::path tempPath = filePath + ".tmp";
  {
    std::ofstream file(tempPath, std::ios::binary);
    if (!file.is_open())
    {
      return false;
    }
    file.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!file.good())
    {
      std::filesystem::remove(tempPath);
      return false;
    }
  }
  std::error_code ec;
  std::filesystem::rename(tempPath, target, ec);
  if (ec)
  {
    std::filesystem::remove(target, ec);
    ec.clear();
    std::filesystem::rename(tempPath, target, ec);
  }
  return !ec;
}

bool UChunkStorageService::ReadBytesFromFile(
    const std::string &filePath, std::vector<uint8_t> &outBytes) const
{
  std::ifstream file(filePath, std::ios::binary);
  if (!file.is_open())
  {
    return false;
  }
  outBytes.assign(std::istreambuf_iterator<char>(file),
                  std::istreambuf_iterator<char>());
  return !outBytes.empty();
}

bool UChunkStorageService::SaveChunk(glm::ivec3 chunkCoord, const UChunk &chunk,
                                     const std::string &worldFolder,
                                     UBlockRegistry &registry)
{
  const SerializedChunk serialized =
      SerializeChunk(chunkCoord, chunk, registry);
  const std::string filePath =
      ChunkFilePath(worldFolder, chunkCoord, serialized.format);
  if (!WriteBytesAtomically(filePath, serialized.bytes))
  {
    return false;
  }

  if (Settings.writeFormat == ChunkWriteFormat::Binary &&
      Settings.deleteLegacyJsonOnBinarySave)
  {
    const std::string legacyJson =
        ChunkFilePath(worldFolder, chunkCoord, ChunkDiskFormat::Json);
    std::error_code ec;
    std::filesystem::remove(legacyJson, ec);
  }
  return true;
}

int UChunkStorageService::LoadChunk(glm::ivec3 chunkCoord, UBlockWorld &world,
                                    const std::string &worldFolder,
                                    UBlockRegistry &registry)
{
  const ChunkDiskFormat format = DetectFormatOnDisk(worldFolder, chunkCoord);
  if (format == ChunkDiskFormat::Absent)
  {
    return -1;
  }

  std::vector<uint8_t> bytes;
  const std::string filePath = ChunkFilePath(worldFolder, chunkCoord, format);
  if (!ReadBytesFromFile(filePath, bytes))
  {
    return -1;
  }

  const UChunkBuffer buffer =
      DeserializeChunk(bytes, chunkCoord, format, registry);
  const int placed = ApplyBufferToWorld(buffer, world);
  return placed;
}

int UChunkStorageService::GetHighestChunkSliceOnDisk(
    const std::string &worldFolder, glm::ivec3 groundCoord) const
{
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  const std::filesystem::path chunks_dir(ChunksDir(worldFolder));
  if (!std::filesystem::exists(chunks_dir) ||
      !std::filesystem::is_directory(chunks_dir))
  {
    return -1;
  }

  const std::string prefix = std::to_string(groundCoord.x) + "_";
  const std::string suffix = "_" + std::to_string(groundCoord.z);
  int highest = -1;
  for (const auto &entry : std::filesystem::directory_iterator(chunks_dir))
  {
    const std::string stem = entry.path().stem().string();
    if (stem.size() <= prefix.size() + suffix.size())
    {
      continue;
    }
    if (stem.compare(0, prefix.size(), prefix) != 0 ||
        stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) != 0)
    {
      continue;
    }
    try
    {
      const std::string cy_text = stem.substr(
          prefix.size(), stem.size() - prefix.size() - suffix.size());
      const int cy = std::stoi(cy_text);
      highest = std::max(highest, cy);
    }
    catch (const std::exception &)
    {
    }
  }
  return highest;
}

void UChunkStorageService::RemoveChunkSliceFromDisk(
    const std::string &worldFolder, glm::ivec3 chunkCoord) const
{
  for (const ChunkDiskFormat format :
       {ChunkDiskFormat::Binary, ChunkDiskFormat::Json})
  {
    const std::string filePath = ChunkFilePath(worldFolder, chunkCoord, format);
    std::error_code ec;
    std::filesystem::remove(filePath, ec);
  }
}

void UChunkStorageService::SaveTerrainColumn(glm::ivec3 groundCoord,
                                             const UBlockWorld &world,
                                             const std::string &worldFolder,
                                             UBlockRegistry &registry,
                                             int maxWorldY)
{
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  const int maxCy = (maxWorldY + CHUNK_SIZE - 1) / CHUNK_SIZE;
  const int highestOnDisk = GetHighestChunkSliceOnDisk(worldFolder, groundCoord);
  const int highestNonAir =
      GetHighestNonAirChunkSlice(world, groundCoord, maxWorldY);
  int highestToSave = std::max(highestOnDisk, highestNonAir);
  if (highestToSave < 0)
  {
    return;
  }
  highestToSave = std::min(highestToSave, maxCy);

  for (int cy = 0; cy <= highestToSave; ++cy)
  {
    const glm::ivec3 slice(groundCoord.x, cy, groundCoord.z);
    const UChunk *chunk = world.GetChunkManager().GetChunk(slice);
    if (chunk)
    {
      SaveChunk(slice, *chunk, worldFolder, registry);
    }
    else
    {
      const UChunk empty_slice(slice);
      SaveChunk(slice, empty_slice, worldFolder, registry);
    }
  }

  for (int cy = highestToSave + 1; cy <= maxCy; ++cy)
  {
    RemoveChunkSliceFromDisk(
        worldFolder, glm::ivec3(groundCoord.x, cy, groundCoord.z));
  }
}

int UChunkStorageService::LoadTerrainColumn(glm::ivec3 groundCoord,
                                            UBlockWorld &world,
                                            const std::string &worldFolder,
                                            UBlockRegistry &registry,
                                            int maxWorldY)
{
  if (groundCoord.y != 0)
  {
    return -1;
  }
  if (IsColumnSavePending(groundCoord))
  {
    return -1;
  }

  int total = 0;
  const int maxCy = (maxWorldY + CHUNK_SIZE - 1) / CHUNK_SIZE;
  const int highestOnDisk = GetHighestChunkSliceOnDisk(worldFolder, groundCoord);
  if (highestOnDisk < 0)
  {
    return 0;
  }
  for (int cy = 0; cy <= highestOnDisk; ++cy)
  {
    const int placed = LoadChunk(glm::ivec3(groundCoord.x, cy, groundCoord.z),
                                 world, worldFolder, registry);
    if (placed > 0)
    {
      total += placed;
    }
  }
  MaterializeRequiredTerrainColumnSlices(world, groundCoord, maxWorldY,
                                         highestOnDisk);
  (void)maxCy;
  return total > 0 ? total : 1;
}

void UChunkStorageService::WriteStorageMarker(
    const std::string &worldFolder) const
{
  json marker;
  marker["format_version"] = 4;
  marker["storage"] = ChunkWriteFormatToString(Settings.writeFormat);
  marker["legacy_json"] = true;
  const std::string chunks_file = worldFolder + "/chunks.json";
  std::ofstream markerFile(chunks_file);
  if (markerFile.is_open())
  {
    markerFile << marker.dump();
  }
}

void UChunkStorageService::ApplyStorageMarkerFromDisk(
    const std::string &worldFolder)
{
  const std::string chunks_file = worldFolder + "/chunks.json";
  if (!std::filesystem::exists(chunks_file))
  {
    return;
  }
  try
  {
    std::ifstream file(chunks_file);
    if (!file.is_open())
    {
      return;
    }
    const json marker = json::parse(file);
    if (marker.contains("storage") && marker["storage"].is_string())
    {
      Settings.writeFormat =
          ChunkWriteFormatFromString(marker["storage"].get<std::string>());
    }
  }
  catch (const json::exception &)
  {
  }
}

bool UChunkStorageService::IsColumnSavePending(glm::ivec3 groundCoord) const
{
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  return PendingSaveColumns.count(groundCoord) > 0;
}

void UChunkStorageService::MarkColumnSavePending(glm::ivec3 groundCoord)
{
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  PendingSaveColumns.insert(groundCoord);
}

void UChunkStorageService::ClearColumnSavePending(glm::ivec3 groundCoord)
{
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  PendingSaveColumns.erase(groundCoord);
}

} // namespace cutum

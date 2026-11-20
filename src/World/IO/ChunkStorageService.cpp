#include "World/IO/ChunkStorageService.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Core/BlockWorld.h"
#include "World/IO/TerrainSliceLoad.h"
#include "World/Lighting/PersistedSectionLight.h"
#include "World/Math/GridMath.h"
#include <algorithm>
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

bool TryParseInt(const std::string &text, int &out)
{
  try
  {
    size_t parsed = 0;
    out = std::stoi(text, &parsed);
    return parsed == text.size();
  }
  catch (const std::exception &)
  {
    return false;
  }
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
  KnownAirEvidenceRevision.fetch_add(1, std::memory_order_release);
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

std::string UChunkStorageService::HighestChunkSliceIndexKey(
    const std::string &worldFolder) const
{
  return std::filesystem::path(ChunksDir(worldFolder))
      .lexically_normal()
      .string();
}

void UChunkStorageService::BuildHighestChunkSliceIndex(
    const std::string &worldFolder, DiskTerrainColumnIndex &index) const
{
  const std::filesystem::path chunks_dir(ChunksDir(worldFolder));
  if (!std::filesystem::exists(chunks_dir) ||
      !std::filesystem::is_directory(chunks_dir))
  {
    index.initialized = true;
    KnownAirEvidenceRevision.fetch_add(1, std::memory_order_release);
    return;
  }

  for (const auto &entry : std::filesystem::directory_iterator(chunks_dir))
  {
    const std::string extension = entry.path().extension().string();
    if (extension != JsonSerializer.FileExtension() &&
        extension != BinarySerializer.FileExtension())
    {
      continue;
    }
    const std::string stem = entry.path().stem().string();
    const size_t first_sep = stem.find('_');
    const size_t last_sep = stem.rfind('_');
    if (first_sep == std::string::npos || last_sep == first_sep)
    {
      continue;
    }
    int x = 0;
    int cy = 0;
    int z = 0;
    if (!TryParseInt(stem.substr(0, first_sep), x) ||
        !TryParseInt(stem.substr(first_sep + 1, last_sep - first_sep - 1), cy) ||
        !TryParseInt(stem.substr(last_sep + 1), z))
    {
      continue;
    }
    const DiskTerrainColumnKey column{x, z};
    index.slice_cys[column].insert(cy);
    auto [it, inserted] = index.highest_cy.emplace(column, cy);
    if (!inserted)
    {
      it->second = std::max(it->second, cy);
    }
  }
  index.initialized = true;
  KnownAirEvidenceRevision.fetch_add(1, std::memory_order_release);
}

int UChunkStorageService::ScanHighestChunkSliceOnDisk(
    const std::string &worldFolder, glm::ivec3 groundCoord,
    std::unordered_set<int> *slices) const
{
  const std::filesystem::path chunks_dir(ChunksDir(worldFolder));
  if (!std::filesystem::exists(chunks_dir) ||
      !std::filesystem::is_directory(chunks_dir))
  {
    return kNoChunkSliceOnDisk;
  }
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  const std::string prefix = std::to_string(groundCoord.x) + "_";
  const std::string suffix = "_" + std::to_string(groundCoord.z);
  int highest = kNoChunkSliceOnDisk;
  for (const auto &entry : std::filesystem::directory_iterator(chunks_dir))
  {
    const std::string extension = entry.path().extension().string();
    if (extension != JsonSerializer.FileExtension() &&
        extension != BinarySerializer.FileExtension())
    {
      continue;
    }
    const std::string stem = entry.path().stem().string();
    if (stem.size() <= prefix.size() + suffix.size() ||
        stem.compare(0, prefix.size(), prefix) != 0 ||
        stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) != 0)
    {
      continue;
    }
    int cy = 0;
    const std::string cy_text = stem.substr(
        prefix.size(), stem.size() - prefix.size() - suffix.size());
    if (TryParseInt(cy_text, cy))
    {
      if (slices)
        slices->insert(cy);
      highest = highest == kNoChunkSliceOnDisk ? cy : std::max(highest, cy);
    }
  }
  return highest;
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
  RecordChunkSliceSaved(worldFolder, chunkCoord);
  return true;
}

void UChunkStorageService::RecordChunkSliceSaved(
    const std::string &worldFolder, glm::ivec3 chunkCoord) const
{
  const glm::ivec3 ground(chunkCoord.x, 0, chunkCoord.z);
  const std::string cache_key = HighestChunkSliceIndexKey(worldFolder);
  {
    std::lock_guard<std::mutex> lock(HighestChunkSliceCacheMutex);
    const auto folder_index = HighestChunkSliceIndexByFolder.find(cache_key);
    if (folder_index != HighestChunkSliceIndexByFolder.end() &&
        folder_index->second.initialized)
    {
      DiskTerrainColumnIndex &index = folder_index->second;
      const DiskTerrainColumnKey column{ground.x, ground.z};
      index.slice_cys[column].insert(chunkCoord.y);
      if (index.dirty_columns.count(column) == 0)
      {
        auto [cached, inserted] = index.highest_cy.emplace(column, chunkCoord.y);
        if (!inserted)
        {
          cached->second = std::max(cached->second, chunkCoord.y);
        }
      }
    }
  }
  KnownAirEvidenceRevision.fetch_add(1, std::memory_order_release);
}

std::optional<LoadedAirSlice> UChunkStorageService::ReadKnownAbsentTerrainSlice(
    const std::string &worldFolder, glm::ivec3 coord) const
{
  const glm::ivec3 column(coord.x, 0, coord.z);
  if (IsColumnSavePending(column))
    return std::nullopt;
  const int highest = GetHighestChunkSliceOnDisk(worldFolder, column);
  if (highest == kNoChunkSliceOnDisk || coord.y > highest ||
      !IsChunkSliceAbsentOnDisk(worldFolder, coord))
    return std::nullopt;
  for (const auto format : {ChunkDiskFormat::Binary, ChunkDiskFormat::Json})
  {
    std::error_code ec;
    if (std::filesystem::exists(ChunkFilePath(worldFolder, coord, format),
                                ec) ||
        ec)
      return std::nullopt;
  }
  LoadedAirSlice air;
  air.Coord = coord;
  return air;
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
    world.GetChunkManager().MarkTerrainColumnReadUnresolved(chunkCoord);
    return -1;
  }

  const UChunkBuffer buffer =
      DeserializeChunk(bytes, chunkCoord, format, registry);
  const size_t before = world.CountNonAir();
  if (!ApplyTerrainSliceLoad(world, chunkCoord, &buffer, nullptr,
                             {{chunkCoord.x, 0, chunkCoord.z}, 0}, 0, false))
  {
    world.GetChunkManager().MarkTerrainColumnReadUnresolved(chunkCoord);
    return -1;
  }
  const size_t after = world.CountNonAir();
  return static_cast<int>(after > before ? after - before : 0);
}

bool UChunkStorageService::ResolveTerrainColumnRead(
    UBlockWorld &world, const std::string &worldFolder, glm::ivec3 column,
    int maxWorldY, int minWorldY)
{
  column.y = 0;
  const int highest = GetHighestChunkSliceOnDisk(worldFolder, column);
  if (highest == kNoChunkSliceOnDisk || maxWorldY < minWorldY ||
      highest < FloorDiv(minWorldY, CHUNK_SIZE) ||
      highest > FloorDiv(maxWorldY, CHUNK_SIZE))
    return false;
  auto &manager = world.GetChunkManager();
  for (int cy = FloorDiv(minWorldY, CHUNK_SIZE); cy <= highest; ++cy)
  {
    const glm::ivec3 coord(column.x, cy, column.z);
    if (IsChunkSliceAbsentOnDisk(worldFolder, coord))
    {
      if (!manager.HasLoadedAirSlice(coord) &&
          !manager.HasDecodedTerrainSlice(coord))
        return false;
    }
    else if (!manager.HasDecodedTerrainSlice(coord))
      return false;
  }
  manager.ResolveTerrainColumnRead(column);
  return true;
}

void UChunkStorageService::RefreshDiskColumnIndex(
    const std::string &worldFolder, glm::ivec3 groundCoord,
    DiskTerrainColumnIndex &index) const
{
  if (!index.initialized)
  {
    BuildHighestChunkSliceIndex(worldFolder, index);
  }
  const DiskTerrainColumnKey column{groundCoord.x, groundCoord.z};
  if (index.dirty_columns.erase(column) > 0)
  {
    KnownAirEvidenceRevision.fetch_add(1, std::memory_order_release);
    std::unordered_set<int> slices;
    const int highest =
        ScanHighestChunkSliceOnDisk(worldFolder, groundCoord, &slices);
    if (slices.empty())
      index.slice_cys.erase(column);
    else
      index.slice_cys[column] = std::move(slices);
    if (highest != kNoChunkSliceOnDisk)
    {
      index.highest_cy[column] = highest;
    }
    else
    {
      index.highest_cy.erase(column);
    }
  }
}

int UChunkStorageService::GetHighestChunkSliceOnDisk(
    const std::string &worldFolder, glm::ivec3 groundCoord) const
{
  groundCoord.y = 0;
  const std::string cache_key = HighestChunkSliceIndexKey(worldFolder);
  std::lock_guard<std::mutex> lock(HighestChunkSliceCacheMutex);
  auto &index = HighestChunkSliceIndexByFolder[cache_key];
  RefreshDiskColumnIndex(worldFolder, groundCoord, index);
  const auto cached = index.highest_cy.find({groundCoord.x, groundCoord.z});
  return cached != index.highest_cy.end() ? cached->second
                                          : kNoChunkSliceOnDisk;
}

bool UChunkStorageService::IsChunkSliceAbsentOnDisk(
    const std::string &worldFolder, glm::ivec3 chunkCoord) const
{
  if (worldFolder.empty())
    return false;
  const std::string cache_key = HighestChunkSliceIndexKey(worldFolder);
  std::lock_guard<std::mutex> lock(HighestChunkSliceCacheMutex);
  auto &index = HighestChunkSliceIndexByFolder[cache_key];
  RefreshDiskColumnIndex(worldFolder, {chunkCoord.x, 0, chunkCoord.z}, index);
  const auto found = index.slice_cys.find({chunkCoord.x, chunkCoord.z});
  return found == index.slice_cys.end() || !found->second.count(chunkCoord.y);
}

void UChunkStorageService::PrepareHighestChunkSliceIndex(
    const std::string &worldFolder) const
{
  const std::string cache_key = HighestChunkSliceIndexKey(worldFolder);
  std::lock_guard<std::mutex> lock(HighestChunkSliceCacheMutex);
  DiskTerrainColumnIndex &index = HighestChunkSliceIndexByFolder[cache_key];
  if (!index.initialized)
  {
    BuildHighestChunkSliceIndex(worldFolder, index);
  }
}

bool UChunkStorageService::RemoveChunkSliceFromDisk(
    const std::string &worldFolder, glm::ivec3 chunkCoord,
    std::string *outError) const
{
  const glm::ivec3 ground(chunkCoord.x, 0, chunkCoord.z);
  const std::string cache_key = HighestChunkSliceIndexKey(worldFolder);
  const DiskTerrainColumnKey column{ground.x, ground.z};
  const auto invalidate_cached_highest = [&]()
  {
    std::lock_guard<std::mutex> lock(HighestChunkSliceCacheMutex);
    const auto folder_index = HighestChunkSliceIndexByFolder.find(cache_key);
    if (folder_index != HighestChunkSliceIndexByFolder.end() &&
        folder_index->second.initialized)
    {
      folder_index->second.dirty_columns.insert(column);
      KnownAirEvidenceRevision.fetch_add(1, std::memory_order_release);
    }
  };

  // Invalidate both before and after filesystem work. A concurrent lookup may
  // consume the first dirty mark and scan while removals are in progress; the
  // second mark forces the next lookup to see the completed disk state. Never
  // hold the global index mutex while the filesystem can block.
  invalidate_cached_highest();
  bool success = true;
  std::string first_error;
  for (const ChunkDiskFormat format :
       {ChunkDiskFormat::Binary, ChunkDiskFormat::Json})
  {
    const std::string filePath = ChunkFilePath(worldFolder, chunkCoord, format);
    std::error_code ec;
    std::filesystem::remove(filePath, ec);
    if (ec)
    {
      success = false;
      if (first_error.empty())
      {
        first_error = filePath + ": " + ec.message();
      }
    }
  }
  invalidate_cached_highest();
  if (outError && !success)
  {
    *outError = std::move(first_error);
  }
  return success;
}

bool UChunkStorageService::RemoveTerrainColumnFromDisk(
    const std::string &worldFolder, glm::ivec3 groundCoord,
    int maxWorldY, std::string *outError, int minWorldY) const
{
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  const int minCy = FloorDiv(minWorldY, CHUNK_SIZE);
  const int maxCy = FloorDiv(maxWorldY, CHUNK_SIZE);
  const int highestOnDisk = GetHighestChunkSliceOnDisk(worldFolder, groundCoord);
  const int toCy = highestOnDisk == kNoChunkSliceOnDisk
                       ? maxCy
                       : std::max(maxCy, highestOnDisk);
  bool success = true;
  std::string first_error;
  for (int cy = minCy; cy <= toCy; ++cy)
  {
    std::string remove_error;
    if (!RemoveChunkSliceFromDisk(
            worldFolder, glm::ivec3(groundCoord.x, cy, groundCoord.z),
            &remove_error))
    {
      success = false;
      if (first_error.empty())
      {
        first_error = std::move(remove_error);
      }
    }
  }
  std::error_code light_error;
  std::filesystem::remove(ColumnLightFilePath(worldFolder, groundCoord),
                          light_error);
  if (light_error)
  {
    success = false;
    if (first_error.empty())
      first_error = light_error.message();
  }
  if (outError && !success)
  {
    *outError = std::move(first_error);
  }
  return success;
}

std::string
UChunkStorageService::ColumnLightFilePath(const std::string &worldFolder,
                                          glm::ivec3 column)
{
  return worldFolder + "/light/" + std::to_string(column.x) + "_" +
         std::to_string(column.z) + ".json";
}

std::shared_ptr<const PersistedColumnLight>
UChunkStorageService::CaptureColumnLight(glm::ivec3 column,
                                         const UBlockWorld &world,
                                         UBlockRegistry &registry) const
{
  auto receipts = std::make_shared<PersistedColumnLight>();
  ColumnLightCaptureStats stats;
  const auto finish = [&]() -> std::shared_ptr<const PersistedColumnLight>
  {
    if (ObserveLightCapture)
      ObserveLightCapture(column, stats);
    return receipts;
  };
  const auto &profile = world.GetVerticalProfile();
  if (!profile.IsValid())
    return finish();
  const int64_t low = profile.MinChunkY(CHUNK_SIZE);
  const int64_t high = profile.MaxChunkY(CHUNK_SIZE);
  if (high - low + 1 > PersistedSectionLight::MaxDependencies)
    return finish();
  const auto catalog = registry.GetDefinitionsCatalogSnapshot();
  size_t bytes = 0;
  for (int64_t cy = low; cy <= high; ++cy)
  {
    const glm::ivec3 coord(column.x, int(cy), column.z);
    if (ObserveLightCapture)
      if (const auto field =
              world.GetChunkManager().GetSectionLightField(coord))
      {
        ++stats.fields;
        stats.recorded_output += world.HasRecordedLightSamplingOutput(coord);
        stats.padded +=
            field->GetPaddedData() && field->GetPaddedDomains() == 3;
        stats.current +=
            world.HasCompleteLightSamplingCalculation(coord, catalog);
        const auto context =
            world.GetRecordedLightInputContext(coord, LightDomain::Sky);
        if (context && context->input_catalog)
        {
          stats.frozen_context += bool(context->content_certificate);
          stats.complete_context += context->inputs_complete;
          stats.retained_current += world.HasCompleteLightSamplingCalculation(
              coord, context->input_catalog);
          stats.equivalent_catalog +=
              context->input_catalog != catalog && catalog &&
              LightCacheCatalogsEquivalent(*context->input_catalog, *catalog);
          const ChunkInputStamp *stale = nullptr;
          const auto validation = ValidateRelightInputs(
              *context, world, context->input_catalog, true, &stale);
          stats.stale_inputs += validation != RelightInputValidation::Current;
          if (!stats.has_stale_sample &&
              validation != RelightInputValidation::Current)
          {
            stats.stale_reason = static_cast<int>(validation);
            if (stale)
            {
              const auto now =
                  world.CaptureInputStamp(stale->coord, false, true);
              stats.has_stale_sample = true;
              stats.stale_coord = stale->coord;
              stats.absent_before = static_cast<int>(stale->missingEvidence);
              stats.absent_now = static_cast<int>(now.missingEvidence);
              stats.incarnation_before = stale->incarnation;
              stats.incarnation_now = now.incarnation;
              stats.content_before = stale->content;
              stats.content_now = now.content;
            }
          }
        }
        else
          ++stats.no_context;
      }
    auto receipt =
        CaptureSectionLight(world, {column.x, int(cy), column.z}, catalog);
    if (!receipt)
      continue;
    const size_t size = PersistedLightBytes(*receipt);
    if (receipts->size() >= kMaxColumnLightSections ||
        size > kMaxColumnLightBytes - bytes)
      break;
    bytes += size;
    ++stats.exported;
    stats.bytes = bytes;
    receipts->push_back(
        std::make_shared<const PersistedSectionLight>(std::move(*receipt)));
  }
  return finish();
}

bool UChunkStorageService::WriteColumnLight(
    const std::string &worldFolder, glm::ivec3 column,
    const PersistedColumnLight &receipts) const
{
  if (receipts.size() > kMaxColumnLightSections)
    return false;
  json fields = json::array();
  size_t bytes = 0;
  int64_t previous = std::numeric_limits<int>::min() - int64_t{1};
  for (const auto &receipt : receipts)
  {
    if (!receipt || receipt->coord.x != column.x ||
        receipt->coord.z != column.z || receipt->coord.y <= previous ||
        PersistedLightBytes(*receipt) > kMaxColumnLightBytes - bytes)
      return false;
    previous = receipt->coord.y;
    bytes += PersistedLightBytes(*receipt);
    fields.push_back(EncodeSectionLight(*receipt));
  }
  const auto text = json{{"format_version", 1},
                         {"column", {column.x, column.z}},
                         {"fields", std::move(fields)}}
                        .dump();
  if (text.size() > kMaxColumnLightBytes)
    return false;
  try
  {
    return WriteBytesAtomically(ColumnLightFilePath(worldFolder, column),
                                std::vector<uint8_t>(text.begin(), text.end()));
  }
  catch (const std::filesystem::filesystem_error &)
  {
    return false;
  }
}

std::shared_ptr<const PersistedColumnLight>
UChunkStorageService::ReadColumnLight(const std::string &worldFolder,
                                      glm::ivec3 column) const
{
  try
  {
    // Bound the actual read too: a file can grow after a size probe.
    std::ifstream file(ColumnLightFilePath(worldFolder, column),
                       std::ios::binary);
    if (!file)
      return {};
    std::string text;
    char buffer[4096];
    while (file.read(buffer, sizeof(buffer)) || file.gcount())
    {
      if (size_t(file.gcount()) > kMaxColumnLightBytes - text.size())
        return {};
      text.append(buffer, size_t(file.gcount()));
    }
    if (!file.eof())
      return {};
    const auto data = json::parse(text);
    int version = 0, x = 0, z = 0;
    if (!persisted_light_detail::Integer(data.at("format_version"), version) ||
        version != 1 || !data.at("column").is_array() ||
        data.at("column").size() != 2 ||
        !persisted_light_detail::Integer(data.at("column")[0], x) ||
        !persisted_light_detail::Integer(data.at("column")[1], z) ||
        x != column.x || z != column.z || !data.at("fields").is_array() ||
        data.at("fields").size() > kMaxColumnLightSections)
      return {};
    auto receipts = std::make_shared<PersistedColumnLight>();
    size_t bytes = 0;
    int64_t previous = std::numeric_limits<int>::min() - int64_t{1};
    for (const auto &row : data.at("fields"))
    {
      auto receipt = DecodeSectionLight(row);
      if (!receipt || receipt->coord.x != x || receipt->coord.z != z ||
          receipt->coord.y <= previous ||
          PersistedLightBytes(*receipt) > kMaxColumnLightBytes - bytes)
        return {};
      previous = receipt->coord.y;
      bytes += PersistedLightBytes(*receipt);
      receipts->push_back(
          std::make_shared<const PersistedSectionLight>(std::move(*receipt)));
    }
    return receipts;
  }
  catch (const json::exception &)
  {
    return {};
  }
}

void UChunkStorageService::SaveTerrainColumn(glm::ivec3 groundCoord,
                                             const UBlockWorld &world,
                                             const std::string &worldFolder,
                                             UBlockRegistry &registry,
                                             int maxWorldY, int minWorldY)
{
  if (world.GetChunkManager().HasUnresolvedTerrainColumnRead(groundCoord))
    return; // Unknown disk data is not permission to overwrite or truncate it.
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  const auto light = CaptureColumnLight(groundCoord, world, registry);
  WriteColumnLight(worldFolder, groundCoord, *light);
  const int minCy = FloorDiv(minWorldY, CHUNK_SIZE);
  const int maxCy = FloorDiv(maxWorldY, CHUNK_SIZE);
  const int highestOnDisk = GetHighestChunkSliceOnDisk(worldFolder, groundCoord);
  const int highestData =
      GetHighestTerrainSliceForSave(world, groundCoord, maxWorldY, minWorldY);
  int highestToSave = highestData;
  if (highestOnDisk != kNoChunkSliceOnDisk)
  {
    highestToSave = highestToSave == kNoTerrainChunkSlice
                        ? highestOnDisk
                        : std::max(highestOnDisk, highestToSave);
  }
  if (highestToSave == kNoTerrainChunkSlice)
  {
    return;
  }
  highestToSave = std::clamp(highestToSave, minCy, maxCy);

  for (int cy = minCy; cy <= highestToSave; ++cy)
  {
    const glm::ivec3 slice(groundCoord.x, cy, groundCoord.z);
    const auto snapshot =
        world.GetChunkManager().MakeTerrainSaveSnapshot(slice);
    if (snapshot)
    {
      SaveChunk(slice, *snapshot, worldFolder, registry);
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
                                            int maxWorldY, int minWorldY)
{
  if (groundCoord.y != 0)
  {
    return -1;
  }
  if (IsColumnSavePending(groundCoord))
  {
    return -1;
  }
  const bool was_blocked =
      world.GetChunkManager().HasUnresolvedTerrainColumnRead(groundCoord);

  int total = 0;
  const int minCy = FloorDiv(minWorldY, CHUNK_SIZE);
  const int highestOnDisk = GetHighestChunkSliceOnDisk(worldFolder, groundCoord);
  if (highestOnDisk == kNoChunkSliceOnDisk || highestOnDisk < minCy)
  {
    return 0;
  }
  const int highestToLoad = std::min(highestOnDisk, FloorDiv(maxWorldY, CHUNK_SIZE));
  for (int cy = minCy; cy <= highestToLoad; ++cy)
  {
    const glm::ivec3 slice(groundCoord.x, cy, groundCoord.z);
    int placed = 0;
    if (DetectFormatOnDisk(worldFolder, slice) == ChunkDiskFormat::Absent)
    {
      const auto air = ReadKnownAbsentTerrainSlice(worldFolder, slice);
      if (!air || !ApplyTerrainSliceLoad(world, slice, nullptr, &*air,
                                         {groundCoord, 0}, 0, false))
      {
        world.GetChunkManager().MarkTerrainColumnReadUnresolved(groundCoord);
        return -1;
      }
    }
    else
    {
      placed = LoadChunk(slice, world, worldFolder, registry);
      if (placed < 0)
        return -1;
    }
    if (placed > 0)
    {
      total += placed;
    }
  }
  if (!ResolveTerrainColumnRead(world, worldFolder, groundCoord, maxWorldY,
                                minWorldY))
  {
    world.GetChunkManager().MarkTerrainColumnReadUnresolved(groundCoord);
    return -1;
  }
  if (was_blocked && !IsTerrainChunkComplete(world, groundCoord, maxWorldY,
                                             kNoTerrainChunkSlice, minWorldY))
  {
    world.GetChunkManager().MarkTerrainColumnReadUnresolved(groundCoord);
    return -1;
  }
  if (const auto receipts = ReadColumnLight(worldFolder, groundCoord))
    for (const auto &receipt : *receipts)
      if (!world.HasCompleteLightSamplingCalculation(
              receipt->coord, registry.GetDefinitionsCatalogSnapshot()))
        RestoreSectionLight(world, *receipt,
                            registry.GetDefinitionsCatalogSnapshot());
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
  if (PendingSaveColumns.insert(groundCoord).second)
    KnownAirEvidenceRevision.fetch_add(1, std::memory_order_release);
}

void UChunkStorageService::ClearColumnSavePending(glm::ivec3 groundCoord)
{
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  if (PendingSaveColumns.erase(groundCoord))
    KnownAirEvidenceRevision.fetch_add(1, std::memory_order_release);
}

} // namespace cutum

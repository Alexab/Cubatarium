#include "World/IO/JsonChunkSerializer.h"
#include "Core/Memory/StlSourceStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Math/FluidCellState.h"
#include <algorithm>
#include <array>
#include <iterator>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace cutum
{

using json = nlohmann::json;

namespace
{

// The JSON lexer keeps the raw text surrounding the current token for errors.
// Collapse each run of inter-token whitespace to one separator. Whitespace
// inside strings and separation within invalid literals remain significant.
template<class Iterator>
class JsonWhitespaceIterator
{
public:
  using iterator_category = std::input_iterator_tag;
  using value_type = char;
  using difference_type = std::ptrdiff_t;
  using pointer = const char *;
  using reference = char;

  JsonWhitespaceIterator(Iterator first, Iterator last) : Next(first), End(last) { ++*this; }
  char operator*() const { return Current; }
  JsonWhitespaceIterator operator++(int)
  {
    const auto previous = *this;
    ++*this;
    return previous;
  }
  bool operator==(const JsonWhitespaceIterator &other) const
  {
    return HasCurrent == other.HasCurrent && (!HasCurrent || Next == other.Next);
  }
  bool operator!=(const JsonWhitespaceIterator &other) const { return !(*this == other); }

  JsonWhitespaceIterator &operator++()
  {
    HasCurrent = Next != End;
    if (!HasCurrent)
      return *this;
    Current = static_cast<char>(*Next);
    ++Next;
    if (!InString && IsWhitespace(Current))
    {
      while (Next != End && IsWhitespace(static_cast<char>(*Next)))
        ++Next;
      Current = ' ';
    }
    else if (Escaped)
      Escaped = false;
    else if (InString && Current == '\\')
      Escaped = true;
    else if (Current == '"')
      InString = !InString;
    return *this;
  }

private:
  static bool IsWhitespace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
  Iterator Next, End;
  char Current{};
  bool HasCurrent{false};
  bool InString{false};
  bool Escaped{false};
};

// Keep only the fields the terrain schema consumes. Unknown subtrees remain
// subject to JSON syntax validation, without creating a value tree for them.
class ChunkJsonReader : public nlohmann::json_sax<json>
{
public:
  ChunkJsonReader(glm::ivec3 coord, UBlockRegistry &registry)
      : Coord(coord), Registry(registry) {}

  bool null() override { return Value(nullptr); }
  bool boolean(bool) override { return Value(nullptr); }
  bool binary(binary_t &) override { return Value(nullptr); }
  bool number_integer(number_integer_t v) override { return Value(v); }
  bool number_unsigned(number_unsigned_t v) override { return Value(v); }
  bool number_float(number_float_t v, const string_t &) override { return Value(v); }
  bool parse_error(size_t, const std::string &, const json::exception &) override { return false; }

  bool key(string_t &value) override
  {
    if (Ignored)
      return true;
    static constexpr std::array<std::string_view, Count> names{
        "", "format_version", "cx", "cy", "cz", "voxels", "weather_state",
        "lx", "ly", "lz", "id", "fluid", "type", "surface_wetness", "last_update_sec"};
    Member = static_cast<Field>(std::find(names.begin(), names.end(), value) - names.begin());
    return true;
  }

  bool string(string_t &value) override
  {
    if (!Ignored && Place == Location::Voxel && Member == Type)
    {
      HasType = true;
      ExplicitAir = value == "air";
      TypeId = value.empty() ? BLOCK_AIR : Registry.GetIdByTypeName(value);
      return true;
    }
    return Value(nullptr);
  }

  bool start_object(size_t) override
  {
    if (!Ignored && Place == Location::Document)
      Place = Location::Root;
    else if (!Ignored && Place == Location::Voxels)
    {
      for (auto field : {LX, LY, LZ, Id, Fluid})
        Fields[field].reset();
      HasType = false;
      Place = Location::Voxel;
    }
    else if (!Ignored && Place == Location::Root && Member == Weather)
    {
      WeatherPresent = WeatherObject = true;
      Fields[Wetness].reset();
      Fields[LastUpdate].reset();
      Place = Location::Weather;
    }
    else
      SkipValue();
    return true;
  }

  bool end_object() override
  {
    if (Ignored)
      --Ignored;
    else if (Place == Location::Voxel)
    {
      VoxelsValid &= ApplyVoxel();
      Place = Location::Voxels;
    }
    else if (Place == Location::Weather)
      Place = Location::Root;
    else if (Place == Location::Root)
      Place = Location::Done;
    return true;
  }

  bool start_array(size_t) override
  {
    if (!Ignored && Place == Location::Root && Member == Voxels)
    {
      Buffer.Clear();
      VoxelsValid = true;
      Place = Location::Voxels;
    }
    else
      SkipValue();
    return true;
  }

  bool end_array() override
  {
    if (Ignored)
      --Ignored;
    else if (Place == Location::Voxels)
      Place = Location::Root;
    return true;
  }

  bool ReadHeader()
  {
    bool valid = Place == Location::Done && VoxelsValid;
    if (Fields[Version])
      valid &= IntegerIn(Fields[Version], 1, 4);
    if (Fields[X] || Fields[Y] || Fields[Z])
      for (int i = 0; i < 3; ++i)
        valid &= IntegerIn(Fields[X + i], Coord[i], Coord[i]);

    if (WeatherPresent)
    {
      const auto &wetness = Fields[Wetness];
      const auto &updated = Fields[LastUpdate];
      const bool weather_valid = WeatherObject && (!wetness || wetness->is_number()) &&
                                 (!updated || updated->is_number());
      valid &= weather_valid;
      if (weather_valid)
        Buffer.SetChunkWeatherState(Coord, wetness ? wetness->get<float>() : 0.0f,
                                    updated ? updated->get<double>() : 0.0);
    }
    return valid;
  }

  UChunkBuffer Buffer;

private:
  enum Field { None, Version, X, Y, Z, Voxels, Weather, LX, LY, LZ, Id, Fluid, Type, Wetness, LastUpdate, Count };
  enum class Location { Document, Root, Voxels, Voxel, Weather, Done };

  static bool IntegerIn(const std::optional<json> &value, int lo, int hi)
  {
    if (!value || !value->is_number_integer())
      return false;
    if (value->is_number_unsigned())
    {
      const auto n = value->get<uint64_t>();
      return hi >= 0 && n <= static_cast<uint64_t>(hi) &&
             (lo <= 0 || n >= static_cast<uint64_t>(lo));
    }
    const auto n = value->get<int64_t>();
    return lo <= n && n <= hi;
  }

  bool ApplyVoxel()
  {
    if (!IntegerIn(Fields[LX], 0, CHUNK_SIZE - 1) ||
        !IntegerIn(Fields[LY], 0, CHUNK_SIZE - 1) ||
        !IntegerIn(Fields[LZ], 0, CHUNK_SIZE - 1))
      return false;
    if (Fields[Id] ? !IntegerIn(Fields[Id], 0, UINT16_MAX) :
        (!HasType || (TypeId == BLOCK_AIR && !ExplicitAir)))
      return false;
    const auto id = Fields[Id] ? Fields[Id]->get<BlockId>() : TypeId;
    const bool fluid_valid = !Fields[Fluid] || IntegerIn(Fields[Fluid], 0, 0);
    if (id != BLOCK_AIR)
    {
      const glm::ivec3 local(Fields[LX]->get<int>(), Fields[LY]->get<int>(), Fields[LZ]->get<int>());
      const auto pos = Coord * CHUNK_SIZE + local;
      Buffer.SetBlock(pos, id);
      if (Fields[Fluid])
      {
        if (IntegerIn(Fields[Fluid], 0, UINT8_MAX))
          Buffer.SetFluidPacked(pos, Fields[Fluid]->get<uint8_t>());
      }
      else if (Registry.IsLiquid(id))
        Buffer.SetFluidPacked(pos, PackFluidCellState(FluidCellState::Source()));
    }
    return fluid_valid;
  }

  bool Value(json value)
  {
    if (Ignored)
      return true;
    switch (Place)
    {
    case Location::Root:
      if (Member >= Version && Member <= Z)
        Fields[Member] = std::move(value);
      else if (Member == Weather)
      {
        WeatherPresent = true;
        WeatherObject = false;
      }
      else if (Member == Voxels)
      {
        Buffer.Clear();
        VoxelsValid = false;
      }
      break;
    case Location::Voxels:
      VoxelsValid = false;
      break;
    case Location::Voxel:
      if (Member >= LX && Member <= Fluid)
        Fields[Member] = std::move(value);
      else if (Member == Type)
        HasType = false;
      break;
    case Location::Weather:
      if (Member == Wetness || Member == LastUpdate)
        Fields[Member] = std::move(value);
      break;
    default:
      break;
    }
    return true;
  }

  void SkipValue()
  {
    Value(nullptr);
    ++Ignored;
  }

  glm::ivec3 Coord;
  UBlockRegistry &Registry;
  std::array<std::optional<json>, Count> Fields;
  Location Place{Location::Document};
  Field Member{None};
  size_t Ignored{0};
  BlockId TypeId{BLOCK_AIR};
  bool HasType{false};
  bool ExplicitAir{false};
  bool VoxelsValid{false};
  bool WeatherPresent{false};
  bool WeatherObject{false};
};

template<class Parse, class Seal>
UChunkBuffer DecodeJson(glm::ivec3 coord, UBlockRegistry &registry, size_t input_bytes,
                        const IoCodecProbe *probe, Parse parse, Seal seal)
{
  ChunkJsonReader reader(coord, registry);
  if (!RunIoCodecWindow(probe, IoCodecWindow::JsonParse, [&] { return parse(reader); }))
    return {};
  if (reader.ReadHeader())
    seal(reader.Buffer);
  if (probe)
    probe->sample(probe->context, {IoCodecKind::JsonRead, 0, input_bytes,
                                  reader.Buffer.GetOwnedAllocationBytes(), sizeof(reader),
                                  StlSourceStorage::SupportedProfile()});
  return std::move(reader.Buffer);
}

template<class Iterator>
bool ParseJson(Iterator first, Iterator last, ChunkJsonReader &reader)
{
  return json::sax_parse(JsonWhitespaceIterator(first, last), JsonWhitespaceIterator(last, last), &reader);
}

} // namespace

SerializedChunk UJsonChunkSerializer::Serialize(glm::ivec3 chunkCoord,
                                                const UChunk &chunk,
                                                UBlockRegistry &registry,
                                                const IoCodecProbe *probe) const
{
  SerializedChunk out;
  out.format = ChunkDiskFormat::Json;

  json data;
  data["format_version"] = 4;
  data["cx"] = chunkCoord.x;
  data["cy"] = chunkCoord.y;
  data["cz"] = chunkCoord.z;
  json voxels = json::array();
  for (int z = 0; z < CHUNK_SIZE; ++z)
  {
    for (int y = 0; y < CHUNK_SIZE; ++y)
    {
      for (int x = 0; x < CHUNK_SIZE; ++x)
      {
        const glm::ivec3 local(x, y, z);
        const BlockId id = chunk.GetBlockLocal(local);
        if (id == BLOCK_AIR)
        {
          continue;
        }
        json voxel = {{"lx", x}, {"ly", y}, {"lz", z}, {"id", id}};
        const uint8_t fluid_packed =
            PackFluidCellState(chunk.GetFluidLocal(local));
        if (fluid_packed != 0)
        {
          voxel["fluid"] = fluid_packed;
        }
        voxels.push_back(std::move(voxel));
      }
    }
  }
  data["voxels"] = voxels;
  data["weather_state"] = {
      {"surface_wetness", std::clamp(chunk.GetSurfaceWetness(), 0.0f, 1.0f)},
      {"last_update_sec", std::max(0.0, chunk.GetWeatherLastUpdateSec())}};
  const auto text = RunIoCodecWindow(probe, IoCodecWindow::JsonDump, [&] { return data.dump(); });
  out.bytes.assign(text.begin(), text.end());
  if (probe)
  {
    StlSourceStorage scratch, output;
    scratch.JsonTree(voxels);
    scratch.JsonTree(data);
    scratch.Text(text);
    output.Buffer(out.bytes.capacity(), sizeof(uint8_t));
    probe->sample(probe->context, {IoCodecKind::JsonWrite, scratch.bytes.requested,
                                  0, output.bytes.requested, 0,
                                  scratch.bytes.supported && output.bytes.supported});
  }
  return out;
}

UChunkBuffer
UJsonChunkSerializer::Deserialize(const std::vector<uint8_t> &bytes,
                                  glm::ivec3 chunkCoord,
                                  UBlockRegistry &registry,
                                  const IoCodecProbe *probe) const
{
  if (bytes.empty())
    return {};
  StlSourceStorage input;
  input.Buffer(bytes.capacity(), sizeof(uint8_t));
  return DecodeJson(chunkCoord, registry, input.bytes.requested, probe,
      [&](ChunkJsonReader &reader) { return ParseJson(bytes.begin(), bytes.end(), reader); },
      [chunkCoord](UChunkBuffer &buffer) { buffer.MarkDecodedFrame(chunkCoord); });
}

UChunkBuffer UJsonChunkSerializer::DeserializeStream(std::istream &file, glm::ivec3 chunkCoord,
                                                      UBlockRegistry &registry, const IoCodecProbe *probe,
                                                      bool withholdFrame) const
{
  if (!file)
    return {};
  return DecodeJson(chunkCoord, registry, 0, probe,
      [&](ChunkJsonReader &reader) {
        return ParseJson(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>(), reader) &&
               !file.bad();
      },
      [chunkCoord, withholdFrame](UChunkBuffer &buffer) {
        if (!withholdFrame)
          buffer.MarkDecodedFrame(chunkCoord);
      });
}

} // namespace cutum

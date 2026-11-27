#pragma once

#include "World/IO/BufferedJsonStream.h"
#include "World/Lighting/PersistedSectionLight.h"
#include <type_traits>

namespace cutum::column_light_json
{
class Reader : public nlohmann::json_sax<nlohmann::json>
{
public:
  Reader(glm::ivec3 column, PersistedColumnLight &output, const IoCodecProbe *probe)
      : Column(column), Output(output), Probe(probe)
  {
  }

  bool null() override { return InvalidValue(); }
  bool boolean(bool) override { return InvalidValue(); }
  bool binary(binary_t &) override { return InvalidValue(); }
  bool number_integer(number_integer_t value) override { return Integer(value); }
  bool number_unsigned(number_unsigned_t value) override { return Integer(value); }
  bool number_float(number_float_t, const string_t &) override { return InvalidValue(); }
  bool parse_error(size_t, const std::string &, const nlohmann::json::exception &) override { return false; }

  bool key(string_t &value) override
  {
    if (Ignored)
      return true;
    static constexpr std::array<std::string_view, FieldCount> names{
        "", "format_version", "column", "fields", "coord", "profile",
        "configuration", "light_signature", "inputs", "padded_light", "checksum"};
    static_assert([] {
      for (const auto name : names)
        if (name.size() >= kUnknownPrefix)
          return false;
      return true;
    }());
    const auto found = std::find(names.begin(), names.end(), value);
    Member = found == names.end() ? None : static_cast<Field>(found - names.begin());
    if (Position == Place::Root)
    {
      RootBits &= ~Bit(Member);
      if (Member == Fields)
      {
        PersistedColumnLight{}.swap(Output);
        OutputBytes = 0;
      }
    }
    else if (Position == Place::Row)
    {
      RowBits &= ~Bit(Member);
      if (Member == Signature)
        std::string{}.swap(Row.signature);
      if (Member == Inputs)
        std::vector<PersistedSectionLight::Input>{}.swap(Row.inputs);
    }
    return true;
  }

  bool string(string_t &value) override
  {
    if (!Ignored && Position == Place::Row)
    {
      if (Member == Signature && !value.empty() && value.size() <= persisted_light_detail::kMaxSignatureBytes)
      {
        Row.signature = value;
        RowBits |= Bit(Signature);
      }
      if (Member == Light && persisted_light_detail::DecodePaddedLight(value, Row.volume))
        RowBits |= Bit(Light);
    }
    return InvalidValue();
  }

  bool start_object(size_t) override { return BeginContainer(true); }
  bool start_array(size_t) override { return BeginContainer(false); }
  bool end_object() override { return EndContainer(); }
  bool end_array() override { return EndContainer(); }

  bool Complete() const noexcept
  {
    return Position == Place::Done && RootBits == (Bit(Version) | Bit(ColumnKey) | Bit(Fields));
  }

  size_t StringLimit(bool is_object_value) const noexcept
  {
    // An extra decoded byte signals overflow without confusing a valid prefix
    // with the complete key, signature or light string.
    if (is_object_value && !Ignored && Position == Place::Row)
    {
      if (Member == Signature)
        return persisted_light_detail::kMaxSignatureBytes + 1;
      if (Member == Light)
        return 2 * SectionLightVolume::CellCount + 1;
    }
    return kUnknownPrefix;
  }

private:
  enum class Place { Document, Root, Column, Fields, Row, Coord, Profile, Inputs, Input, Done };
  enum Field { None, Version, ColumnKey, Fields, Coord, Profile, Configuration, Signature, Inputs, Light, Checksum, FieldCount };
  static constexpr unsigned Bit(Field field) noexcept { return 1u << field; }
  static constexpr size_t kUnknownPrefix = 16;
  static constexpr size_t kMaxDepth = 16385;

  bool BeginContainer(bool object)
  {
    if (Depth == kMaxDepth)
      return false;
    ++Depth;
    if (Ignored)
    {
      ++Ignored;
      return true;
    }
    if (object && Position == Place::Document)
    {
      Position = Place::Root;
      return true;
    }
    if (object && Position == Place::Fields && FieldsValid && Output.size() < kMaxColumnLightSections)
    {
      Position = Place::Row;
      RowBits = 0;
      return true;
    }
    if (!object)
    {
      if (Position == Place::Root && Member == ColumnKey)
        return BeginTuple(Place::Column);
      if (Position == Place::Root && Member == Fields)
      {
        Position = Place::Fields;
        FieldsValid = true;
        return true;
      }
      if (Position == Place::Row && Member == Coord)
        return BeginTuple(Place::Coord);
      if (Position == Place::Row && Member == Profile)
        return BeginTuple(Place::Profile);
      if (Position == Place::Row && Member == Inputs)
      {
        Position = Place::Inputs;
        InputsValid = true;
        return true;
      }
      if (Position == Place::Inputs && InputsValid &&
          Row.inputs.size() < PersistedSectionLight::MaxDependencies)
        return BeginTuple(Place::Input);
    }
    InvalidValue();
    Ignored = 1;
    return true;
  }

  bool BeginTuple(Place place)
  {
    Position = place;
    NumberCount = 0;
    TupleValid = true;
    return true;
  }

  bool InvalidValue()
  {
    if (!Ignored)
      switch (Position)
      {
      case Place::Fields:
        FieldsValid = false;
        Output.clear();
        break;
      case Place::Inputs:
        InputsValid = false;
        break;
      case Place::Column:
      case Place::Coord:
      case Place::Profile:
      case Place::Input:
        TupleValid = false;
        break;
      default:
        break;
      }
    return true;
  }

  template<class Number>
  bool Integer(Number value)
  {
    if (Ignored)
      return true;
    const bool nonnegative = std::is_unsigned_v<Number> || value >= 0;
    switch (Position)
    {
    case Place::Root:
      if (Member == Version && value == 1)
        RootBits |= Bit(Version);
      return true;
    case Place::Row:
      if (Member == Version && value == 1)
        RowBits |= Bit(Version);
      if (Member == Configuration && nonnegative)
      {
        Row.configuration = static_cast<uint64_t>(value);
        RowBits |= Bit(Configuration);
      }
      if (Member == Checksum && nonnegative)
      {
        Row.checksum = static_cast<uint64_t>(value);
        RowBits |= Bit(Checksum);
      }
      return true;
    case Place::Input:
      if (NumberCount == 3)
      {
        ++NumberCount;
        TupleValid &= nonnegative;
        Content = static_cast<uint64_t>(value);
        return true;
      }
      [[fallthrough]];
    case Place::Column:
    case Place::Coord:
    case Place::Profile:
    {
      bool fits = value <= (std::numeric_limits<int>::max)();
      if constexpr (std::is_signed_v<Number>)
        fits &= value >= (std::numeric_limits<int>::min)();
      if (!fits || NumberCount == Numbers.size())
        return InvalidValue();
      Numbers[NumberCount++] = static_cast<int>(value);
      return true;
    }
    default:
      return InvalidValue();
    }
  }

  bool EndContainer()
  {
    --Depth;
    if (Ignored)
    {
      --Ignored;
      return true;
    }
    switch (Position)
    {
    case Place::Root:
      Position = Place::Done;
      break;
    case Place::Column:
      if (TupleValid && NumberCount == 2 && Numbers[0] == Column.x && Numbers[1] == Column.z)
        RootBits |= Bit(ColumnKey);
      Position = Place::Root;
      break;
    case Place::Fields:
      if (FieldsValid)
        RootBits |= Bit(Fields);
      Position = Place::Root;
      break;
    case Place::Row:
      FinishRow();
      Position = Place::Fields;
      break;
    case Place::Coord:
      if (TupleValid && NumberCount == 3)
      {
        Row.coord = {Numbers[0], Numbers[1], Numbers[2]};
        RowBits |= Bit(Coord);
      }
      Position = Place::Row;
      break;
    case Place::Profile:
      if (TupleValid && NumberCount == 5)
      {
        Row.profile = {Numbers[0], Numbers[1], Numbers[2], Numbers[3], Numbers[4]};
        RowBits |= Bit(Profile);
      }
      Position = Place::Row;
      break;
    case Place::Inputs:
      if (InputsValid)
        RowBits |= Bit(Inputs);
      Position = Place::Row;
      break;
    case Place::Input:
    {
      const glm::ivec3 coord{Numbers[0], Numbers[1], Numbers[2]};
      using persisted_light_detail::Key;
      InputsValid = TupleValid && NumberCount == 4 &&
          (Row.inputs.empty() || Key(Row.inputs.back().coord) < Key(coord));
      if (InputsValid)
        Row.inputs.push_back({coord, Content});
      Position = Place::Inputs;
      break;
    }
    default:
      return false;
    }
    return true;
  }

  void FinishRow()
  {
    constexpr unsigned required = Bit(Version) | Bit(Coord) | Bit(Profile) | Bit(Configuration) |
                                   Bit(Signature) | Bit(Inputs) | Bit(Light) | Bit(Checksum);
    const auto bytes = PersistedLightBytes(Row);
    if (RowBits == required && Row.coord.x == Column.x && Row.coord.z == Column.z &&
        (Output.empty() || Output.back()->coord.y < Row.coord.y) &&
        bytes <= kMaxColumnLightBytes - OutputBytes &&
        persisted_light_detail::ValidateDecodedReceipt(Row, Probe))
    {
      Output.push_back(std::make_shared<const PersistedSectionLight>(std::move(Row)));
      OutputBytes += bytes;
    }
    else
    {
      FieldsValid = false;
      Output.clear();
    }
    std::string{}.swap(Row.signature);
    std::vector<PersistedSectionLight::Input>{}.swap(Row.inputs);
  }

  glm::ivec3 Column;
  PersistedColumnLight &Output;
  const IoCodecProbe *Probe;
  PersistedSectionLight Row;
  std::array<int, 5> Numbers{};
  uint64_t Content{0};
  size_t Depth{0}, Ignored{0}, NumberCount{0}, OutputBytes{0};
  Place Position{Place::Document};
  Field Member{None};
  unsigned RootBits{0}, RowBits{0};
  bool TupleValid{false}, FieldsValid{false}, InputsValid{false};
};

} // namespace cutum::column_light_json

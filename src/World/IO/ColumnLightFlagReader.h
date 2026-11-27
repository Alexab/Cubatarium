#pragma once

#include "World/IO/BufferedJsonStream.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>
#include <type_traits>

namespace cutum
{
enum class ColumnLightFlagRead { Loaded, InvalidDocument, Incompatible };

namespace column_light_flag_detail
{
template<class Columns>
class Reader : public nlohmann::json_sax<nlohmann::json>
{
public:
  explicit Reader(std::string_view signature) : Signature(signature) {}

  bool null() override { return InvalidValue(); }
  bool boolean(bool) override { return InvalidValue(); }
  bool binary(binary_t &) override { return InvalidValue(); }
  bool number_float(number_float_t, const string_t &) override { return InvalidValue(); }
  bool number_integer(number_integer_t value) override { return Integer(value); }
  bool number_unsigned(number_unsigned_t value) override { return Integer(value); }
  bool parse_error(size_t, const std::string &, const nlohmann::json::exception &) override { return false; }

  bool key(string_t &value) override
  {
    if (Skipped)
      return true;
    Member = Field::Unknown;
    if (value == "format_version")
    {
      Member = Field::Version;
      VersionMatches = false;
    }
    else if (value == "light_signature")
    {
      Member = Field::Signature;
      SignatureMatches = false;
    }
    else if (value == "complete")
    {
      Member = Field::Complete;
      Candidate.reset();
    }
    return true;
  }

  bool string(string_t &value) override
  {
    if (!Skipped && Position == Place::Root && Member == Field::Signature)
      SignatureMatches = !Signature.empty() && value == Signature;
    return InvalidValue();
  }

  bool start_object(size_t) override { return BeginContainer(true); }
  bool start_array(size_t) override { return BeginContainer(false); }
  bool end_object() override { return EndContainer(); }
  bool end_array() override { return EndContainer(); }

  size_t StringLimit(bool is_object_value) const noexcept
  {
    if (is_object_value && !Skipped && Position == Place::Root && Member == Field::Signature)
      return Signature.size() + 1;
    // A truncated key must never match a recognized name.
    return 16;
  }

  bool Publish(Columns &output)
  {
    if (Position != Place::Done || !VersionMatches || !SignatureMatches || !Candidate)
      return false;
    Candidate->swap(output);
    return true;
  }

private:
  enum class Place { Document, Root, Rows, Coordinate, Done };
  enum class Field { Unknown, Version, Signature, Complete };

  bool BeginContainer(bool object)
  {
    constexpr size_t max_depth = 16385;
    if (++Depth > max_depth)
      return false;
    if (Skipped)
    {
      ++Skipped;
      return true;
    }
    if (object && Position == Place::Document)
      Position = Place::Root;
    else if (!object && Position == Place::Root && Member == Field::Complete)
    {
      Candidate.emplace();
      Position = Place::Rows;
    }
    else if (!object && Position == Place::Rows && Candidate)
    {
      Position = Place::Coordinate;
      Axis = 0;
      PairValid = true;
    }
    else
    {
      InvalidValue();
      Skipped = 1;
    }
    return true;
  }

  bool EndContainer()
  {
    --Depth;
    if (Skipped)
    {
      --Skipped;
      return true;
    }
    switch (Position)
    {
    case Place::Coordinate:
      if (PairValid && Axis == 2)
        Candidate->insert(typename Columns::value_type{Pair[0], Pair[1]});
      else
        Candidate.reset();
      Position = Place::Rows;
      break;
    case Place::Rows:
      Position = Place::Root;
      break;
    case Place::Root:
      Position = Place::Done;
      break;
    default:
      return false;
    }
    return true;
  }

  bool InvalidValue()
  {
    if (!Skipped && Position == Place::Rows)
      Candidate.reset();
    else if (!Skipped && Position == Place::Coordinate)
      PairValid = false;
    return true;
  }

  template<class Number> bool Integer(Number value)
  {
    if (Skipped)
      return true;
    if (Position == Place::Root && Member == Field::Version)
      VersionMatches = value == 2;
    if (Position != Place::Coordinate)
      return InvalidValue();
    bool fits = value <= (std::numeric_limits<int>::max)();
    if constexpr (std::is_signed_v<Number>)
      fits &= value >= (std::numeric_limits<int>::min)();
    if (!fits || Axis == 2)
      return InvalidValue();
    Pair[Axis++] = static_cast<int>(value);
    return true;
  }

  std::string_view Signature;
  std::optional<Columns> Candidate;
  std::array<int, 2> Pair{};
  size_t Depth{0}, Skipped{0}, Axis{0};
  Place Position{Place::Document};
  Field Member{Field::Unknown};
  bool VersionMatches{false}, SignatureMatches{false}, PairValid{false};
};
} // namespace column_light_flag_detail

// Leave any already published output intact on malformed, incompatible or
// incomplete input, or if an ordinary allocation fails during preparation.
template<class Columns>
ColumnLightFlagRead ReadColumnLightFlags(std::istream &file, std::string_view signature,
                                         Columns &output)
{
  BufferedJsonStream source(file);
  column_light_flag_detail::Reader<Columns> reader(signature);
  using Input = BufferedJsonInput<decltype(reader)>;
  nlohmann::detail::parser<nlohmann::json, Input> parser{Input{source, reader}};
  if (!parser.sax_parse(&reader, true) || !source.Complete())
    return ColumnLightFlagRead::InvalidDocument;
  return reader.Publish(output) ? ColumnLightFlagRead::Loaded : ColumnLightFlagRead::Incompatible;
}
} // namespace cutum

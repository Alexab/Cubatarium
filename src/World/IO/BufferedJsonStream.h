#pragma once

#include <array>
#include <istream>
#include <limits>

namespace cutum
{
class BufferedJsonStream
{
public:
  explicit BufferedJsonStream(std::istream &file,
      size_t limit = (std::numeric_limits<size_t>::max)()) : File(file), Remaining(limit) {}

  std::char_traits<char>::int_type Read()
  {
    using Traits = std::char_traits<char>;
    if (Next == End)
    {
      if (File.eof() || Failed)
        return Traits::eof();
      // With a finite byte limit, read one extra byte to distinguish EOF
      // from a file that exceeded the limit after it was opened.
      const auto size = Remaining < Buffer.size() ? Remaining + 1 : Buffer.size();
      File.read(Buffer.data(), static_cast<std::streamsize>(size));
      const auto count = static_cast<size_t>(File.gcount());
      if (count > Remaining || File.bad() || (!File && !File.eof()))
      {
        Failed = true;
        return Traits::eof();
      }
      Next = 0;
      End = count;
      Remaining -= count;
      if (!count)
        return Traits::eof();
    }
    const auto value = Buffer[Next++];
    // A raw NUL in the binary stream is invalid JSON.
    return Traits::to_int_type(value == '\0' ? '\x01' : value);
  }

  bool Complete() const noexcept { return !Failed && Next == End && File.eof(); }

private:
  std::istream &File;
  std::array<char, 4096> Buffer;
  size_t Next{0}, End{0}, Remaining;
  bool Failed{false};
};

template<class Reader> struct BufferedJsonInput
{
  using char_type = char;
  static constexpr bool cutum_collect_raw_lexer_diagnostics = false;
  static constexpr size_t cutum_numeric_token_limit = 4096;

  size_t cutum_string_token_limit(bool is_object_value) const noexcept
  { return Sax.StringLimit(is_object_value); }
  std::char_traits<char>::int_type get_character() { return Stream.Read(); }

  BufferedJsonStream &Stream;
  const Reader &Sax;
};
} // namespace cutum

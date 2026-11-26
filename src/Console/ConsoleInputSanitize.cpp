#include "Console/ConsoleInputSanitize.h"

#include <algorithm>

namespace cutum
{

void AppendSanitizedConsoleByte(std::string &line, unsigned char byte)
{
  if (line.size() >= kConsoleMaxLineLength)
    return;
  if (byte >= 32 && byte < 127)
    line.push_back(static_cast<char>(byte));
  else if (byte == '\t' && !line.empty() && line.back() != ' ')
    line.push_back(' ');
}

std::string SanitizeConsoleLine(std::string_view input)
{
  std::string out;
  out.reserve(std::min(input.size(), kConsoleMaxLineLength));
  for (unsigned char byte : input)
  {
    AppendSanitizedConsoleByte(out, byte);
    if (out.size() == kConsoleMaxLineLength)
      break;
  }
  return out;
}

std::string SanitizeConsolePaste(const std::string &raw, size_t currentLen,
                                 size_t maxTotalLen)
{
  std::string cleaned = SanitizeConsoleLine(raw);
  if (currentLen >= maxTotalLen)
  {
    return {};
  }
  const size_t room = maxTotalLen - currentLen;
  if (cleaned.size() > room)
  {
    cleaned.resize(room);
  }
  return cleaned;
}

} // namespace cutum

#include "ConsoleInputSanitize.h"

#include <algorithm>

namespace cutum
{

std::string SanitizeConsoleLine(std::string s)
{
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s)
  {
    if (c == '\r' || c == '\n' || c == '\t')
    {
      if (c == '\t' && !out.empty() && out.back() != ' ')
      {
        out.push_back(' ');
      }
      continue;
    }
    if (c >= 32 && c < 127)
    {
      out.push_back(static_cast<char>(c));
    }
  }
  if (out.size() > kConsoleMaxLineLength)
  {
    out.resize(kConsoleMaxLineLength);
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

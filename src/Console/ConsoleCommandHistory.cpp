#include "Console/ConsoleCommandHistory.h"
#include "Console/ConsoleInputSanitize.h"

#include <algorithm>
#include <array>
#include <fstream>

namespace cutum
{
namespace
{
void AppendHistoryEntry(std::vector<std::string> &entries, std::string line)
{
  if (entries.size() == entries.capacity())
    entries.reserve(std::min(UConsoleCommandHistory::kMaxEntries,
                             std::max(size_t{1}, entries.size() * 2)));
  entries.push_back(std::move(line));
}
} // namespace

void UConsoleCommandHistory::SetFilePath(std::filesystem::path path)
{
  FilePath = std::move(path);
}

bool UConsoleCommandHistory::Load()
{
  if (FilePath.empty())
  {
    History.clear();
    return false;
  }
  std::ifstream file(FilePath);
  if (!file.is_open())
  {
    History.clear();
    return false;
  }
  std::vector<std::string> entries;
  std::string line;
  size_t oldest = 0;
  const auto finish_line = [&]()
  {
    if (line.empty())
      return;
    if (entries.size() < kMaxEntries)
      AppendHistoryEntry(entries, std::move(line));
    else
    {
      entries[oldest] = std::move(line);
      oldest = (oldest + 1) % kMaxEntries;
    }
    line.clear();
  };
  std::array<char, 4096> buffer;
  do
  {
    file.read(buffer.data(), buffer.size());
    for (std::streamsize i = 0; i < file.gcount(); ++i)
      if (buffer[static_cast<size_t>(i)] == '\n')
        finish_line();
      else
        AppendSanitizedConsoleByte(line, static_cast<unsigned char>(buffer[static_cast<size_t>(i)]));
  } while (file);
  if (file.bad())
    return false;
  finish_line();
  std::rotate(entries.begin(), entries.begin() + oldest, entries.end());
  History = std::move(entries);
  return true;
}

bool UConsoleCommandHistory::Save() const
{
  if (FilePath.empty())
  {
    return false;
  }
  std::ofstream file(FilePath);
  if (!file.is_open())
  {
    return false;
  }
  for (const std::string &entry : History)
  {
    file << entry << '\n';
  }
  return static_cast<bool>(file);
}

void UConsoleCommandHistory::Append(std::string line)
{
  line = SanitizeConsoleLine(line);
  if (line.empty())
  {
    return;
  }
  if (!History.empty() && History.back() == line)
  {
    return;
  }
  if (History.size() == kMaxEntries)
    History.erase(History.begin());
  AppendHistoryEntry(History, std::move(line));
  Save();
}

std::string UConsoleCommandHistory::GetFromEnd(size_t indexFromEnd) const
{
  if (indexFromEnd >= History.size())
  {
    return {};
  }
  return History[History.size() - 1 - indexFromEnd];
}

} // namespace cutum

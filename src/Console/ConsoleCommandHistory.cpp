#include "Console/ConsoleCommandHistory.h"
#include "Console/ConsoleInputSanitize.h"

#include <fstream>

namespace cutum
{

void UConsoleCommandHistory::SetFilePath(std::filesystem::path path)
{
  FilePath = std::move(path);
}

bool UConsoleCommandHistory::Load()
{
  History.clear();
  if (FilePath.empty())
  {
    return false;
  }
  std::ifstream file(FilePath);
  if (!file.is_open())
  {
    return false;
  }
  std::string line;
  while (std::getline(file, line))
  {
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }
    line = SanitizeConsoleLine(std::move(line));
    if (!line.empty())
    {
      History.push_back(line);
    }
  }
  while (History.size() > kMaxEntries)
  {
    History.erase(History.begin());
  }
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
  line = SanitizeConsoleLine(std::move(line));
  if (line.empty())
  {
    return;
  }
  if (!History.empty() && History.back() == line)
  {
    return;
  }
  History.push_back(line);
  while (History.size() > kMaxEntries)
  {
    History.erase(History.begin());
  }
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

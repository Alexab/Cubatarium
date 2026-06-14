#include "ConsoleCommandHistory.h"
#include "ConsoleInputSanitize.h"

#include <fstream>

namespace cutum {

void UConsoleCommandHistory::SetFilePath(std::filesystem::path path)
{
    filePath_ = std::move(path);
}

bool UConsoleCommandHistory::Load()
{
    entries_.clear();
    if (filePath_.empty()) {
        return false;
    }
    std::ifstream file(filePath_);
    if (!file.is_open()) {
        return false;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        line = SanitizeConsoleLine(std::move(line));
        if (!line.empty()) {
            entries_.push_back(line);
        }
    }
    while (entries_.size() > kMaxEntries) {
        entries_.erase(entries_.begin());
    }
    return true;
}

bool UConsoleCommandHistory::Save() const
{
    if (filePath_.empty()) {
        return false;
    }
    std::ofstream file(filePath_);
    if (!file.is_open()) {
        return false;
    }
    for (const std::string& entry : entries_) {
        file << entry << '\n';
    }
    return static_cast<bool>(file);
}

void UConsoleCommandHistory::Append(std::string line)
{
    line = SanitizeConsoleLine(std::move(line));
    if (line.empty()) {
        return;
    }
    if (!entries_.empty() && entries_.back() == line) {
        return;
    }
    entries_.push_back(line);
    while (entries_.size() > kMaxEntries) {
        entries_.erase(entries_.begin());
    }
    Save();
}

std::string UConsoleCommandHistory::GetFromEnd(size_t indexFromEnd) const
{
    if (indexFromEnd >= entries_.size()) {
        return {};
    }
    return entries_[entries_.size() - 1 - indexFromEnd];
}

} // namespace cutum

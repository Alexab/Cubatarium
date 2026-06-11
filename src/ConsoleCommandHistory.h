#ifndef CONSOLE_COMMAND_HISTORY_H
#define CONSOLE_COMMAND_HISTORY_H

#include <filesystem>
#include <string>
#include <vector>

namespace cutum {

class ConsoleCommandHistory {
public:
    static constexpr size_t kMaxEntries = 100;

    void SetFilePath(std::filesystem::path path);
    bool Load();
    bool Save() const;

    void Append(std::string line);
    const std::vector<std::string>& Entries() const { return entries_; }

    size_t Size() const { return entries_.size(); }
    std::string GetFromEnd(size_t indexFromEnd) const;

private:
    std::filesystem::path filePath_;
    std::vector<std::string> entries_;
};

} // namespace cutum

#endif

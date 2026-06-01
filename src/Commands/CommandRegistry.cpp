#include "CommandRegistry.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace cutum {

void CommandRegistry::Register(const std::string& name, CommandHandler handler)
{
    handlers_[name] = std::move(handler);
}

std::vector<std::string> CommandRegistry::Tokenize(const std::string& line)
{
    std::vector<std::string> tokens;
    std::istringstream stream(line);
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

CommandResult CommandRegistry::ExecuteLine(const std::string& line) const
{
    const auto tokens = Tokenize(line);
    if (tokens.empty()) {
        return {false, "Empty command"};
    }
    std::string cmd = tokens[0];
    std::transform(cmd.begin(), cmd.end(), cmd.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto it = handlers_.find(cmd);
    if (it == handlers_.end()) {
        return {false, "Unknown command: " + cmd};
    }
    return it->second(tokens);
}

} // namespace cutum

#include "Commands/CommandRegistry.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace cutum
{

void UCommandRegistry::Register(const std::string &Name, CommandHandler handler)
{
  Handlers[Name] = std::move(handler);
}

std::vector<std::string> UCommandRegistry::Tokenize(const std::string &line)
{
  std::vector<std::string> tokens;
  std::istringstream stream(line);
  std::string token;
  while (stream >> token)
  {
    tokens.push_back(token);
  }
  return tokens;
}

CommandResult UCommandRegistry::ExecuteLine(const std::string &line) const
{
  const auto tokens = Tokenize(line);
  if (tokens.empty())
  {
    return {false, "Empty command"};
  }
  std::string cmd = tokens[0];
  std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c)
                 { return static_cast<char>(std::tolower(c)); });
  const auto it = Handlers.find(cmd);
  if (it == Handlers.end())
  {
    return {false, "Unknown command: " + cmd};
  }
  return it->second(tokens);
}

std::vector<std::string> UCommandRegistry::GetCommandNames() const
{
  std::vector<std::string> names;
  names.reserve(Handlers.size());
  for (const auto &[name, handler] : Handlers)
  {
    (void)handler;
    names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  return names;
}

std::string UCommandRegistry::FormatHelpText() const
{
  std::ostringstream oss;
  oss << "Commands:";
  for (const std::string &name : GetCommandNames())
  {
    oss << ' ' << name;
  }
  return oss.str();
}

} // namespace cutum

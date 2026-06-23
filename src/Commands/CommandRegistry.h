#ifndef COMMAND_REGISTRY_H
#define COMMAND_REGISTRY_H

#include "Gui/Interfaces/IGameCommandContext.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

class UCommandRegistry
{
public:
  using CommandHandler =
      std::function<CommandResult(const std::vector<std::string> &)>;

  void Register(const std::string &Name, CommandHandler handler);
  CommandResult ExecuteLine(const std::string &line) const;

private:
  static std::vector<std::string> Tokenize(const std::string &line);

  std::unordered_map<std::string, CommandHandler> Handlers;
};

} // namespace cutum

#endif

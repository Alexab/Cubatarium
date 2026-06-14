#ifndef I_GAME_COMMAND_CONTEXT_H
#define I_GAME_COMMAND_CONTEXT_H

#include <string>
#include <vector>

namespace cutum
{

struct CommandResult
{
  bool success{false};
  std::string text;
};

class IGameCommandContext
{
public:
  virtual ~IGameCommandContext() = default;
  virtual CommandResult Execute(const std::vector<std::string> &args) = 0;
  virtual void AddChatLine(const std::string &line) = 0;
};

} // namespace cutum

#endif

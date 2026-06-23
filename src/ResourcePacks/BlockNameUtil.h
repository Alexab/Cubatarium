#ifndef BLOCKNAMEUTIL_H
#define BLOCKNAMEUTIL_H

#include <string>
#include <utility>

namespace cutum
{

constexpr char kQualifiedBlockSeparator[] = "::";

bool IsQualifiedBlockName(const std::string &name);
std::pair<std::string, std::string> SplitQualifiedBlockName(const std::string &name);
std::string MakeQualifiedBlockName(const std::string &packId,
                                   const std::string &localName);
std::string LocalBlockName(const std::string &name);
std::string HumanizeBlockName(const std::string &name);

} // namespace cutum

#endif

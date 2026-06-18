#include "ResourcePacks/BlockNameUtil.h"

namespace cutum
{

bool IsQualifiedBlockName(const std::string &name)
{
  return name.find(kQualifiedBlockSeparator) != std::string::npos;
}

std::pair<std::string, std::string>
SplitQualifiedBlockName(const std::string &name)
{
  const auto pos = name.find(kQualifiedBlockSeparator);
  if (pos == std::string::npos)
  {
    return {std::string{}, name};
  }
  return {name.substr(0, pos), name.substr(pos + 2)};
}

std::string MakeQualifiedBlockName(const std::string &packId,
                                   const std::string &localName)
{
  return packId + kQualifiedBlockSeparator + localName;
}

std::string LocalBlockName(const std::string &name)
{
  return SplitQualifiedBlockName(name).second;
}

std::string HumanizeBlockName(const std::string &name)
{
  if (name.empty())
  {
    return {};
  }
  if (IsQualifiedBlockName(name))
  {
    const auto parts = SplitQualifiedBlockName(name);
    return HumanizeBlockName(parts.second) + " (" + parts.first + ")";
  }
  std::string out;
  out.reserve(name.size());
  for (size_t i = 0; i < name.size(); ++i)
  {
    const char ch = name[i];
    if (ch == '_')
    {
      out.push_back(' ');
    }
    else if (i == 0)
    {
      const unsigned char uch = static_cast<unsigned char>(ch);
      if (uch >= 'a' && uch <= 'z')
      {
        out.push_back(static_cast<char>(uch - 'a' + 'A'));
      }
      else
      {
        out.push_back(ch);
      }
    }
    else
    {
      out.push_back(ch);
    }
  }
  return out;
}

} // namespace cutum

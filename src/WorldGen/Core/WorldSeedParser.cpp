#include "WorldGen/Core/WorldSeedParser.h"
#include <algorithm>
#include <cctype>
#include <random>

namespace cutum
{

namespace
{

std::string TrimCopy(const std::string &s)
{
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
  {
    ++start;
  }
  size_t end = s.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(s[end - 1])))
  {
    --end;
  }
  return s.substr(start, end - start);
}

uint32_t RandomSeedValue()
{
  std::random_device rd;
  std::seed_seq seq{rd(), rd(), rd(), rd()};
  std::mt19937 gen(seq);
  return static_cast<uint32_t>(gen());
}

} // namespace

uint32_t Fnv1a32Seed(const std::string &text)
{
  uint32_t h = 2166136261u;
  for (unsigned char c : text)
  {
    h ^= c;
    h *= 16777619u;
  }
  return h;
}

int32_t JavaStringHashSeed(const std::string &text)
{
  int32_t h = 0;
  for (unsigned char c : text)
  {
    h = static_cast<int32_t>(31LL * static_cast<int64_t>(h) + c);
  }
  return h;
}

bool IsNumericSeedText(const std::string &text)
{
  if (text.empty())
  {
    return false;
  }
  size_t i = 0;
  if (text[i] == '+' || text[i] == '-')
  {
    ++i;
  }
  if (i >= text.size())
  {
    return false;
  }
  for (; i < text.size(); ++i)
  {
    if (!std::isdigit(static_cast<unsigned char>(text[i])))
    {
      return false;
    }
  }
  return true;
}

WorldSeedResolution ResolveWorldSeed(const std::string &input,
                                     WorldSeedHashAlgo hashAlgo)
{
  WorldSeedResolution result;
  result.raw = TrimCopy(input);
  result.hashAlgo = hashAlgo;

  if (result.raw.empty() || result.raw == "0")
  {
    result.resolved = RandomSeedValue();
    result.kind = WorldSeedKind::Random;
    return result;
  }

  if (IsNumericSeedText(result.raw))
  {
    try
    {
      const unsigned long long parsed = std::stoull(result.raw);
      result.resolved = static_cast<uint32_t>(parsed & 0xFFFFFFFFull);
      result.kind = WorldSeedKind::Numeric;
      return result;
    }
    catch (...)
    {
      // fall through to hash
    }
  }

  if (hashAlgo == WorldSeedHashAlgo::JavaStringHash)
  {
    result.resolved = static_cast<uint32_t>(JavaStringHashSeed(result.raw));
  }
  else
  {
    result.resolved = Fnv1a32Seed(result.raw);
  }
  result.kind = WorldSeedKind::Hashed;
  return result;
}

const char *WorldSeedKindToString(WorldSeedKind kind)
{
  switch (kind)
  {
  case WorldSeedKind::Hashed:
    return "hashed";
  case WorldSeedKind::Random:
    return "random";
  case WorldSeedKind::Numeric:
  default:
    return "numeric";
  }
}

WorldSeedKind WorldSeedKindFromString(const std::string &s)
{
  if (s == "hashed")
  {
    return WorldSeedKind::Hashed;
  }
  if (s == "random")
  {
    return WorldSeedKind::Random;
  }
  return WorldSeedKind::Numeric;
}

} // namespace cutum

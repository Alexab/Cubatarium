#include "UiSettings.h"

#include <algorithm>
#include <cctype>

namespace cutum {

namespace {

std::string ToLowerAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

BlockInputProfile BlockInputProfileFromString(const std::string& value)
{
    const std::string key = ToLowerAscii(value);
    if (key == "cubatarium") {
        return BlockInputProfile::Cubatarium;
    }
    return BlockInputProfile::Classic;
}

const char* BlockInputProfileToString(BlockInputProfile profile)
{
    switch (profile) {
    case BlockInputProfile::Cubatarium:
        return "cubatarium";
    case BlockInputProfile::Classic:
    default:
        return "classic";
    }
}

} // namespace cutum

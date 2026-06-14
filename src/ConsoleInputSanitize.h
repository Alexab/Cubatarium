#ifndef CONSOLE_INPUT_SANITIZE_H
#define CONSOLE_INPUT_SANITIZE_H

#include <cstddef>
#include <string>

namespace cutum
{

constexpr size_t kConsoleMaxLineLength = 256;

std::string SanitizeConsoleLine(std::string s);
std::string SanitizeConsolePaste(const std::string &raw, size_t currentLen,
                                 size_t maxTotalLen = kConsoleMaxLineLength);

} // namespace cutum

#endif

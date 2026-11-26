#ifndef CONSOLE_INPUT_SANITIZE_H
#define CONSOLE_INPUT_SANITIZE_H

#include <cstddef>
#include <string>
#include <string_view>

namespace cutum
{

constexpr size_t kConsoleMaxLineLength = 256;

void AppendSanitizedConsoleByte(std::string &line, unsigned char byte);
std::string SanitizeConsoleLine(std::string_view input);
std::string SanitizeConsolePaste(const std::string &raw, size_t currentLen,
                                 size_t maxTotalLen = kConsoleMaxLineLength);

} // namespace cutum

#endif

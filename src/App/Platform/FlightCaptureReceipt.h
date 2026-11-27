#pragma once

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace cutum
{
struct FlightCaptureReceipt
{
  std::uint32_t captureId{};
  std::uint64_t frameSequence{};
  std::array<float, 3> position{};
  float yaw{}, pitch{};
};

inline std::string EncodeFlightCaptureReceipt(const FlightCaptureReceipt &receipt,
    std::uint32_t captureIndex, int width, int height, std::uint64_t checksum,
    std::string_view pngName, bool frontBuffer)
{
  constexpr std::size_t fixedFields = 512;
  constexpr char hex[] = "0123456789abcdef";
  std::string text;
  if (pngName.size() > (text.max_size() - fixedFields) / 6)
    throw std::length_error("capture receipt name");
  text.reserve(fixedFields + pngName.size() * 6);
  const auto number = [&](auto value) {
    if constexpr (std::is_floating_point_v<decltype(value)>)
      if (!std::isfinite(value))
      {
        text += "null";
        return;
      }
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{})
      throw std::runtime_error("capture receipt number");
    text.append(buffer, result.ptr);
  };
  text += "{\"schema\":\"diagnostic-capture-receipt.v1\",\"capture_id\":";
  number(receipt.captureId);
  text += ",\"frame_sequence\":";
  number(receipt.frameSequence);
  text += frontBuffer ? ",\"source\":\"GL_FRONT\"" : ",\"source\":\"GL_BACK\"";
  text += ",\"position\":[";
  number(receipt.position[0]); text += ',';
  number(receipt.position[1]); text += ',';
  number(receipt.position[2]);
  text += "],\"yaw\":";
  number(receipt.yaw);
  text += ",\"pitch\":";
  number(receipt.pitch);
  text += ",\"png_fnv1a64\":\"";
  for (int shift = 60; shift >= 0; shift -= 4)
    text += hex[(checksum >> shift) & 15];
  text += "\",\"width\":";
  number(width);
  text += ",\"height\":";
  number(height);
  text += ",\"capture_index\":";
  number(captureIndex);
  text += ",\"png\":\"";
  for (const unsigned char byte : pngName)
  {
    if (byte == '"' || byte == '\\')
    {
      text += '\\';
      text += static_cast<char>(byte);
    }
    else if (byte < 32)
    {
      text += "\\u00";
      text += hex[byte >> 4];
      text += hex[byte & 15];
    }
    else
      text += static_cast<char>(byte);
  }
  text += "\"}\n";
  return text;
}
} // namespace cutum

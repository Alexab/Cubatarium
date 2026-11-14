#ifndef ITEM_ICON_FINGERPRINT_H
#define ITEM_ICON_FINGERPRINT_H

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <string>

namespace cutum
{
/// Disk cache identity includes content, not timestamps: texture-only edits and
/// same-size replacements must invalidate icons even on a cold application run.
/// Empty means dependencies cannot be validated; never reuse an older PNG.
inline std::string BuildItemIconFingerprint(const std::filesystem::path &root,
                                            const std::filesystem::path &model,
                                            float iconScale)
{
  try
  {
    const auto path = std::filesystem::weakly_canonical(root / model);
    std::set<std::filesystem::path> dependencies{path};
    if (path.extension() == ".gltf")
    {
      std::ifstream in(path);
      const auto gltf = nlohmann::json::parse(in);
      for (const char *kind : {"buffers", "images"})
        for (const auto &entry : gltf.value(kind, nlohmann::json::array()))
        {
          const auto uri = entry.value("uri", std::string());
          if (uri.empty() || uri.find("data:") == 0)
            continue;
          if (uri.find(':') != std::string::npos)
            return {};
          const auto dependency =
              std::filesystem::weakly_canonical(path.parent_path() / uri);
          const auto relative =
              dependency.lexically_relative(path.parent_path());
          if (relative.empty() || relative.is_absolute() ||
              *relative.begin() == "..")
            return {};
          dependencies.insert(dependency);
        }
    }
    for (const char *shader : {"item_surface.vert", "item_surface.frag",
                               "vshader.glsl", "fshader.glsl"})
      dependencies.insert(
          std::filesystem::weakly_canonical(root / "shaders" / shader));
    std::ostringstream out;
    out << "item-icon-v4-linear-albedo|"
        << std::setprecision(std::numeric_limits<float>::max_digits10)
        << iconScale;
    for (const auto &dependency : dependencies)
    {
      std::ifstream in(dependency, std::ios::binary);
      if (!in)
        return {};
      uint64_t hash = 14695981039346656037ull, size = 0;
      std::array<char, 65536> bytes;
      while (in)
      {
        in.read(bytes.data(), std::streamsize(bytes.size()));
        for (std::streamsize i = 0; i < in.gcount(); ++i)
        {
          hash ^= static_cast<unsigned char>(bytes[size_t(i)]);
          hash *= 1099511628211ull;
        }
        size += uint64_t(in.gcount());
      }
      if (!in.eof())
        return {};
      out << '|' << dependency.generic_string() << ':' << size << ':' << hash;
    }
    return out.str();
  }
  catch (const std::exception &)
  {
    return {};
  }
}
} // namespace cutum
#endif

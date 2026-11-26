#pragma once

#include "Render/Engine/RenderImageStorage.h"
#include <nlohmann/json.hpp>
#include <utility>

namespace cutum
{
inline nlohmann::json DescribeRenderImages(const RenderImageSnapshot &snapshot)
{
  using Json = nlohmann::json;
  auto images = Json::array();
  for (const auto &image : snapshot.images)
  {
    auto roles = Json::array();
    for (std::size_t i = 0; i < RenderImageRoles.size(); ++i)
      if (image.roles & (std::uint32_t{1} << i))
        roles.push_back(RenderImageRoles[i]);
    auto levels = Json::array();
    for (const auto &extent : image.levels)
      levels.push_back({{"level", extent.level}, {"width", extent.width}, {"height", extent.height},
          {"depth", extent.depth}, {"samples", extent.samples}, {"internal_format", extent.format},
          {"bits_per_sample", extent.bitsPerSample}, {"compressed", extent.compressed},
          {"logical_bytes", extent.logicalBytes}});
    const auto &first = image.levels.front();
    images.push_back({{"name", image.name}, {"target", image.target}, {"roles", std::move(roles)},
        {"width", first.width}, {"height", first.height}, {"depth", first.depth}, {"samples", first.samples},
        {"internal_format", first.format}, {"bits_per_sample", first.bitsPerSample}, {"logical_bytes", image.logicalBytes},
        {"levels", std::move(levels)}});
  }
  const char *status = !snapshot.contextAvailable ? "UNOBSERVED" :
      snapshot.failedQueries || snapshot.overflow ? "PARTIAL" : "PASS";
  return {{"schema", "render-image-storage.v1"}, {"status", status},
      {"logical_bytes", snapshot.logicalBytes}, {"references", snapshot.references},
      {"names_without_object", snapshot.namesWithoutObject}, {"images_without_storage", snapshot.imagesWithoutStorage},
      {"failed_queries", snapshot.failedQueries}, {"overflow", snapshot.overflow}, {"images", std::move(images)},
      {"whole_runtime_complete", false}};
}
} // namespace cutum

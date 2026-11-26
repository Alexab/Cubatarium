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
    images.push_back({{"name", image.name}, {"target", image.target}, {"roles", std::move(roles)},
        {"width", image.width}, {"height", image.height}, {"depth", image.depth}, {"samples", image.samples},
        {"internal_format", image.format}, {"bits_per_sample", image.bitsPerSample}, {"logical_bytes", image.logicalBytes}});
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

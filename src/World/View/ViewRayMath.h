#ifndef VIEWRAYMATH_H
#define VIEWRAYMATH_H

#include <glm/glm.hpp>

namespace cutum
{

/// Window-space mouse → world ray via inverse view-projection.
bool ScreenPointToWorldRay(const glm::mat4 &view, const glm::mat4 &proj,
                           const glm::ivec4 &viewport, float mouse_x,
                           float mouse_y, glm::vec3 &out_origin,
                           glm::vec3 &out_dir);

} // namespace cutum

#endif

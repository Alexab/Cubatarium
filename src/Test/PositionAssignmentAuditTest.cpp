#include "Render/Camera/PositionAssignmentAudit.h"

#include <cstdlib>
#include <iostream>

int main()
{
  cutum::PositionAssignmentAudit audit;
  const glm::vec3 start(0.0f, -48.0f, 0.0f);
  audit.NoteAssignment(start, start);
  if (audit.Count() != 0)
  {
    return EXIT_FAILURE;
  }
  // Each correction must be visible even when XZ chunk coordinates agree.
  for (int axis = 0; axis < 3; ++axis)
  {
    glm::vec3 corrected = start;
    corrected[axis] += 0.125f;
    audit.NoteAssignment(start, corrected);
  }
  if (audit.Count() != 3)
  {
    std::cerr << "position assignment audit missed a sub-block correction\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

#include "World/Physics/PhysicsQueuePurge.h"
#include <iostream>

namespace cutum
{

void WarnOncePhysicsQueuePurge(const char *queue_name)
{
  static bool warned_block = false;
  static bool warned_liquid = false;
  static bool warned_chunk = false;
  bool *slot = &warned_block;
  if (queue_name && queue_name[0] == 'l')
  {
    slot = &warned_liquid;
  }
  else if (queue_name && queue_name[0] == 'c')
  {
    slot = &warned_chunk;
  }
  if (!*slot)
  {
    std::cerr << "Physics queue purge: evicting entries from " << queue_name
              << std::endl;
    *slot = true;
  }
}

} // namespace cutum

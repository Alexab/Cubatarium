#ifndef CREATUREDRAWQUEUE_H
#define CREATUREDRAWQUEUE_H

#include "Creatures/Visual/CreatureDrawRequest.h"
#include <vector>

namespace cutum
{

class UGeometryEngine;

class CreatureDrawQueue
{
public:
  void Clear();
  void Push(CreatureDrawRequest request);
  void Flush(UGeometryEngine &engine);

  size_t Size() const { return Requests.size(); }

private:
  std::vector<CreatureDrawRequest> Requests;
};

} // namespace cutum

#endif

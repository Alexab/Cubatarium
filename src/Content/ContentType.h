#ifndef CONTENT_TYPE_H
#define CONTENT_TYPE_H

#include <string>

namespace cutum
{

struct ContentType
{
  std::string Id;
  std::string displayName;
  int sortOrder{0};
};

} // namespace cutum

#endif

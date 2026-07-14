#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace cutum
{

template <typename GetSortOrderFn>
void SortDefinitionIdsByCatalogOrder(std::vector<std::string> &ids,
                                     GetSortOrderFn getSortOrder)
{
  std::sort(ids.begin(), ids.end(),
            [&](const std::string &a, const std::string &b)
            {
              const int orderA = getSortOrder(a);
              const int orderB = getSortOrder(b);
              if (orderA != orderB)
              {
                return orderA < orderB;
              }
              return a < b;
            });
}

} // namespace cutum

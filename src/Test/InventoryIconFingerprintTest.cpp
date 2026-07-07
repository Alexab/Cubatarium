#include "Gui/Cache/InventoryIconService.h"

#include <iostream>

using cutum::UInventoryIconService;

int main()
{
  const std::string k1 =
      UInventoryIconService::BuildCacheKey("creature", "puffin", "");
  const std::string k2 =
      UInventoryIconService::BuildCacheKey("creature", "puffin", "");
  const std::string k3 =
      UInventoryIconService::BuildCacheKey("creature", "puffin", "skin_a");
  if (k1 != k2 || k1 == k3)
  {
    std::cerr << "BuildCacheKey is unstable\n";
    return 1;
  }

  const std::string h1 = UInventoryIconService::HashFingerprint("v2|a");
  const std::string h2 = UInventoryIconService::HashFingerprint("v2|a");
  const std::string h3 = UInventoryIconService::HashFingerprint("v2|b");
  if (h1 != h2 || h1 == h3)
  {
    std::cerr << "HashFingerprint is unstable\n";
    return 1;
  }
  return 0;
}

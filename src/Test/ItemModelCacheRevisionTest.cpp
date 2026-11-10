#include "Items/ItemDefinitionStorage.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "item_model_cache_revision_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  using namespace cutum;

  std::unordered_map<std::string, int> cache;
  uint64_t cacheRevision = 0;

  // First use adopts the current revision and keeps what gets cached.
  SyncItemModelCache(cache, cacheRevision);
  cache["iron_sword"] = 1;
  SyncItemModelCache(cache, cacheRevision);
  Expect(cache.size() == 1, "unchanged definitions keep the cache");

  // A resource-pack apply reloads the definitions: the cache is dropped once.
  UItemDefinitionStorage storage;
  const uint64_t before = UItemDefinitionStorage::ContentRevision();
  storage.Load("this/folder/does/not/exist");
  Expect(UItemDefinitionStorage::ContentRevision() != before,
         "Load bumps the revision (even when the folder is missing)");
  SyncItemModelCache(cache, cacheRevision);
  Expect(cache.empty(), "reloaded definitions clear the model cache");
  cache["iron_sword"] = 2;
  SyncItemModelCache(cache, cacheRevision);
  Expect(cache.size() == 1, "refilled cache stays until the next change");

  const uint64_t afterLoad = UItemDefinitionStorage::ContentRevision();
  storage.LoadOverlay("this/folder/does/not/exist");
  Expect(UItemDefinitionStorage::ContentRevision() == afterLoad,
         "an overlay that applies nothing leaves the revision alone");
  storage.Clear();
  Expect(UItemDefinitionStorage::ContentRevision() != afterLoad,
         "Clear bumps the revision");
  const uint64_t afterClear = UItemDefinitionStorage::ContentRevision();
  storage.LoadVisualPresets("this/file/does/not/exist.json");
  Expect(UItemDefinitionStorage::ContentRevision() != afterClear,
         "visual presets bump the revision");

  std::cout << "item_model_cache_revision_test: ok" << std::endl;
  return 0;
}

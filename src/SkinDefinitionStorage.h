#ifndef SKINDEFINITIONSTORAGE_H
#define SKINDEFINITIONSTORAGE_H

#include <string>
#include <unordered_map>
#include <vector>
#include "SkinDefinition.h"

namespace cutum {

class SkinDefinitionStorage {
public:
 void Load(const std::string& folder);
 const SkinDefinition* Get(const std::string& id) const;
 size_t Count() const { return definitions_.size(); }

 std::vector<std::string> ListEquippable() const;
 bool IsCompatible(const std::string& skinId, const std::string& speciesId) const;

private:
 bool LoadFile(const std::string& path);

 std::unordered_map<std::string, SkinDefinition> definitions_;
};

} // namespace cutum

#endif

#ifndef CREATUREDEFINITIONSTORAGE_H
#define CREATUREDEFINITIONSTORAGE_H

#include <string>
#include <unordered_map>
#include "CreatureDefinition.h"

namespace cutum {

class CreatureDefinitionStorage {
public:
 void Load(const std::string& folder);
 const CreatureDefinition* Get(const std::string& id) const;
 size_t Count() const { return definitions_.size(); }

private:
 bool LoadFile(const std::string& path);

 std::unordered_map<std::string, CreatureDefinition> definitions_;
};

} // namespace cutum

#endif

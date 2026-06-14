#ifndef CREATUREPOSEPRESENTERREGISTRY_H
#define CREATUREPOSEPRESENTERREGISTRY_H

#include <array>
#include <memory>
#include "ICreaturePosePresenter.h"
#include "LocomotionTypes.h"

namespace cutum {

class UCreaturePosePresenterRegistry {
 public:
 void Register(std::unique_ptr<ICreaturePosePresenter> presenter);
 ICreaturePosePresenter* Get(LocomotionArchetype archetype) const;
 void Clear();

 private:
 std::array<std::unique_ptr<ICreaturePosePresenter>, 5> presenters_{};
};

void RegisterDefaultCreaturePosePresenters(UCreaturePosePresenterRegistry& registry);

} // namespace cutum

#endif

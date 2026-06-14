#ifndef CREATUREPOSEPRESENTERREGISTRY_H
#define CREATUREPOSEPRESENTERREGISTRY_H

#include "Pose/ICreaturePosePresenter.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include <array>
#include <memory>

namespace cutum
{

class UCreaturePosePresenterRegistry
{
public:
  void Register(std::unique_ptr<ICreaturePosePresenter> presenter);
  ICreaturePosePresenter *Get(LocomotionArchetype archetype) const;
  void Clear();

private:
  std::array<std::unique_ptr<ICreaturePosePresenter>, 5> presenters_{};
};

void RegisterDefaultCreaturePosePresenters(
    UCreaturePosePresenterRegistry &registry);

} // namespace cutum

#endif

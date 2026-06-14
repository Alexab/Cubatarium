#include "CreaturePosePresenterRegistry.h"
#include "TerrestrialBipedPosePresenter.h"

namespace cutum {

void UCreaturePosePresenterRegistry::Register(std::unique_ptr<ICreaturePosePresenter> presenter)
{
 if (!presenter) {
  return;
 }
 const size_t index = static_cast<size_t>(presenter->GetArchetype());
 if (index < presenters_.size()) {
  presenters_[index] = std::move(presenter);
 }
}

ICreaturePosePresenter* UCreaturePosePresenterRegistry::Get(LocomotionArchetype archetype) const
{
 const size_t index = static_cast<size_t>(archetype);
 if (index >= presenters_.size()) {
  return nullptr;
 }
 return presenters_[index].get();
}

void UCreaturePosePresenterRegistry::Clear()
{
 for (auto& entry : presenters_) {
  entry.reset();
 }
}

void RegisterDefaultCreaturePosePresenters(UCreaturePosePresenterRegistry& registry)
{
 registry.Register(std::make_unique<UTerrestrialBipedPosePresenter>());
}

} // namespace cutum

#include "CreaturePosePresenterRegistry.h"
#include "TerrestrialBipedPosePresenter.h"

namespace cutum {

void CreaturePosePresenterRegistry::Register(std::unique_ptr<ICreaturePosePresenter> presenter)
{
 if (!presenter) {
  return;
 }
 const size_t index = static_cast<size_t>(presenter->GetArchetype());
 if (index < presenters_.size()) {
  presenters_[index] = std::move(presenter);
 }
}

ICreaturePosePresenter* CreaturePosePresenterRegistry::Get(LocomotionArchetype archetype) const
{
 const size_t index = static_cast<size_t>(archetype);
 if (index >= presenters_.size()) {
  return nullptr;
 }
 return presenters_[index].get();
}

void CreaturePosePresenterRegistry::Clear()
{
 for (auto& entry : presenters_) {
  entry.reset();
 }
}

void RegisterDefaultCreaturePosePresenters(CreaturePosePresenterRegistry& registry)
{
 registry.Register(std::make_unique<TerrestrialBipedPosePresenter>());
}

} // namespace cutum

#ifndef I_HOTBAR_VIEW_MODEL_H
#define I_HOTBAR_VIEW_MODEL_H

#include <array>
#include <cstddef>
#include <string>

namespace cutum {

struct HotbarSlotView {
    std::string id;
    std::string label;
    bool isBlock{true};
    bool selected{false};
};

class IHotbarViewModel {
public:
    virtual ~IHotbarViewModel() = default;
    virtual std::array<HotbarSlotView, 10> GetBlockSlots() const = 0;
    virtual std::array<HotbarSlotView, 10> GetPrefabSlots() const = 0;
    virtual size_t GetActiveBlockIndex() const = 0;
    virtual size_t GetActivePrefabIndex() const = 0;
    virtual void SelectBlockSlot(size_t index) = 0;
    virtual void SelectPrefabSlot(size_t index) = 0;
};

} // namespace cutum

#endif

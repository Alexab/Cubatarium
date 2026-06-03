#ifndef I_GUI_CLIPBOARD_H
#define I_GUI_CLIPBOARD_H

#include <string>

namespace cutum {

class IGuiClipboard {
public:
    virtual ~IGuiClipboard() = default;
    virtual std::string GetString() const = 0;
    virtual void SetString(const std::string& text) = 0;
};

} // namespace cutum

#endif

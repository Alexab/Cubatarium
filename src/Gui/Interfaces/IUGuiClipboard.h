#ifndef IU_GUI_CLIPBOARD_H
#define IU_GUI_CLIPBOARD_H

#include <string>

namespace cutum
{

class IUGuiClipboard
{
public:
  virtual ~IUGuiClipboard() = default;
  virtual std::string GetString() const = 0;
  virtual void SetString(const std::string &text) = 0;
};

} // namespace cutum

#endif

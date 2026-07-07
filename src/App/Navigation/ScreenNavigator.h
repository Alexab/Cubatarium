#ifndef SCREENNAVIGATOR_H
#define SCREENNAVIGATOR_H

namespace cutum
{

class UApplication;

/// Menu and screen transitions (extracted from UApplication).
class UScreenNavigator
{
public:
  explicit UScreenNavigator(UApplication *application);

  void ShowMainMenu();
  void ShowSettings();
  void ShowWorldSettings();
  void ShowNewWorld();
  void ShowLoadWorld();
  void ReturnToMainMenu();
  void CloseInventoryPalette();
  void CloseConsoleOverlay();
  void CloseWorldGenOverlay();

private:
  UApplication *Application;
};

} // namespace cutum

#endif

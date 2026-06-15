#ifndef ANDROID_SOFT_KEYBOARD_H
#define ANDROID_SOFT_KEYBOARD_H

struct android_app;

namespace cutum
{

class UApplication;
class UGuiTextInput;

void AndroidSoftKeyboardAttachApp(android_app *app);
void AndroidSoftKeyboardSetTarget(UGuiTextInput *input);
void AndroidSoftKeyboardClearTarget();
void AndroidSoftKeyboardProcess(UApplication *application);

} // namespace cutum

#endif

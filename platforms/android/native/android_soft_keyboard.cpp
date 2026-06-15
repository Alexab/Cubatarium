#include "android_soft_keyboard.h"

#include "App/Application.h"
#include "Gui/Widgets/GuiTextInput.h"

#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <game-text-input/gametextinput.h>

#include <string>

namespace cutum
{

namespace
{

android_app *gApp = nullptr;
UGuiTextInput *gActiveInput = nullptr;
std::string gImeBuffer;

void OnImeState(void *context, const GameTextInputState *state)
{
  auto *input = static_cast<UGuiTextInput *>(context);
  if (!input || !state || !state->text_UTF8)
  {
    return;
  }
  input->SetText(std::string(state->text_UTF8,
                             static_cast<size_t>(state->text_length)));
}

} // namespace

void AndroidSoftKeyboardAttachApp(android_app *app) { gApp = app; }

void AndroidSoftKeyboardSetTarget(UGuiTextInput *input)
{
  gActiveInput = input;
  if (!gApp || !gApp->activity || !input)
  {
    return;
  }

  gImeBuffer = input->GetText();
  GameTextInputState state{};
  state.text_UTF8 = gImeBuffer.c_str();
  state.text_length = static_cast<int32_t>(gImeBuffer.size());
  state.selection.start = state.selection.end =
      static_cast<int32_t>(gImeBuffer.size());
  state.composingRegion.start = SPAN_UNDEFINED;
  state.composingRegion.end = SPAN_UNDEFINED;

  auto *activity = gApp->activity;
  GameActivity_setImeEditorInfo(
      activity, TYPE_CLASS_TEXT, IME_ACTION_DONE,
      static_cast<GameTextInputImeOptions>(IME_FLAG_NO_FULLSCREEN));
  GameActivity_setTextInputState(activity, &state);
  GameActivity_showSoftInput(activity, GAMEACTIVITY_SHOW_SOFT_INPUT_IMPLICIT);
}

void AndroidSoftKeyboardClearTarget()
{
  if (gApp && gApp->activity && gActiveInput)
  {
    GameActivity_hideSoftInput(gApp->activity, 0);
  }
  gActiveInput = nullptr;
}

void AndroidSoftKeyboardProcess(UApplication *application)
{
  if (!gApp || !gApp->activity || gApp->textInputState == 0)
  {
    return;
  }

  if (gActiveInput)
  {
    GameActivity_getTextInputState(gApp->activity, OnImeState, gActiveInput);
  }
  gApp->textInputState = 0;
}

} // namespace cutum

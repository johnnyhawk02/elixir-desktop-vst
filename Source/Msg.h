// The page's player messages (the objects app/*.js posts to the AudioWorklet) parsed into lxr::Msg,
// on the message thread, so the audio thread only copies plain structs.
#pragma once
#include <juce_core/juce_core.h>
#include "Player.h"

namespace lxr {
// false for messages the plugin ignores (the recorder's rec / recStop) or doesn't know
bool parseMsg(const juce::var& v, Msg& m);
}

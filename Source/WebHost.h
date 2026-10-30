// The app's page, running for as long as the plugin exists (owned by the processor, so the DAW's automation
// and MIDI CCs reach it with the window closed); the editor only shows it. It's served with the stored settings and
// the sound to restore filled in, and wired to the processor: page messages in; step / level / host / MIDI / PERF
// parameter events out.
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "Player.h"
#include "Storage.h"

class ElixirProcessor;

class WebHost : private juce::Timer {
public:
    explicit WebHost(ElixirProcessor&);
    ~WebHost() override;
    juce::WebBrowserComponent& view() { return *browser; }
    void reload();

private:
    ElixirProcessor& proc;
    juce::SharedResourcePointer<Storage> storage;
    std::unique_ptr<juce::WebBrowserComponent> browser;
    std::unique_ptr<juce::FileChooser> chooser;
    bool hostWas = false, dialogs = false;
    lxr::Event evBuf[256];
    uint8_t midiBuf[64][3];
    std::optional<juce::WebBrowserComponent::Resource> serve(const juce::String& url);
    void fromPage(const juce::var& v);
    void timerCallback() override;
};

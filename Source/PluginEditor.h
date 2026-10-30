// The editor shows the processor's web view (WebHost): the app's page.
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class ElixirEditor : public juce::AudioProcessorEditor {
public:
    explicit ElixirEditor(ElixirProcessor&);
    ~ElixirEditor() override;
    void resized() override;
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff1b1d1a)); }

private:
    ElixirProcessor& proc;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ElixirEditor)
};

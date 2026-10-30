// Elixir Desktop as a VST3: the C++ player (Player.h) driven by the app's own page in the editor.
// The page's player messages come in through post() on the message thread; the audio thread takes them from a FIFO.
// Step and level events go back the other way for the page's displays.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Player.h"
#include <map>
#include <memory>

class WebHost;

class ElixirProcessor : public juce::AudioProcessor {
public:
    ElixirProcessor();
    ~ElixirProcessor() override;

    void prepareToPlay(double sampleRate, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // message thread: a message from the page (the same object it would post to its AudioWorklet)
    void post(const juce::var& m);
    juce::var uiState;                                   // the page's last snapshot of the sound
    WebHost& web();                                      // the page, made on first use (message thread)
    // the twelve PERF faders as host parameters (Perf 1..12, 0..1 across each fader's range), both ways: the page
    // reports its faders' positions and readouts; slotFromHost() gives the ones the DAW has moved since
    static constexpr int NUM_SLOTS = 12;
    void slotsFromPage(const juce::var& fracs, const juce::var& shows);
    bool slotFromHost(int i, float& x);
    // the editor drains these on its timer
    int readEvents(lxr::Event* dst, int max);
    int readMidi(uint8_t (*dst)[3], int max);
    std::atomic<bool> playing { false }, hostPlaying { false };

private:
    std::unique_ptr<lxr::Player> player;
    juce::CriticalSection postLock;
    std::vector<lxr::Msg> msgs; juce::AbstractFifo msgFifo { 256 };
    std::vector<lxr::Event> events; juce::AbstractFifo eventFifo { 1024 };
    uint8_t midi[256][3] {}; juce::AbstractFifo midiFifo { 256 };
    // the newest message of each kind (kit with every param edit since folded in), saved with the project so the
    // sound comes back even before the editor is opened
    std::map<juce::String, juce::var> engineState;
    std::unique_ptr<WebHost> webHost;
    juce::AudioParameterFloat* slots[NUM_SLOTS] {};
    std::atomic<float> slotSeen[NUM_SLOTS];               // the value last given to / taken from the page
    std::atomic<bool> slotMoved[NUM_SLOTS];               // the DAW has moved it since
    juce::String slotShow[NUM_SLOTS];                     // the page's readout at slotSeen
    juce::CriticalSection showLock;
    void remember(const juce::var& m);
    void push(const juce::var& m);
    void addEvents();
    JUCE_DECLARE_WEAK_REFERENCEABLE(ElixirProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ElixirProcessor)
};

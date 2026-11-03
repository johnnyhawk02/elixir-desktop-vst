// The page's localStorage, kept in ~/Library/Application Support/Johnny/Elixir Desktop/storage.json and shared by every
// instance of the plugin (saved presets, favourites, randomiser and MIDI settings live there). Message thread only.
#pragma once
#if JUCE_MAC
 #define ELIXIR_DATA_DIR "Application Support/Johnny/Elixir Desktop"
#else
 #define ELIXIR_DATA_DIR "Johnny/Elixir Desktop"
#endif
#include <juce_events/juce_events.h>

class Storage : private juce::Timer {
public:
    Storage();
    ~Storage() override;
    const juce::var& all() const { return data; }  // an object of key -> string
    void set(const juce::String& key, const juce::var& value); // void value: remove
private:
    juce::File file;
    juce::var data;
    void timerCallback() override;
    void save();
};

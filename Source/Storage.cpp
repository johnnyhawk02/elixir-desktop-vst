#include "Storage.h"

Storage::Storage() {
    file = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("Application Support/Johnny/Elixir Desktop/storage.json");
    data = juce::JSON::parse(file);
    if (!data.isObject()) data = juce::var(new juce::DynamicObject());
}
Storage::~Storage() { if (isTimerRunning()) save(); }

void Storage::set(const juce::String& key, const juce::var& value) {
    auto* o = data.getDynamicObject();
    if (value.isVoid() || value.isUndefined()) o->removeProperty(key);
    else o->setProperty(key, value.toString());
    startTimer(500); // a burst of changes is written once
}
void Storage::timerCallback() { stopTimer(); save(); }
void Storage::save() {
    file.getParentDirectory().createDirectory();
    const juce::File tmp = file.getSiblingFile("storage.json.tmp");
    if (tmp.replaceWithText(juce::JSON::toString(data, true))) tmp.moveFileTo(file);
}

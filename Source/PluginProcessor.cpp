#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Msg.h"
#include "WebHost.h"

ElixirProcessor::ElixirProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      msgs(256), events(1024) {
    player = std::make_unique<lxr::Player>(48000.0);
    for (int i = 0; i < NUM_SLOTS; i++) {
        slotSeen[i] = 0; slotMoved[i] = false;
        auto attrs = juce::AudioParameterFloatAttributes().withStringFromValueFunction([this, i](float v, int) {
            const juce::ScopedLock sl(showLock);
            if (std::abs(v - slotSeen[i].load()) < 0.002f && slotShow[i].isNotEmpty()) return slotShow[i];
            return juce::String(juce::roundToInt(v * 100)) + "%";
        });
        slots[i] = new juce::AudioParameterFloat(juce::ParameterID("perf" + juce::String(i + 1), 1), "Perf " + juce::String(i + 1),
                                                 juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f, attrs);
        addParameter(slots[i]);
    }
    // the page starts now, so the sound and the DAW's automation work before the window is ever opened
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr && juce::MessageManager::getInstance()->isThisTheMessageThread())
        web();
}
ElixirProcessor::~ElixirProcessor() = default;

WebHost& ElixirProcessor::web() {
    if (!webHost) webHost = std::make_unique<WebHost>(*this);
    return *webHost;
}

void ElixirProcessor::slotsFromPage(const juce::var& fracs, const juce::var& shows) {
    for (int i = 0; i < NUM_SLOTS && i < fracs.size(); i++) {
        const float x = juce::jlimit(0.0f, 1.0f, (float) (double) fracs[i]);
        { const juce::ScopedLock sl(showLock); slotShow[i] = shows[i].toString(); }
        if (std::abs(x - slots[i]->get()) < 1e-4f) { slotSeen[i] = slots[i]->get(); continue; }
        slotSeen[i] = x; // (so the audio thread doesn't send it back)
        slots[i]->beginChangeGesture();
        slots[i]->setValueNotifyingHost(x);
        slots[i]->endChangeGesture();
    }
}
bool ElixirProcessor::slotFromHost(int i, float& x) {
    if (!slotMoved[i].exchange(false)) return false;
    x = slotSeen[i];
    return true;
}

bool ElixirProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void ElixirProcessor::prepareToPlay(double sampleRate, int) { player->setOutRate(sampleRate); }

// ---- page -> player ----
void ElixirProcessor::remember(const juce::var& m) {
    const juce::String type = m["type"].toString();
    if (type == "atBar") { remember(m["m"]); return; }
    if (type == "param") {
        // folded into the kit, so the saved kit is the sound as it is now
        auto it = engineState.find("kit");
        if (it != engineState.end())
            if (auto* a = it->second["values"].getArray()) {
                const int par = (int) m["par"];
                if (par >= 0 && par < a->size()) a->set(par, m["value"]);
            }
        return;
    }
    if (type == "kit" || type == "mix" || type == "delay" || type == "xmod" || type == "rush" || type == "mute" || type == "seq") {
        engineState[type] = juce::JSON::parse(juce::JSON::toString(m, true)); // a deep copy
        return;
    }
    if (type == "rushSet") {
        // the page sends the whole set each time, but merge in case it sends part
        auto& rs = engineState["rushSet"];
        if (!rs.isObject()) rs = juce::JSON::parse(juce::JSON::toString(m, true));
        else if (auto* src = m["set"].getDynamicObject())
            for (auto& p : src->getProperties()) rs["set"].getDynamicObject()->setProperty(p.name, p.value);
    }
}
void ElixirProcessor::push(const juce::var& m) {
    const juce::ScopedLock sl(postLock);
    int s1, n1, s2, n2;
    msgFifo.prepareToWrite(1, s1, n1, s2, n2);
    if (n1 + n2 == 0) return; // full: the audio thread is stalled
    lxr::Msg& dst = msgs[(size_t) (n1 ? s1 : s2)];
    if (!lxr::parseMsg(m, dst)) return;
    msgFifo.finishedWrite(1);
}
void ElixirProcessor::post(const juce::var& m) { const juce::ScopedLock sl(postLock); remember(m); push(m); }

// ---- the project ----
void ElixirProcessor::getStateInformation(juce::MemoryBlock& dest) {
    const juce::ScopedLock sl(postLock);
    auto* o = new juce::DynamicObject();
    o->setProperty("v", 1);
    o->setProperty("ui", uiState);
    juce::Array<juce::var> eng;
    for (const char* k : { "kit", "mix", "delay", "xmod", "rush", "rushSet", "mute", "seq" }) {
        auto it = engineState.find(k);
        if (it == engineState.end()) continue;
        juce::var m = juce::JSON::parse(juce::JSON::toString(it->second, true));
        if (m["type"].toString() == "seq") m.getDynamicObject()->setProperty("playing", false);
        eng.add(m);
    }
    o->setProperty("engine", eng);
    juce::MemoryOutputStream(dest, false).writeString(juce::JSON::toString(juce::var(o), true));
}
void ElixirProcessor::setStateInformation(const void* data, int size) {
    const juce::var s = juce::JSON::parse(juce::MemoryInputStream(data, (size_t) size, false).readString());
    if (!s.isObject()) return;
    uiState = s["ui"];
    if (auto* eng = s["engine"].getArray()) for (const auto& m : *eng) post(m);
    juce::MessageManager::callAsync([safe = juce::WeakReference<ElixirProcessor>(this)] {
        if (auto* p = safe.get()) p->web().reload(); // the page comes back with the restored sound
    });
}

// ---- audio ----
void ElixirProcessor::addEvents() {
    const int n = player->nEvents;
    int s1, n1, s2, n2;
    eventFifo.prepareToWrite(n, s1, n1, s2, n2);
    for (int i = 0; i < n1; i++) events[(size_t) (s1 + i)] = player->events[i];
    for (int i = 0; i < n2; i++) events[(size_t) (s2 + i)] = player->events[n1 + i];
    eventFifo.finishedWrite(n1 + n2);
    player->nEvents = 0;
}

void ElixirProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiIn) {
    juce::ScopedNoDenormals noDenormals;
    {
        int s1, n1, s2, n2;
        const int n = msgFifo.getNumReady();
        msgFifo.prepareToRead(n, s1, n1, s2, n2);
        for (int i = 0; i < n1; i++) player->msg(msgs[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; i++) player->msg(msgs[(size_t) (s2 + i)]);
        msgFifo.finishedRead(n1 + n2);
    }
    bool has = false, hp = false; double bpm = 0, ppq = 0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            hp = pos->getIsPlaying();
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) { ppq = *q; has = bpm > 0; }
        }
    player->host(has, hp, bpm, ppq);
    // PERF parameters the DAW moved: to the page (WebHost's timer)
    for (int i = 0; i < NUM_SLOTS; i++) {
        const float v = slots[i]->get();
        if (std::abs(v - slotSeen[i].load()) > 1e-5f) { slotSeen[i] = v; slotMoved[i] = true; }
    }
    hostPlaying = has && hp;

    const int total = buffer.getNumSamples();
    float* L = buffer.getWritePointer(0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    float mono[512];
    // in pieces between MIDI events: notes 36..42 (C1..F#1) play drum 1..open hat, CCs go to the page (its PERF faders)
    int at = 0;
    auto run = [&](int upTo) {
        while (at < upTo) {
            const int n = R ? upTo - at : juce::jmin(upTo - at, 512);
            player->process(L + at, R ? R + at : mono, n);
            if (!R) for (int i = 0; i < n; i++) L[at + i] = 0.5f * (L[at + i] + mono[i]);
            at += n;
        }
    };
    for (const auto meta : midiIn) {
        const auto msg = meta.getMessage();
        run(juce::jlimit(at, total, meta.samplePosition));
        if (msg.isNoteOn()) {
            const int v = msg.getNoteNumber() - 36;
            if (v >= 0 && v < 7) {
                player->trig(v, juce::jlimit(1, 127, (int) msg.getVelocity()));
            }
        } else if (msg.isController()) {
            int s1, n1, s2, n2;
            midiFifo.prepareToWrite(1, s1, n1, s2, n2);
            if (n1 + n2) {
                auto* d = midi[n1 ? s1 : s2];
                d[0] = (uint8_t) (0xb0 | (msg.getChannel() - 1)); d[1] = (uint8_t) msg.getControllerNumber(); d[2] = (uint8_t) msg.getControllerValue();
                midiFifo.finishedWrite(1);
            }
        }
    }
    run(total);
    for (int c = 2; c < buffer.getNumChannels(); c++) buffer.clear(c, 0, total);
    addEvents();
    playing = player->playing;
}

int ElixirProcessor::readEvents(lxr::Event* dst, int max) {
    int s1, n1, s2, n2;
    eventFifo.prepareToRead(juce::jmin(max, eventFifo.getNumReady()), s1, n1, s2, n2);
    for (int i = 0; i < n1; i++) dst[i] = events[(size_t) (s1 + i)];
    for (int i = 0; i < n2; i++) dst[n1 + i] = events[(size_t) (s2 + i)];
    eventFifo.finishedRead(n1 + n2);
    return n1 + n2;
}
int ElixirProcessor::readMidi(uint8_t (*dst)[3], int max) {
    int s1, n1, s2, n2;
    midiFifo.prepareToRead(juce::jmin(max, midiFifo.getNumReady()), s1, n1, s2, n2);
    for (int i = 0; i < n1; i++) std::memcpy(dst[i], midi[s1 + i], 3);
    for (int i = 0; i < n2; i++) std::memcpy(dst[n1 + i], midi[s2 + i], 3);
    midiFifo.finishedRead(n1 + n2);
    return n1 + n2;
}

juce::AudioProcessorEditor* ElixirProcessor::createEditor() { return new ElixirEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ElixirProcessor(); }

#include "WebHost.h"
#include "PluginProcessor.h"
#include "BinaryData.h"
#if JUCE_MAC
 #include "WebDialogs.h"
#endif

WebHost::WebHost(ElixirProcessor& p) : proc(p) {
    // WebView2 needs a folder it can write to (the host's own folder usually isn't)
    const juce::File dataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile(ELIXIR_DATA_DIR);
    auto opts = juce::WebBrowserComponent::Options()
                    .withWinWebView2Options(juce::WebBrowserComponent::Options::WinWebView2()
                                                .withUserDataFolder(dataDir.getChildFile("WebView2")))
                    .withBackend(
                       #if JUCE_WINDOWS
                        juce::WebBrowserComponent::Options::Backend::webview2
                       #else
                        juce::WebBrowserComponent::Options::Backend::defaultBackend
                       #endif
                        )
                    .withNativeIntegrationEnabled()
                    .withKeepPageLoadedWhenBrowserIsHidden()
                    .withResourceProvider([this](const juce::String& url) { return serve(url); })
                    .withEventListener("elixir", [this](juce::var v) { fromPage(v); });
    browser = std::make_unique<juce::WebBrowserComponent>(opts);
    browser->setSize(1280, 780);
    browser->setVisible(true); // (events only go to a visible one; it has no parent while the window is closed)
    reload();
    startTimerHz(30);
}
WebHost::~WebHost() { stopTimer(); }

void WebHost::reload() { browser->goToURL(juce::WebBrowserComponent::getResourceProviderRoot()); }

std::optional<juce::WebBrowserComponent::Resource> WebHost::serve(const juce::String& url) {
    if (url != "/" && url != "/index.html") return std::nullopt;
    auto* o = new juce::DynamicObject();
    o->setProperty("store", storage->all());
    o->setProperty("ui", proc.uiState);
    o->setProperty("playing", proc.playing.load() || proc.hostPlaying.load());
    // JSON in a <script>: no "</" (a "</script" would end it)
    const juce::String native = "const ELIXIR_NATIVE = " + juce::JSON::toString(juce::var(o), true).replace("</", "<\\/") + ";";
    const juce::String page = juce::String::fromUTF8(BinaryData::index_html, BinaryData::index_htmlSize).replace("/*@NATIVE@*/", native);
    const auto utf8 = page.toUTF8();
    const auto* b = reinterpret_cast<const std::byte*>(utf8.getAddress());
    return juce::WebBrowserComponent::Resource { std::vector<std::byte>(b, b + std::strlen(utf8.getAddress())), "text/html" };
}

void WebHost::fromPage(const juce::var& v) {
    const juce::String t = v["t"].toString();
    if (t == "msg") proc.post(v["m"]);
    else if (t == "ui") proc.uiState = v["ui"];
    else if (t == "slots") proc.slotsFromPage(v["v"], v["show"]);
    else if (t == "copy") juce::SystemClipboard::copyTextToClipboard(v["s"].toString());
    else if (t == "save") {
        // an export (presets as JSON): where to put it
        const juce::String text = v["text"].toString();
        chooser = std::make_unique<juce::FileChooser>("Save", juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                                                                   .getChildFile(juce::File::createLegalFileName(v["name"].toString())), "*.json");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                             [text](const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) fc.getResult().replaceWithText(text); });
    }
    else if (t == "log" || t == "ready") {
        #if JUCE_MAC
        const juce::File log = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Logs/Johnny/Elixir Desktop.log");
       #else
        const juce::File log = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile(ELIXIR_DATA_DIR "/elixir-desktop.log");
       #endif
        log.getParentDirectory().createDirectory();
        log.appendText(juce::Time::getCurrentTime().toString(true, true) + " " + (t == "ready" ? juce::String("page ready") : v["s"].toString()) + "\n");
    }
    else if (t == "store") storage->set(v["k"].toString(), v["v"].isString() ? v["v"] : juce::var());
}

void WebHost::timerCallback() {
   #if JUCE_MAC
    if (!dialogs) if (auto* peer = browser->getPeer()) dialogs = addWebDialogs(peer->getNativeHandle());
   #endif
    juce::Array<juce::var> batch;
    const bool host = proc.hostPlaying.load();
    if (host != hostWas) {
        hostWas = host;
        auto* o = new juce::DynamicObject(); o->setProperty("type", "host"); o->setProperty("playing", host);
        batch.add(juce::var(o));
    }
    // PERF parameters the DAW moved
    for (int i = 0; i < ElixirProcessor::NUM_SLOTS; i++) {
        float x;
        if (!proc.slotFromHost(i, x)) continue;
        auto* o = new juce::DynamicObject(); o->setProperty("type", "slot"); o->setProperty("i", i); o->setProperty("x", x);
        batch.add(juce::var(o));
    }
    const int n = proc.readEvents(evBuf, 256);
    for (int i = 0; i < n; i++) {
        const lxr::Event& e = evBuf[i];
        auto* o = new juce::DynamicObject();
        if (e.type == lxr::Event::Step) {
            o->setProperty("type", "step"); o->setProperty("step", e.step); o->setProperty("bar", e.bar);
            juce::Array<juce::var> hits; for (int h = 0; h < e.nHits; h++) hits.add(e.hits[h]);
            o->setProperty("hits", hits); o->setProperty("gr", e.gr); o->setProperty("duck", e.duck);
        } else {
            o->setProperty("type", "lvl");
            juce::Array<juce::var> l; for (float x : e.lvl) l.add(x);
            o->setProperty("l", l);
        }
        batch.add(juce::var(o));
    }
    const int m = proc.readMidi(midiBuf, 64);
    for (int i = 0; i < m; i++) {
        auto* o = new juce::DynamicObject(); o->setProperty("type", "midi");
        o->setProperty("d", juce::Array<juce::var> { midiBuf[i][0], midiBuf[i][1], midiBuf[i][2] });
        batch.add(juce::var(o));
    }
    if (!batch.isEmpty()) browser->emitEventIfBrowserIsVisible("elixirEv", batch);
}

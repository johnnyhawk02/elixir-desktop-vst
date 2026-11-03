#include "Msg.h"

namespace lxr {

static double num(const juce::var& v, double dflt = 0) {
    if (v.isDouble() || v.isInt() || v.isInt64()) return (double) v;
    if (v.isBool()) return (bool) v ? 1 : 0;
    return dflt;
}
template <typename T> static void nums(const juce::var& a, T* dst, int n) {
    if (auto* arr = a.getArray()) for (int i = 0; i < n && i < arr->size(); i++) dst[i] = (T) num((*arr)[i]);
}
static void page(const juce::var& p, Page& P) {
    P = Page();
    for (int t = 0; t < 7; t++) {
        auto row = [&](const char* k) { const juce::var& a = p[k]; return a.isArray() && t < a.size() ? a[t] : juce::var(); };
        juce::var on = row("on"), acc = row("acc"), bar = row("bar");
        for (int s = 0; s < 16; s++) {
            P.on[t][s] = on.isArray() && s < on.size() && num(on[s]) != 0;
            P.acc[t][s] = acc.isArray() && s < acc.size() && num(acc[s]) != 0;
            P.bar[t][s] = (int16_t) (bar.isArray() && s < bar.size() ? num(bar[s]) : 0);
        }
    }
}

bool parseMsg(const juce::var& v, Msg& m) {
    const juce::String type = v["type"].toString();
    if (type == "atBar") { if (!parseMsg(v["m"], m)) return false; m.atBar = true; return true; }
    m.atBar = false;
    if (type == "param") { m.type = Msg::Param; m.par = (int) num(v["par"]); m.value = (int) num(v["value"]); return true; }
    if (type == "kit") {
        m.type = Msg::Kit; const juce::var& a = v["values"];
        m.kitN = juce::jmin(a.size(), 512);
        for (int i = 0; i < m.kitN; i++) m.kit[i] = (uint8_t) ((int) num(a[i]) & 0xff);
        return true;
    }
    if (type == "trig") { m.type = Msg::Trig; m.voice = (int) num(v["voice"]); m.vel = (int) num(v["vel"], 100); return true; }
    if (type == "rush") { m.type = Msg::Rush; m.rush = num(v["v"]); m.rushT = v.hasProperty("t") && !v["t"].isVoid() ? (int) num(v["t"]) : -1; return true; }
    if (type == "rushSet") {
        m.type = Msg::RushSetT; m.rsMask = 0;
        static const char* keys[11] = { "start", "p32", "top", "hats", "only", "kicks", "first", "hp", "hpTop", "rollMs", "hpMs" };
        const juce::var& s = v["set"];
        for (int i = 0; i < 11; i++) if (s.hasProperty(keys[i])) { m.rs[i] = num(s[keys[i]]); m.rsMask |= (uint16_t) (1 << i); }
        return true;
    }
    if (type == "mute") { m.type = Msg::Mute; std::fill(m.muted, m.muted + 6, (uint8_t) 0); nums(v["muted"], m.muted, 6); return true; }
    if (type == "xmod") {
        m.type = Msg::XMod; const juce::var& a = v["amt"];
        m.xOn = a.isArray();
        std::fill(m.xAmt, m.xAmt + 36, 0.0f); nums(a, m.xAmt, 36);
        const juce::var& t = v["target"];
        for (int i = 0; i < 6; i++) {
            const juce::String s = t.isArray() && i < t.size() ? t[i].toString() : juce::String();
            m.xTarget[i] = s == "filter" ? X_FILTER : s == "ring" ? X_RING : X_FM;
        }
        return true;
    }
    if (type == "delay") { m.type = Msg::DelayT; m.pos = num(v["pos"]); return true; }
    if (type == "mix") {
        m.type = Msg::Mix;
        const juce::var& M = v["master"];
        m.hasMaster = M.isObject(); m.masterMask = 0;
        static const char* keys[11] = { "comp", "thresh", "ratio", "attack", "release", "makeup", "duck", "duckRelease", "duckFx", "duckOn", "space" };
        if (m.hasMaster) for (int i = 0; i < 11; i++) if (M.hasProperty(keys[i])) { m.master[i] = num(M[keys[i]]); m.masterMask |= (uint16_t) (1 << i); }
        std::fill(m.fx, m.fx + 6, 0.0f); nums(v["fx"], m.fx, 6);
        const juce::var& d = v["dist"];
        m.hasDist = d.isObject();
        if (m.hasDist) {
            m.distType = (int) num(d["type"]); std::fill(m.distP, m.distP + 4, 0.0); nums(d["p"], m.distP, 4); m.distMix = num(d["mix"]);
            const juce::var& r = v["verb"]; m.hasVerb = r.isObject();
            if (m.hasVerb) { static const char* vk[4] = { "time", "decay", "tone", "early" }; for (int i = 0; i < 4; i++) m.verb[i] = num(r[vk[i]]); }
            std::fill(m.sendD, m.sendD + 6, 0.0f); nums(v["sendD"], m.sendD, 6);
            std::fill(m.sendR, m.sendR + 6, 0.0f); nums(v["sendR"], m.sendR, 6);
            const juce::var& ec = v["echo"];
            m.echoPct = num(ec["pct"], 100);
            m.echoSync = !(ec.hasProperty("sync") && ec["sync"].isBool() && !(bool) ec["sync"]);
            m.echoFree = num(ec["free"], 0.25); m.echoFine = num(ec["fine"], 0);
        }
        return true;
    }
    if (type == "seq") {
        m.type = Msg::Seq;
        const juce::var& P = v["pattern"]; const juce::var& pages = P["pages"];
        if (pages.isArray() && pages.size() > 0) {
            m.nPages = juce::jmin(pages.size(), MAX_PAGES);
            for (int i = 0; i < m.nPages; i++) page(pages[i], m.pages[i]);
        } else { m.nPages = 0; page(P, m.pages[0]); }
        m.patternOn = !(v["patternOn"].isBool() && !(bool) v["patternOn"]);
        m.bpm = num(v["bpm"], 120); m.vel_ = num(v["vel"], 100); m.accentDb = num(v["accentDb"]);
        m.playing = (bool) num(v["playing"]);
        const juce::var& off = v["groove"]["off"];
        m.hasGroove = off.isArray();
        if (m.hasGroove) for (int t = 0; t < 7; t++) { for (int s = 0; s < 16; s++) m.groove[t][s] = 0; if (t < off.size()) nums(off[t], m.groove[t], 16); }
        return true;
    }
    return false;
}

}

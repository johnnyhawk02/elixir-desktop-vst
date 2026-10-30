// The step sequencer, sends, sidechain/spacer, master distortion, rush high-pass and compressor around the
// engine, resampled from 44 140 Hz to the host rate: C++ twin of Elixir Desktop's app/player.js.
// Messages from the page arrive as Msg (parsed from the page's JSON on the message thread, see Msg.cpp);
// events go back as Event. The recorder and count-in are left out (the DAW records).
#pragma once
#include "Lxr.h"

namespace lxr {

struct Page { uint8_t on[7][16] {}, acc[7][16] {}; int16_t bar[7][16] {}; };
constexpr int MAX_PAGES = 16;

struct Master {
    bool comp = true; double thresh = -12, ratio = 3, attack = 5, release = 120, makeup = 0, duck = 0.4, duckRelease = 150;
    bool duckFx = false, duckOn = true; double space = 0;
};
struct RushSet { double start = 75, p32 = 80, top = 4, hats = 80, only = 98, kicks = 90, first = 95, hp = 90, hpTop = 1500, rollMs = 40, hpMs = 40; };

struct Msg {
    enum Type { None, Param, Kit, Trig, Rush, RushSetT, Mute, XMod, DelayT, Mix, Seq } type = None;
    bool atBar = false;
    int par = 0, value = 0;                                  // param
    uint8_t kit[512] {}; int kitN = 0;                       // kit
    int voice = 0, vel = 0;                                  // trig
    double rush = 0; int rushT = -1;                         // rush (rushT -1: unchanged)
    double rs[11] {}; uint16_t rsMask = 0;                   // rushSet, in RushSet field order
    uint8_t muted[6] {};                                     // mute
    bool xOn = false; float xAmt[36] {}; XTarget xTarget[6] {}; // xmod
    double pos = 0;                                          // delay
    // mix
    bool hasMaster = false; double master[11] {}; uint16_t masterMask = 0; // in Master field order
    float fx[6] {}; bool hasDist = false; int distType = 0; double distP[4] {}; double distMix = 0;
    bool hasVerb = false; double verb[4] {}; float sendD[6] {}, sendR[6] {}; double echoPct = 100;
    // seq
    Page pages[MAX_PAGES]; int nPages = 0;                   // nPages 0: one pattern in pages[0]
    bool patternOn = true, hasGroove = false, playing = false; double bpm = 120, groove[7][16] {}, vel_ = 100, accentDb = 0;
};

struct Event {
    enum Type { Step, Lvl } type = Step;
    int step = 0, bar = 0, hits[8] {}, nHits = 0; double gr = 0, duck = 1;
    float lvl[6] {};
};

class Player {
public:
    explicit Player(double outRate);
    void setOutRate(double outRate) { ratio = (double) REAL_FS / outRate; }
    void msg(const Msg& m);
    void trig(int voice, int velocity) { e.trigger(voice, velocity, 60); gainTo[voice < 5 ? voice : 5] = 1; } // a MIDI note
    // the host's transport, once a block before process(): while the host plays it runs the sequencer, in step with
    // its bar position, at its tempo; when it stops the page's own play button runs it again
    void host(bool hasTransport, bool playing, double bpm, double ppq);
    void process(float* L, float* R, int n);

    Engine e;
    // events from the last process() calls, read and cleared by the caller
    Event events[64]; int nEvents = 0;
    bool playing = false;

private:
    double ratio;
    float A[2][64] {}, B[2][64] {};
    int idx = 0; double frac = 0;
    Page pat[MAX_PAGES]; int nPages = 0; bool patternOn = true;
    double bpm = 120, vel = 100, accGain = 0.5;
    int step = 0; long bar = 0; double toNext = 0;
    Msg* atBarQ[32] {}; int nAtBar = 0; Msg atBarStore[32];
    double grooveOff[7][16] {};
    struct Hit { double at; int t; bool acc; double sw; int roll; bool free; };
    Hit queue[512]; int nQueue = 0;
    int sched[16][8] {}; int nSched[16] {};
    bool fresh = false;
    float gain[6], gainTo[6], fxGain[6] {}, fxAmt[6];
    uint8_t muted[6] {};
    RushSet rs; int rushT = 3;
    double rush = 0; bool rushPhOn = false; double rushPh = 0, rushLog = 1; bool rushHeadOn = false; Hit rushHead {}; double rushGap = 0, clock = 0, snareAt = -1e9;
    bool hpOn = false; double hpLog, hpMix = 0, hpG = 0, hpS[4] {};
    Master master;
    double spF[8]; double spS[48] {}; float spEnv[8] {}, spG[8]; float spL[64] {}, spR[64] {}, spKey[64] {}; bool spOn = false;
    double delayPos;
    FxBus dist; ReverbBus verb; Rng fxRng { 0x2545f491u };
    float sendD[6] {}, sendR[6] {}; double echoWidth = 1;
    double distMix = 0, distAt = 0;
    float busIn[128] {}; int16_t busOut[128] {};
    double duckEnv = 0, duckAt = 1, compEnv = 0, gr = 0, duckMin = 1;
    float lvl[6] {}; int lvlN = 0; bool lvlOn = false;
    // host transport
    bool hostOn = false, hostPlaying = false, pagePlaying = false; double pageBpm = 120, lastPpq = 0;

    void start();
    void apply(const Msg& m);
    void flushBar();
    void setPlaying(bool p);
    void schedule(int s, long bar, double at0);
    void scheduleRush(int s, long bar, int pg, const Page& P, double at0, double len);
    void push(const Hit& h) { if (nQueue < 512) queue[nQueue++] = h; }
    void spacer(const float* key, float* XL, float* XR);
    void highPass(float* L, float* R);
    void render(float (*dst)[64]);
    void event(const Event& ev) { if (nEvents < 64) events[nEvents++] = ev; }
};

}

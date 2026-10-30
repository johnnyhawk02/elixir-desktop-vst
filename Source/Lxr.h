// LXR drum engine: C++ twin of Elixir Desktop's lxr.js (itself a port of the LXR V2 firmware,
// github.com/erica-synths/lxr-desktop-app). GPL-3.0-only, like the original.
//
// This follows lxr.js line for line. JS numbers are doubles here, Float32Array / Math.fround values are
// rounded through float (f()), Int16Array stores wrap to int16. Built with -ffp-contract=off so nothing is
// fused that the JS doesn't fuse. tools/compare checks it sample for sample against the JS.
#pragma once
#include <cstdint>
#include <vector>

namespace lxr {

constexpr int REAL_FS = 44140;
constexpr int BLOCK = 64;

constexpr int FX_OFF = 0, FX_DISTORTION = 1, FX_RING = 2, FX_COMPRESSOR = 3, FX_DELAY = 4;
constexpr int MIXER_ROUTING_FX_BUS = 6;

struct Rng {
    uint32_t state;
    explicit Rng(uint32_t seed = 0x12345678u) : state(seed) {}
    uint32_t next() { uint32_t x = state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; state = x; return x; }
};

struct Osc {
    int output = 0; const uint32_t* pm = nullptr; uint32_t phaseInc = 0, phase = 0; double freq = 0; int waveform = 0;
    int tableOffset = 0; double pitchMod = 0, fmMod = 0, modNodeValue = 0; int midiFreq = 0; int baseNote = 0;
};
struct DecayEg { double decay = 0, slope = 0, value = 0; };
struct SlopeEg { double attack = 0, decay = 0, slope = 0, invSlope = 0; int repeat = 0, repeatCnt = 0; double value = 0; int state = 0; };
struct Transient { double phase = 0, pitch = 1; int waveform = 0; double volume = 1; };
struct OnePole { double r = 0, b = 0; };
struct Filter { double f = 0, g = 0, q = 0, s1 = 0, s2 = 0, a = 0, b = 0, drive = 0; };
struct ModNode { int destination = 0; double amount = 0, lastVal = 0; };
struct Lfo {
    uint32_t phase = 0, phaseInc = 1; int waveform = 0, retrigger = 0; uint32_t phaseOffset = 0; double rnd = 0; int sync = 0;
    double freq = 1, modNodeValue = 1; ModNode modTarget;
};

// what every voice has (the fields lxr.js reaches through the voice lists)
struct Voice {
    Osc osc; double vol = 0, velo = 0; int pan = 0; double triggeredPanL = 0, triggeredPanR = 0;
    Filter filter; int filterType = 0; Lfo lfo; Transient transGen; SlopeEg oscVolEg; struct { double value = 0; } snapEg;
    int volumeMod = 1; double combinedVolume = 0; OnePole faderFilter; double pwm = 0;
};
struct DrumVoice : Voice {
    Osc modOsc; double fmModAmount = 0; DecayEg oscPitchEg; double egPitchModAmount = 0;
    struct { double shape = 0; OnePole driveFilter; } distortion;
    double ampFilterInput = 0, lastGain = 0; int mixOscs = 1; OnePole mixerVolFilter;
};
struct Snare : Voice {
    Osc noiseOsc; double mix = 0; DecayEg oscPitchEg; double egPitchModAmount = 0, egValueOscVol = 0, distortionShape = 0;
};
struct Metal : Voice {
    Osc modOsc, modOsc2; double fmModAmount1 = 0, fmModAmount2 = 0, distortionShape = 0, egValueOscVol = 0;
    double decayClosed = 0, decayOpen = 0; int isOpen = 0;
};

struct Distortion {
    int type = 0; double drive = 0, p1 = 0, p2 = 0, p3 = 0;
    double lastOut[2] {}, hp1[2] {}, hp3[2] {}, lp1[2] {}, lp2[2] {}, lp3[2] {}, slew[2] {};
};
struct Ringmod { Osc osc; int16_t buf[BLOCK] {}; };
struct Compressor { double tresh = 0, attack = 0, release = 0, ratio = 0, markupGain = 0, env = 0; };
struct Delay {
    uint32_t mask = 0; bool hasSync = false; double sync = 0; uint32_t oldRead = 0, newRead = 0, write = 0;
    double fracReadNew = 0, fracReadOld = 0, interpValue = 0, interpStep = 0; int16_t tmp[2] {}, tmp2[2] {};
    double feedback = 0, range = 0, delayTime = 0, pendingDelayTime = 0; int timePending = 0; OnePole tone, tone2; int type = 0;
};

class FxBus {
public:
    FxBus();
    double dryWet = 0; int audioRouting = 0, fxType = FX_OFF; float parameter[4] {};
    Distortion distortion; Ringmod ringmod; Compressor compressor; Delay delay;
    std::vector<float> buffer; bool forceWet = false; int16_t input[BLOCK * 2] {};
    void setParameter(int idx, double value) { parameter[idx] = (float) value; updateParameter(); }
    void setDelaySync(bool on, double samples);
    void updateParameter();
    void process(int16_t* out, int size, Rng& rng);
};

// The firmware's reverb (FX/Reverb.c + VerbTools + TiltEq.c, compiled out there: USE_REVERB 0), as lxr.js's ReverbBus.
// Wet only. Parameters 0..1: time (pre-delay), feedback, composition (tilt EQ / damping), variation (early level).
struct VerbDelay { std::vector<float> buf; int readPtr = 0, length = 256, writePtr = 256; double roomSz = 1;
                   OnePole lp; double frac = 0, depth = 0, modVal = 0; bool mod = false; };
struct Allpass { double g = 0.5, out = 0, in = 0; VerbDelay d; };
struct Tilt { double a0 = 0, b1 = 0, lp = 0, lgain = 0, hgain = 0; };
class ReverbBus {
public:
    ReverbBus();
    int16_t input[BLOCK * 2] {};
    void setParameter(int idx, double v);
    void process(int16_t* out, int size); // adds into out
private:
    Tilt eq, eqFb; double lfoS[2] { 0.5, 0 }, lfoA = 0;
    double earlyVolume = 1, feedback = 0; OnePole lpFeedback, lpImpulse; double lastOut = 0, lastOut2 = 0;
    Allpass apDelay[2], apModDelay[2], inputAllpass[4]; VerbDelay delay[4], early, preDelay;
    float parameter[4] { 0.3f, 0.6f, 0.5f, 0.5f };
};

enum XTarget { X_FM, X_FILTER, X_RING };

class Engine {
public:
    explicit Engine(uint32_t seed = 0x12345678u);
    Rng rng; double bpm = 120; double faderVolume[6] { 1, 1, 1, 1, 1, 1 };
    uint8_t activeParameters[512] {}; int activeNrpnNumber = 0;
    float decimationRate[7] { 1, 1, 1, 1, 1, 1, 1 }; float decimationCnt[6] {}; int16_t voiceSamples[6] {};
    uint8_t audioRouting[6] {}; ModNode velocityModulators[6];
    DrumVoice drums[3]; Snare snare; Metal hat, cymbal;
    Lfo* lfos[6]; Voice* voices[6]; // drums 1-3, snare, cymbal, hat
    int16_t out[6][BLOCK] {};
    FxBus fx; int16_t fxOut[BLOCK * 2] {};

    void setCrossMod(const float* amounts, const XTarget* targets); // amounts null or all zero: off
    void loadInitKit();
    void loadKit(const uint8_t* values, int n);
    void setParam(int par, int value);
    void trigger(int voice, int velocity, int note = 60);
    void renderBlock();
    const int16_t* renderFx(const float* gains, double width); // width < 0: as the firmware

private:
    int16_t modBuf[BLOCK] {}, buf2[BLOCK] {}, buf3[BLOCK] {};
    struct { bool on = false; float amt[36] {}; XTarget target[6] {}; int16_t prev[6][BLOCK] {}; float env[6] {}; uint32_t pm[6][BLOCK] {}; } xmod;
    Osc* xOscs[6];

    void dispatch(int par, int data2, bool isFloat, double v);
    static int targetPar(int index);
    void setVelocityDestination(int voice, int par) { resetTargets(); velocityModulators[voice].destination = par; }
    void setLfoDestination(int lfo, int par) { resetTargets(); lfos[lfo]->modTarget.destination = par; }
    void modUpdate(ModNode& vm, double val);
    void setOriginal(int par);
    void resetTargets();
    void lfoRetrigger(int voice);
    void lfoSetFreq(Lfo& lfo, double x);
    void drumTrigger(int n, int vel, int note);
    void snareTrigger(int vel, int note);
    void metalTrigger(Metal& v, int lfoVoice, int vel, int note);
    void cymbalTrigger(int vel, int note);
    void hatTrigger(int vel, int isOpen, int note);
    void drumAsync(DrumVoice& v);
    void snareAsync(Snare& v);
    void metalAsync(Metal& v);
    void drumSync(int n, int16_t* buf);
    void snareSync(int16_t* buf);
    void metalSync(Metal& v, double fmGain, int faderIdx, int16_t* buf);
    void decimate(int n, int16_t* buf);
    void xmodBefore();
    void xmodAfter();
    void ccHandler(bool isCC2, int num, int data2, bool isFloat, double v);
};

}

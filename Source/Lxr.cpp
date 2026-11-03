// C++ twin of Elixir Desktop's lxr.js; see Lxr.h. Function names and comments follow the JS (and the firmware's C).
#include "Lxr.h"
#include "LxrTables.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace lxr {

// ---------------------------------------------------------------------------------------
// Numeric helpers (C semantics)
// ---------------------------------------------------------------------------------------
static inline double f(double x) { return (double) (float) x; }
static const double F_32767 = 32767.0;
static const double F_2POW32 = f(4294967295.0);
static const double PI = 3.141592653589793;

static inline int32_t toI32(double x) {
    if (x != x) return 0;
    if (x >= 2147483647.0) return 2147483647;
    if (x <= -2147483648.0) return (int32_t) -2147483647 - 1;
    return (int32_t) std::trunc(x);
}
static inline uint32_t toU32(double x) {
    if (x != x || x <= 0) return 0;
    if (x >= 4294967295.0) return 4294967295u;
    return (uint32_t) std::trunc(x);
}
static inline int16_t toI16(double x) { return (int16_t) (uint16_t) ((uint32_t) toI32(x) & 0xffffu); }
static inline int wrap16(int x) { return (int16_t) (uint16_t) ((uint32_t) x & 0xffffu); }
static inline int toU8(double x) { return (int) (toU32(x) & 0xff); }
static inline int ssat16(int x) { return x > 32767 ? 32767 : x < -32768 ? -32768 : x; }
static inline double ssat16d(double x) { return x > 32767 ? 32767 : x < -32768 ? -32768 : x; }
static inline int qadd16(int a, int b) { return ssat16(a + b); }
static inline int qsub16(int a, int b) { return ssat16(a - b); }
static inline double fmaf_(double a, double b, double c) { return (double) std::fma((float) a, (float) b, (float) c); }
static inline double jsRound(double x) { return std::floor(x + 0.5); }

// ---------------------------------------------------------------------------------------
// Oscillator.c
// ---------------------------------------------------------------------------------------
enum { SINE = 0, TRI = 1, SAW = 2, REC = 3, NOISE = 4, PWM = 5, NUM_OSC_WAVEFORMS = 6 };
static const int SEQ_DEFAULT_NOTE = 60;
static const double SEMITONE_UP_MINUS_1 = f(f(1.0594630943592952645618252949463) - 1);

static const int TBL = 11264;
static int16_t WAVE_MEM[TBL * 3 + 1];
static const int TRI_BASE = 0, REC_BASE = TBL, SAW_BASE = TBL * 2;
static struct WaveMemInit {
    WaveMemInit() {
        std::memcpy(WAVE_MEM, TRI_TABLE, sizeof(TRI_TABLE));
        std::memcpy(WAVE_MEM + TBL, REC_TABLE, sizeof(REC_TABLE));
        std::memcpy(WAVE_MEM + 2 * TBL, SAW_TABLE, sizeof(SAW_TABLE));
        WAVE_MEM[TBL * 3] = SAW_TABLE[10 * 1024];
    }
} waveMemInit;

static inline uint32_t freq2PhaseIncr(double x) { return toU32(f(f(f(4096 * x) / REAL_FS) * 1048576)); }
static inline uint32_t freq2PhaseIncr1024(double x) { return toU32(f(f(f(1024 * x) / REAL_FS) * 4194304)); }
static inline uint32_t freq2PhaseIncr32767(double x) { return toU32(f(f(f(1024 * x) / REAL_FS) * 131072)); }

static int clz32(uint32_t x) { int n = 32; while (x) { x >>= 1; n--; } return n; }
static int freqToTableIndex(double fr) {
    const int log2 = (31 - clz32(toU32(f(fr / 440)))) & 0xff;
    const int midiNote = 12 * log2 + 70;
    return (midiNote / 12) & 0xff;
}

static inline int lerp(const int16_t* mem, uint32_t idx, uint32_t frac, int shift) {
    const int a = mem[idx];
    const int64_t d = (int64_t) mem[idx + 1] - a;
    return (int16_t) (uint16_t) ((uint32_t) (a + (int) ((d * (int64_t) frac) >> shift)) & 0xffffu);
}

static void calcSineBlock(Osc& osc, int16_t* buf, int size, double gain) {
    const uint32_t* pm = osc.pm;
    for (int i = 0; i < size; i++) {
        const uint32_t phase = osc.phase, p = pm ? phase + pm[i] : phase;
        const int out = lerp(SINE_TABLE, p >> 20, p & 0xfffff, 20);
        osc.phase = phase + osc.phaseInc;
        buf[i] = toI16(f(out * gain));
    }
}
static void calcFmSineBlock(Osc& osc, const int16_t* mod, int16_t* buf, int size, double gain) {
    const uint32_t* pm = osc.pm;
    for (int i = 0; i < size; i++) {
        const uint32_t phase = osc.phase;
        const uint32_t index = (pm ? phase + pm[i] : phase) + (toU32(f(mod[i] * osc.fmMod)) << 17);
        const int out = lerp(SINE_TABLE, index >> 20, index & 0xfffff, 20);
        osc.phase = phase + osc.phaseInc;
        buf[i] = toI16(f(out * gain));
    }
}
static void calcFmBlock(Osc& osc, int base, const int16_t* mod, int16_t* buf, int size, double gain) {
    const int row = base + osc.tableOffset * 1024; const uint32_t* pm = osc.pm;
    for (int i = 0; i < size; i++) {
        const uint32_t phase = osc.phase;
        const uint32_t index = (pm ? phase + pm[i] : phase) + (toU32(f(mod[i] * osc.fmMod)) << 19);
        const int out = lerp(WAVE_MEM, (uint32_t) row + (index >> 22), index & 0x3fffff, 22);
        osc.phase = phase + osc.phaseInc;
        osc.output = out;
        buf[i] = toI16(f(out * gain));
    }
}
static inline uint32_t pwmShift(uint32_t phase, double pwm) { return toU32(f(f((double) phase) + f(pwm * 2147483648.0))); }
static void calcPwmFmBlock(Osc& osc, const int16_t* mod, int16_t* buf, int size, double gain, double pwm) {
    const int row = SAW_BASE + osc.tableOffset * 1024; const uint32_t* pm = osc.pm;
    for (int i = 0; i < size; i++) {
        const uint32_t phase = osc.phase;
        const uint32_t index = (pm ? phase + pm[i] : phase) + (toU32(f(mod[i] * osc.fmMod)) << 19);
        const uint32_t index2 = pwmShift(index, pwm);
        const int a = lerp(WAVE_MEM, (uint32_t) row + (index >> 22), index & 0x3fffff, 22);
        const int b = lerp(WAVE_MEM, (uint32_t) row + (index2 >> 22), index2 & 0x3fffff, 22);
        const int out = qsub16(a, b);
        osc.phase = phase + osc.phaseInc;
        osc.output = out;
        buf[i] = toI16(f(out * gain));
    }
}
static void calcWavetableOscBlock(Osc& osc, int base, int16_t* buf, int size, double gain) {
    const int row = base + osc.tableOffset * 1024; const uint32_t* pm = osc.pm;
    for (int i = 0; i < size; i++) {
        const uint32_t phase = osc.phase, p = pm ? phase + pm[i] : phase;
        const int out = lerp(WAVE_MEM, (uint32_t) row + (p >> 22), p & 0x3fffff, 22);
        osc.phase = phase + osc.phaseInc;
        buf[i] = toI16(f(out * gain));
    }
}
static void calcPwmBlock(Osc& osc, int16_t* buf, int size, double gain, double pwm) {
    const int row = SAW_BASE + osc.tableOffset * 1024; const uint32_t* pm = osc.pm;
    for (int i = 0; i < size; i++) {
        const uint32_t phase = osc.phase, p = pm ? phase + pm[i] : phase;
        const uint32_t phase2 = pwmShift(p, pwm);
        const int a = lerp(WAVE_MEM, (uint32_t) row + (p >> 22), p & 0x3fffff, 22);
        const int b = lerp(WAVE_MEM, (uint32_t) row + (phase2 >> 22), phase2 & 0x3fffff, 22);
        const int out = qsub16(a, b);
        osc.phase = phase + osc.phaseInc;
        osc.output = out;
        buf[i] = toI16(f(out * gain));
    }
}
static void calcNoiseBlock(Osc& osc, int16_t* buf, int size, double gain, Rng& rng) {
    for (int i = 0; i < size; i++) {
        const uint32_t last = osc.phase;
        osc.phase = osc.phase + osc.phaseInc;
        if (last > osc.phase) osc.output = (int16_t) (uint16_t) (rng.next() & 0xffffu);
        buf[i] = toI16(f(osc.output * gain));
    }
}
static void calcNextOscSampleBlock(Osc& osc, int16_t* buf, int size, double gain, double pwm, Rng& rng) {
    switch (osc.waveform) {
        case SINE: calcSineBlock(osc, buf, size, gain); break;
        case SAW: calcWavetableOscBlock(osc, SAW_BASE, buf, size, gain); break;
        case TRI: calcWavetableOscBlock(osc, TRI_BASE, buf, size, gain); break;
        case REC: calcWavetableOscBlock(osc, REC_BASE, buf, size, gain); break;
        case NOISE: calcNoiseBlock(osc, buf, size, gain, rng); break;
        case PWM: calcPwmBlock(osc, buf, size, gain, pwm); break;
        default: break;
    }
}
static void calcNextOscSampleFmBlock(Osc& osc, const int16_t* mod, int16_t* buf, int size, double gain, double pwm, Rng& rng) {
    switch (osc.waveform) {
        case SINE: calcFmSineBlock(osc, mod, buf, size, gain); break;
        case SAW: calcFmBlock(osc, SAW_BASE, mod, buf, size, gain); break;
        case TRI: calcFmBlock(osc, TRI_BASE, mod, buf, size, gain); break;
        case REC: calcFmBlock(osc, REC_BASE, mod, buf, size, gain); break;
        case NOISE: calcNoiseBlock(osc, buf, size, gain, rng); break;
        case PWM: calcPwmFmBlock(osc, mod, buf, size, gain, pwm); break;
        default: break;
    }
}
static void osc_setFreq(Osc& osc) {
    const double cur = f(f(osc.freq * osc.pitchMod) * osc.modNodeValue);
    switch (osc.waveform) {
        case SINE: case NOISE: osc.phaseInc = freq2PhaseIncr(cur); break;
        case SAW: case TRI: case REC: case PWM: {
            osc.phaseInc = freq2PhaseIncr1024(cur);
            const int idx = freqToTableIndex(cur);
            osc.tableOffset = idx > 10 ? 10 : idx;
            break;
        }
        default: osc.phaseInc = freq2PhaseIncr32767(cur); break;
    }
}
static double calcDetune(int value) {
    const double frac = f(f(value / 127.0) - 0.5);
    return f(1 + f(frac * SEMITONE_UP_MINUS_1));
}
static void osc_recalcFreq(Osc& osc) {
    const double cent = calcDetune(osc.midiFreq & 0xff);
    int note = (osc.midiFreq >> 8) + (osc.baseNote - SEQ_DEFAULT_NOTE);
    if (note > 127) note = 127;
    if (note < 0) note = 0;
    osc.freq = f(MIDI_NOTE_FREQ[note] * cent);
}
static void osc_setBaseNote(Osc& osc, int baseNote) { osc.baseNote = baseNote & 0xff; osc_recalcFreq(osc); }
static void setCoarse(Osc& osc, int v) { osc.midiFreq = (osc.midiFreq & 0x00ff) | ((v << 8) & 0xffff); osc_recalcFreq(osc); }
static void setFine(Osc& osc, int v) { osc.midiFreq = (osc.midiFreq & 0xff00) | v; osc_recalcFreq(osc); }

// ---------------------------------------------------------------------------------------
// Envelopes: Decay.c, SlopeEg2.c, snapEg.c
// ---------------------------------------------------------------------------------------
static inline double decaySlope(double val, double slope) { return f(f(f(1 + slope) * val) / f(1 + f(slope * std::fabs(val)))); }
static const double TIME_K = f(f(2 * f(0.99)) / f(1 - f(0.99)));
static double decayEg_calcTime(int data2) {
    const double val = f(data2 / 127.0);
    const double ret = f(f(1 - f(f(f(1 + TIME_K) * val) / f(1 + f(TIME_K * std::fabs(val))))) * 2);
    return ret > 1 ? 1 : ret;
}
static void decayEg_setSlope(DecayEg& eg, int data2) {
    const double amount = f(f(f(data2 / 127.0) - 0.5) * 2);
    eg.slope = f(f(2 * amount) / f(1 - amount));
}
static double decayEg_calc(DecayEg& eg) {
    double val = eg.value;
    if (val > 0) val = f(val - eg.decay); else val = 0;
    eg.value = val;
    return decaySlope(val, eg.slope);
}

enum { EG_STOPPED = 0, EG_A = 1, EG_D = 2, EG_REPEAT = 3 };
static const double TIME_K_ATTACK = 2 * 0.99 / (1.0 - 0.99);
static const double TIME_K_DECAY = 2 * 0.999 / (1.0 - 0.999);
static inline double slopeValue(double val, double slope) { return (1.0 + slope) * val / (1.0 + slope * std::fabs(val)); }
static double slopeEg2_calcTime(int data2, double amount) {
    const double val = data2 / 127.0;
    const double ret = f((1.0 - ((1.0 + amount) * val / (1.0 + amount * std::fabs(val)))) * 2);
    return ret > 1 ? 1 : ret;
}
static void slopeEg2_setSlope(SlopeEg& eg, int data2) {
    const double amount = f(f(f(data2 / 127.0) - 0.5) * f(1.999));
    const double inv = -amount;
    eg.slope = 2 * amount / (1 - amount);
    eg.invSlope = 2 * inv / (1 - inv);
}
static void slopeEg2_init(SlopeEg& eg) {
    eg.attack = 0.01; eg.decay = 0.01; eg.value = 0; eg.state = EG_STOPPED; eg.repeat = 0;
    slopeEg2_setSlope(eg, 0);
}
static void slopeEg2_trigger(SlopeEg& eg) {
    if (!eg.repeat) eg.state = EG_A;
    else { eg.state = EG_REPEAT; eg.value = 1.0; }
    eg.repeatCnt = eg.repeat;
}
static double slopeEg2_calc(SlopeEg& eg) {
    double val = eg.value;
    switch (eg.state) {
        case EG_STOPPED: return 0.0;
        case EG_REPEAT:
            if (val > 0) val -= eg.attack;
            else {
                eg.repeatCnt = (eg.repeatCnt - 1) & 0xff;
                if (eg.repeatCnt > 0) val = 1.0;
                else { eg.state = EG_D; val = 1.0; }
            }
            eg.value = val;
            return slopeValue(val, eg.slope);
        case EG_A:
            val += eg.attack;
            if (val >= 1.0) { val = 1.0; eg.state = EG_D; }
            eg.value = val;
            return slopeValue(val, eg.invSlope);
        case EG_D:
            if (val > 0) { val -= eg.decay; eg.value = val; return slopeValue(val, eg.slope); }
            eg.value = 0;
            return 0;
        default: break;
    }
    return 0;
}
template <typename S> static double snapEg_calc(S& eg, double time) {
    double ret = 0;
    if (eg.value > 0) {
        ret = f(f(eg.value * eg.value) * 24);
        eg.value = f(eg.value - f(f(0.2) * time));
    }
    return ret;
}

// ---------------------------------------------------------------------------------------
// transientGenerator.c (+ Elixir Desktop's extra clicks)
// ---------------------------------------------------------------------------------------
static const int NUM_TRANSIENTS = 12;
static const int TRANSIENT_LEN = 2205;
static const double TRANSIENT_END = 2311061504.0;
static const int NUM_WAVES = NUM_TRANSIENTS + 2 + NUM_EXTRA_CLICKS;
static void transient_setWaveform(Transient& t, int w) { t.waveform = w < NUM_WAVES ? w : 0; }
static void extra_calcBlock(Transient& t, int16_t* buf, int size) {
    const Click& d = EXTRA_CLICKS[t.waveform - NUM_TRANSIENTS - 2]; const int end = d.length - 1;
    const double inc = t.pitch * 1048576;
    for (int i = 0; i < size; i++) {
        const double idx = t.phase / 1048576;
        if (idx < end) {
            const int k = (int) idx; const double fr = idx - k;
            buf[i] = toI16(t.volume * (d.data[k] + (d.data[k + 1] - d.data[k]) * fr));
            t.phase += inc;
        } else buf[i] = 0;
    }
}
static void transient_calcBlock(Transient& t, int16_t* buf, int size) {
    if (t.waveform >= NUM_TRANSIENTS + 2) { extra_calcBlock(t, buf, size); return; }
    if (t.waveform <= 1 || t.phase >= TRANSIENT_END) { std::fill(buf, buf + size, (int16_t) 0); return; }
    const int base = (t.waveform - 2) * TRANSIENT_LEN;
    const double inc = f(t.pitch * 1048576);
    for (int i = 0; i < size; i++) {
        if (t.phase < TRANSIENT_END) {
            const uint32_t idx = ((uint32_t) t.phase) >> 20;
            buf[i] = toI16(f(f(t.volume * (TRANSIENT_DATA[(uint32_t) base + idx] * 256)) * (idx < (uint32_t) TRANSIENT_LEN ? 1 : 0)));
            t.phase = toU32(f(f(t.phase) + inc));
        } else buf[i] = 0;
    }
}

// ---------------------------------------------------------------------------------------
// distortion.c, 1PoleLp.c, valueShaper.h
// ---------------------------------------------------------------------------------------
static inline double distShape(double shape) { shape = f(shape - 0.0078125); return f(f(2 * shape) / f(1 - shape)); }
static inline int16_t distortSample(int s, double shape) {
    double x = f(s / F_32767);
    x = f(f(f(1 + shape) * x) / f(1 + f(shape * std::fabs(x))));
    return toI16(f(x * 32767));
}
static inline double calcOnePole(OnePole& flt, double input) { flt.b = fmaf_(flt.r, input - flt.b, flt.b); return flt.b; }
static double valueShaperF2F(double val, double shape) {
    const double k = f(f(2 * shape) / f(f(1.0001) - shape));
    return f(f(f(1 + k) * val) / f(1 + f(k * std::fabs(val))));
}
static const double FILTER_SHAPER = f(-0.9);

// ---------------------------------------------------------------------------------------
// ResonantFilter.c
// ---------------------------------------------------------------------------------------
enum { FILTER_LP = 1, FILTER_HP = 2, FILTER_BP = 3, FILTER_UNITY_BP = 4, FILTER_NOTCH = 5, FILTER_PEAK = 6, FILTER_NAIVE_2_POLE = 7, FILTER_OFF = 8 };
static inline double fastTanh(double v) {
    if (v < f(-1.95)) return -1;
    if (v > f(1.95)) return 1;
    return f(f(f(4.15) * v) / f(f(4.29) + f(v * v)));
}
static inline double fastTan(double x) {
    x = f(x);
    const double x2 = f(x * x);
    const double A = f(f(-15 * x) + f(x * x2));
    const double B = f(3 * f(-5 + f(2 * x2)));
    return f(A / B);
}
static inline double tanhXdX(double x) {
    const double a = f(x * x);
    return f(f(f(f(a + 105) * a) + 945) / f(f(f(f(15 * a) + 420) * a) + 945));
}
static inline double softClipTwo(double x) { return f(x * tanhXdX(f(0.5 * x))); }
static void SVF_directSetFilterValue(Filter& flt, double val) { flt.f = f(val * f(0.5 * f(0.9))); flt.g = fastTan(PI * flt.f); }
static void SVF_init(Filter& flt) {
    flt.s1 = 0; flt.s2 = 0; flt.a = flt.b = 0;
    flt.f = f(0.20); flt.q = f(0.9); flt.drive = f(0.315);
    SVF_directSetFilterValue(flt, f(0.25));
}
static void SVF_reset(Filter& flt) { flt.s1 = 0; flt.s2 = 0; flt.a = flt.b = 0; }
static void SVF_setReso(Filter& flt, double fb) { flt.q = f(1 - fb); if (flt.q < f(0.1)) flt.q = f(0.02); }
static void SVF_setDrive(Filter& flt, double d) { flt.drive = f(f(0.4) + f(f(d * d) * 6)); }
static void SVF_recalcFreq(Filter& flt) { flt.g = fastTan(PI * flt.f); }
static void SVF_calcBlockZDF(Filter& flt, int type, int16_t* buf, int size) {
    const double g = flt.g;
    const double R = flt.f >= f(0.4499) ? 1 : flt.q;
    const double gg = f(g * g);
    if (type == FILTER_OFF) return;
    if (type == FILTER_NAIVE_2_POLE) {
        const double f2 = f(flt.f * f(2.21));
        for (int i = 0; i < size; i++) {
            const double x = softClipTwo(f(f(buf[i] / F_32767) * flt.drive));
            const double q = f(f(1 - flt.q) * 1.4 + f(1 - flt.q) / (1.0 - f2));
            flt.a = f(flt.a + f(f2 * f(f(x - flt.a) + f(q * f(flt.a - flt.b)))));
            if (flt.a > 1) flt.a = 1; else if (flt.a < -1) flt.a = -1;
            flt.b = f(flt.b + f(f2 * f(flt.a - flt.b)));
            if (flt.b > 1) flt.b = 1; else if (flt.b < -1) flt.b = -1;
            buf[i] = (int16_t) ssat16(toI32(f(flt.b * 32767)));
        }
        return;
    }
    if (flt.s1 != flt.s1) flt.s1 = 0;
    if (flt.s2 != flt.s2) flt.s2 = 0;
    for (int i = 0; i < size; i++) {
        const double x = softClipTwo(f(f(buf[i] / F_32767) * flt.drive));
        const double g0 = f(1 / f(1 + f(f(g * 2) * R)));
        const double s1 = flt.s1, s2 = flt.s2;
        const double f1 = f(gg * g0);
        const double y1 = f(f(f(f(f1 * x) + s2) + f(f(g * g0) * s1)) / f(f1 + 1));
        const double xx = f(x - y1);
        const double y0 = f(f(softClipTwo(s1) + f(g * xx)) * g0);
        flt.s1 = f(softClipTwo(s1) + f(f(2 * g) * f(xx - f(f(2 * R) * y0))));
        flt.s2 = f(s2 + f(f(2 * g) * y0));
        double out;
        switch (type) {
            case FILTER_LP: out = f(fastTanh(y1) * 32767); break;
            case FILTER_HP: { const double ugb = f(f(2 * R) * y0); out = f(f(f(x - ugb) - y1) * 32767); break; }
            case FILTER_BP: out = f(y0 * 32767); break;
            case FILTER_UNITY_BP: out = f(f(f(2 * R) * y0) * 32767); break;
            case FILTER_NOTCH: out = f(f(x - f(f(2 * R) * y0)) * 32767); break;
            case FILTER_PEAK: { const double ugb = f(f(2 * R) * y0); const double h = f(f(x - ugb) - y1); out = f(f(y1 - h) * 32767); break; }
            default: return;
        }
        buf[i] = (int16_t) ssat16(toI32(out));
    }
}

// ---------------------------------------------------------------------------------------
// lfo.c
// ---------------------------------------------------------------------------------------
static const int NUM_LFO_WAVEFORMS = 8;
static const double LFO_SR = f(REAL_FS / f(BLOCK));
static const double LFO_MAX_F = 200;
static double lfo_calc(Lfo& lfo, Rng& rng) {
    const uint32_t old = lfo.phase;
    lfo.phase = lfo.phase + toU32(f(lfo.phaseInc * lfo.modNodeValue));
    const bool overflow = old > lfo.phase;
    const uint32_t ph = lfo.phase;
    switch (lfo.waveform) {
        case 0: return f(f(f(SINE_TABLE[ph >> 20] / F_32767) + 1) / 2);
        case 1: return f(1 - std::fabs(f(f(f(f((double) ph) / F_2POW32) * 2) - 1)));
        case 2: return f(f((double) ph) / F_2POW32);
        case 4: return ph > 0x7fffffffu ? 1 : 0;
        case 5:
            if (overflow) { lfo.rnd = f((double) rng.next()); lfo.rnd = f(lfo.rnd / F_2POW32); }
            return lfo.rnd;
        case 3: return f(f((double) (0xffffffffu - ph)) / F_2POW32);
        case 6: { const double x = f(f((double) ph) / F_2POW32); return f(f(x * x) * x); }
        case 7: { const double x = f(f((double) (0xffffffffu - ph)) / F_2POW32); return f(f(x * x) * x); }
        default: return 0;
    }
}
static const double SYNC_SCALERS[12] = { 0, 0.25, 0.5, 1, 2, 3, 4, 6, 8, 12, 16, 32 };
static uint32_t lfo_calcPhaseInc(double freq, int sync, double bpm) {
    if (sync == 0 || sync > 11) return toU32(f(f(freq / LFO_SR) * F_2POW32));
    const double b = f(bpm);
    const double tempo = b > 0.0 ? f(f(b / 60) / 4) : 0;
    const double syncFreq = f(tempo * SYNC_SCALERS[sync]);
    return toU32(f(f(syncFreq / LFO_SR) * F_2POW32));
}

// ---------------------------------------------------------------------------------------
// Voices
// ---------------------------------------------------------------------------------------
static void initOsc(Osc& o, double freq, int waveform, double fmMod, double pitchMod, double modNodeValue) {
    o.freq = freq; o.waveform = waveform; o.fmMod = fmMod; o.midiFreq = 70 << 8 | 64; o.pitchMod = pitchMod; o.modNodeValue = modNodeValue;
}
static void makeDrumVoice(DrumVoice& v, int i) {
    v.fmModAmount = f(0.5); v.vol = f(0.8); v.oscPitchEg.decay = f(0.01); v.egPitchModAmount = f(0.5);
    v.filterType = 1; v.mixOscs = 1; v.volumeMod = 1;
    v.faderFilter.r = 0.01; v.mixerVolFilter.r = 0.01; v.distortion.driveFilter.r = 0.01;
    SVF_init(v.filter);
    initOsc(v.modOsc, 440, 1, 0, 1, 1);
    initOsc(v.osc, 440, TRI + i, 0, 0, 1);
    slopeEg2_init(v.oscVolEg);
    v.distortion.shape = distShape(0);
}
static void makeSnare(Snare& v) {
    v.vol = f(0.8); v.oscPitchEg.decay = f(0.01); v.egPitchModAmount = f(0.5); v.volumeMod = 1; v.filterType = 0;
    v.faderFilter.r = 0.1;
    initOsc(v.noiseOsc, 440, 1, 0, 1, 1);
    initOsc(v.osc, 440, 1, 0, 0, 1);
    v.distortionShape = distShape(0);
    slopeEg2_init(v.oscVolEg);
    SVF_init(v.filter);
}
static void makeMetal(Metal& v) {
    v.fmModAmount1 = f(0.5); v.fmModAmount2 = f(0.5); v.vol = f(0.8); v.volumeMod = 1; v.filterType = 0;
    v.faderFilter.r = 0.1;
    v.distortionShape = distShape(0);
    initOsc(v.modOsc, 440, SINE, 0, 1, 1);
    initOsc(v.modOsc2, 440, NOISE, 0, 1, 1);
    initOsc(v.osc, 440, 1, 1, 1, 1);
    slopeEg2_init(v.oscVolEg);
    SVF_init(v.filter);
}
static void updatePan(Voice& v) {
    v.triggeredPanL = f(std::sqrt(f((127 - v.pan) / 127.0)));
    v.triggeredPanR = f(std::sqrt(f(v.pan / 127.0)));
}
static inline double drumSineStart(double offset) { return f(1024 + f(((0x3ff << 20) - 1024) * offset)); }
static inline double otherSineStart(double offset) { return f((0x3ff << 20) * offset); }
static void resetPhase(Osc& osc, double offset, bool drum) {
    if (osc.waveform == SINE) osc.phase = toU32(drum ? drumSineStart(offset) : otherSineStart(offset));
    else if (osc.waveform > SINE && osc.waveform <= REC) osc.phase = toU32(f((0xff << 22) * offset));
    else osc.phase = 0;
}

// ---------------------------------------------------------------------------------------
// FX bus
// ---------------------------------------------------------------------------------------
enum { DIST_TUBE = 0, DIST_FOLD = 1, DIST_RAT = 2 };
static const uint32_t FX_BUFFER_SIZE = 0x7fff;
static const uint32_t SYNC_BUFFER_SIZE = 0x7ffff;
static const double F_32768 = 32768.0;
static const double F_REAL_FS = REAL_FS;
static const double LOW_MIN = f(0.0006 * REAL_FS), LOW_MAX = f(0.02 * REAL_FS), HI_MIN = f(0.02 * REAL_FS), HI_MAX = f(FX_BUFFER_SIZE);
static const double PP_MAX = f(FX_BUFFER_SIZE * f(0.48));
static const double TONE_C0 = 0.06537948995454451662, TONE_C1 = 0.92410701734426630737;
static const double SLEW_MAX = 6.79655641141821477118;

// rounded to a half float (round to nearest even), as lxr.js's toHalf (no _Float16 on every compiler)
static double toHalf(double x) {
    const double a = std::fabs(x);
    if (a == 0) return x;
    // half subnormals have a fixed quantum of 2^-24, normals 11 significant bits
    const int e = a < 6.103515625e-5 ? -14 : (int) std::floor(std::log2(a));
    int ee = e;
    if (a >= 6.103515625e-5) { if (std::ldexp(1.0, ee) > a) ee--; else if (std::ldexp(1.0, ee + 1) <= a) ee++; }
    const double q = std::ldexp(1.0, ee - 10);
    const double m = a / q;
    double r = std::floor(m);
    const double d = m - r;
    if (d > 0.5 || (d == 0.5 && ((long long) r & 1))) r++;
    const double v = r * q;
    return x < 0 ? -v : v;
}
static inline int dryWetMix(int dry, int wet, double dryWet) { return wrap16(dry + ssat16(toI32(f((wet - dry) * dryWet)))); }

static double diode(double x) {
    x = f(x);
    x = x < -1 ? f(-1) : x > 1 ? f(1) : x;
    const double index = f(f(x + 1) * 128);
    const int itg = toI32(index) & 0xffff;
    const double frac = f(index - itg);
    const double a = TABLE_1N914[itg], b = itg < 256 ? TABLE_1N914[itg + 1] : 0;
    return f(frac * b + f(a - f(frac * a)));
}
static double foldback(double x, double th) {
    if (x > th || x < -th) x = f(std::fabs(std::fabs(std::fmod(f(x - th), f(th * 4))) - f(th * 2)) - th);
    return x;
}
static void distortionProcess(Distortion& d, const int16_t* fx, int16_t* out, int size, double dryWet) {
    const double coff = TONE_C0 + d.p2 * TONE_C1;
    if (d.type == DIST_TUBE) {
        const double outMod = f((1.0 + 0.55 * d.p1) + 0.55 * d.p1);
        const double g = f(1 + f(d.drive * 4));
        for (int i = 0; i < size; i++)
            for (int c = 0; c < 2; c++) {
                const int x = fx[2 * i + c];
                const int32_t input = toI32(f(x * g));
                const int itg = ((ssat16(input) >> 9) + 64) & 0xff;
                const double frac = f((input & 511) / 512.0);
                double val = ECC83_TABLE[itg] + (ECC83_TABLE[itg + 1] - ECC83_TABLE[itg]) * frac;
                d.lp3[c] += coff * (val - d.lp3[c]);
                val = d.lp3[c] * outMod * d.p3;
                const int wet = ssat16(toI32(val * 32768));
                out[2 * i + c] = (int16_t) qadd16(out[2 * i + c], dryWetMix(x, wet, dryWet));
            }
    } else if (d.type == DIST_FOLD) {
        const double g = f(1 + f(d.drive * 4));
        for (int i = 0; i < size; i++)
            for (int c = 0; c < 2; c++) {
                const int x = fx[2 * i + c];
                const double input = f(x / F_32768);
                const double val = f(f(input * g) + d.p1);
                d.lp3[c] += coff * (val - d.lp3[c]);
                const double y = f(foldback(f(d.lp3[c]), 1) * d.p3);
                const int wet = ssat16(toI32(f(y * 32768)));
                out[2 * i + c] = (int16_t) qadd16(out[2 * i + c], dryWetMix(x, wet, dryWet));
            }
    } else {
        const double gain = f(1 + f(d.drive * 2304));
        for (int i = 0; i < size; i++)
            for (int c = 0; c < 2; c++) {
                const int x = fx[2 * i + c];
                double s = x / 32768.0;
                s += d.lastOut[c] * d.p1 * 0.125;
                d.hp1[c] += 0.64116641920131193922 * (s - d.hp1[c]);
                s = s - d.hp1[c];
                d.lp1[c] += (1 - 0.1025353241628911807) * (s - d.lp1[c]);
                s = d.lp1[c];
                d.hp3[c] += (1 - 0.99149556438154282898) * (s - d.hp3[c]);
                s = s - d.hp3[c];
                d.lp2[c] += (1 - 0.93130041642945205043) * (s - d.lp2[c]);
                s = d.lp2[c];
                s *= gain;
                double diff = s - d.slew[c];
                const double sign = diff > 0 ? 1 : -1;
                if (std::fabs(diff) > SLEW_MAX) diff = SLEW_MAX * sign;
                d.slew[c] += diff;
                if (d.slew[c] > 4.2) d.slew[c] = 4.2;
                else if (d.slew[c] < -4.2) d.slew[c] = -4.2;
                s = diode(d.slew[c]);
                d.lp3[c] += coff * (s - d.lp3[c]);
                d.lastOut[c] = d.lp3[c] * d.p3;
                const int wet = ssat16(toI32(d.lastOut[c] * 32768));
                out[2 * i + c] = (int16_t) qadd16(out[2 * i + c], dryWetMix(x, wet, dryWet));
            }
    }
}
static void ringmodProcess(Ringmod& rm, const int16_t* fx, int16_t* out, int size, double dryWet, Rng& rng) {
    calcNextOscSampleBlock(rm.osc, rm.buf, size, f(1), f(0.3), rng);
    for (int i = 0; i < size; i++) {
        const int m = rm.buf[i];
        for (int c = 0; c < 2; c++) {
            const int x = fx[2 * i + c];
            const int wet = wrap16((x * m) >> 16);
            out[2 * i + c] = (int16_t) qadd16(out[2 * i + c], dryWetMix(x, wet, dryWet));
        }
    }
}

static const double EXPF_SHIFT = 6755399441055744.0, EXPF_INVLN2N = 46.16624130844683;
static const double EXPF_C[3] = { 1.6938359250920212e-06, 0.00023459809789509004, 0.021660849396613134 };
static const double LOGF_LN2 = 0.6931471805599453;
static const double LOGF_A[3] = { -0.25089342214237154, 0.333456765744066, -0.4999997485802103 };
static double logf_(double x) {
    float xf = (float) x; uint32_t ix; std::memcpy(&ix, &xf, 4);
    if (ix == 0x3f800000u) return 0;
    const uint32_t tmp = ix - 0x3f330000u;
    const int i = (int) ((tmp >> 19) & 15);
    const int k = ((int32_t) tmp) >> 23;
    const uint32_t iz = ix - (tmp & 0xff800000u);
    float zf; std::memcpy(&zf, &iz, 4);
    const double z = zf;
    const double r = z * LOGF_INVC[i] - 1;
    const double y0 = LOGF_LOGC[i] + k * LOGF_LN2;
    const double r2 = r * r;
    double y = LOGF_A[1] * r + LOGF_A[2];
    y = LOGF_A[0] * r2 + y;
    y = y * r2 + (y0 + r);
    return f(y);
}
static double expf_(double x) {
    x = f(x);
    if (x < -103.97207641601562) return 0;
    const double z = EXPF_INVLN2N * x;
    const double kd = (z + EXPF_SHIFT) - EXPF_SHIFT;
    const double r = z - kd;
    const int32_t k = (int32_t) kd;
    const uint64_t bits = ((uint64_t) (EXP2F_TAB_HI[k & 31] + ((uint32_t) (k & 0x1ffff) << 15)) << 32) | EXP2F_TAB_LO[k & 31];
    double s; std::memcpy(&s, &bits, 8);
    const double zz = EXPF_C[0] * r + EXPF_C[1];
    const double r2 = r * r;
    double y = EXPF_C[2] * r + 1;
    y = zz * r2 + y;
    y = y * s;
    return f(y);
}
static const double LOG_2_DB = 8.6858896380650365530225783783321, DB_2_LOG = 0.11512925464970228420089957273422;
static void compressorProcess(Compressor& c, const int16_t* fx, int16_t* out, int size, double dryWet) {
    for (int i = 0; i < size; i++) {
        const int x0 = fx[2 * i], x1 = fx[2 * i + 1];
        const double inp = f(0.5 * f(x0 / F_32768) + 0.5 * f(x1 / F_32768));
        double peak = std::fabs(inp);
        peak += 1.0E-25;
        const double peakdB = logf_(f(peak)) * LOG_2_DB;
        double overdB = peakdB - c.tresh;
        if (overdB < 0) overdB = 0;
        const double coeff = overdB > c.env ? c.attack : c.release;
        c.env = overdB + coeff * (c.env - overdB);
        double gr = c.env * (c.ratio - 1.0);
        gr = expf_(f(gr * DB_2_LOG));
        const int l = ssat16(toI32(x0 * gr * c.markupGain));
        const int r = ssat16(toI32(x1 * gr * c.markupGain));
        out[2 * i] = (int16_t) qadd16(out[2 * i], dryWetMix(x0, l, dryWet));
        out[2 * i + 1] = (int16_t) qadd16(out[2 * i + 1], dryWetMix(x1, r, dryWet));
    }
}
static inline double compExpCoef(double ms) { return f(std::exp(-1000.0 / f(ms * F_REAL_FS))); }
static void compressorSetRatio(Compressor& c, int r) {
    static const double R[6] = { f(1), f(0.5), f(1 / 3.0), f(0.25), f(1 / 6.0), f(0.125) };
    c.ratio = r >= 0 && r < 6 ? R[r] : f(1);
}

static const int DELAY_NORMAL = 0;
static inline double interpF(double a, double b, double x) { return f(f(a * f(1 - x)) + f(b * x)); }
static double calcDelayTime(Delay& d, double cv) {
    const double mn = interpF(LOW_MIN, HI_MIN, d.range);
    const double mx = interpF(LOW_MAX, d.type == DELAY_NORMAL ? HI_MAX : PP_MAX, d.range);
    return interpF(mn, mx, cv);
}
static void delaySetDelayTime(Delay& d, double t) {
    if (d.interpValue <= 0) {
        d.delayTime = t;
        const double offset = d.hasSync ? d.sync : calcDelayTime(d, f(d.delayTime * d.range));
        const uint32_t offItg = toU32(offset);
        d.fracReadNew = f(offset - offItg);
        d.newRead = d.write - 1 - offItg;
        d.newRead &= d.type == DELAY_NORMAL ? d.mask : d.mask >> 1;
        d.interpValue = f(1);
        d.interpStep = f(1 / f(F_REAL_FS * f(0.01)));
    } else {
        d.pendingDelayTime = t;
        d.timePending = 1;
    }
}
static inline int16_t delayRead(const float* buf, uint32_t base, uint32_t pos, uint32_t mask, double frac) {
    const uint32_t pos2 = (pos + 1) & mask;
    return toI16(f(interpF(buf[base + pos], buf[base + pos2], frac) * F_32767));
}
static void delayCalcBuffer(Delay& d, const float* buf, int idx) {
    if (idx == 0) { d.tmp[0] = delayRead(buf, 0, d.oldRead, d.mask, d.fracReadOld); d.oldRead = (d.oldRead + 1) & d.mask; }
    else { d.tmp[1] = delayRead(buf, 0, d.newRead, d.mask, d.fracReadNew); d.newRead = (d.newRead + 1) & d.mask; }
}
static void delayCalcBufferPingPong(Delay& d, const float* buf, int idx) {
    const uint32_t half = d.mask >> 1;
    if (idx == 0) {
        d.tmp[0] = delayRead(buf, 0, d.oldRead, half, d.fracReadOld);
        d.tmp2[0] = delayRead(buf, half, d.oldRead, half, d.fracReadOld);
        d.oldRead = (d.oldRead + 1) & half;
    } else {
        d.tmp[1] = delayRead(buf, 0, d.newRead, half, d.fracReadNew);
        d.tmp2[1] = delayRead(buf, half, d.newRead, half, d.fracReadNew);
        d.newRead = (d.newRead + 1) & half;
    }
}
static inline int16_t crossfade(int a, int b, double v) { return toI16(f(a * v) + b * (1.0 - v)); }
static inline float toBuf(int itg) { double v = f(itg / F_32767); if (v > 1) v = f(1); else if (v < -1) v = f(-1); return (float) toHalf(v); }
static void delayStep(Delay& d, bool pingPong, const float* buf, int& l, int& r) {
    auto calc = pingPong ? delayCalcBufferPingPong : delayCalcBuffer;
    if (d.interpValue <= 0) {
        calc(d, buf, 0);
        l = d.tmp[0]; r = d.tmp2[0];
        if (d.timePending) { d.timePending = 0; delaySetDelayTime(d, d.pendingDelayTime); }
    } else {
        calc(d, buf, 0);
        calc(d, buf, 1);
        l = crossfade(d.tmp[0], d.tmp[1], d.interpValue);
        if (pingPong) r = crossfade(d.tmp2[0], d.tmp2[1], d.interpValue);
        d.interpValue = f(d.interpValue - d.interpStep);
        if (d.interpValue <= 0) { d.oldRead = d.newRead; d.fracReadOld = d.fracReadNew; }
    }
}
static void delayProcess(Delay& d, float* buf, const int16_t* fx, int16_t* out, int size, double dryWet) {
    for (int i = 0; i < size; i++) {
        const int x0 = fx[2 * i], x1 = fx[2 * i + 1];
        int o = 0, unused = 0;
        delayStep(d, false, buf, o, unused);
        o = toI16(calcOnePole(d.tone, o));
        const int itg = qadd16(qadd16(x0, x1), toI16(f(o * d.feedback)));
        buf[d.write++] = toBuf(itg);
        d.write &= d.mask;
        out[2 * i] = (int16_t) qadd16(out[2 * i], wrap16(ssat16(x0 + ssat16(toI32(f((o - x0) * dryWet))))));
        out[2 * i + 1] = (int16_t) qadd16(out[2 * i + 1], wrap16(ssat16(x1 + ssat16(toI32(f((o - x1) * dryWet))))));
    }
}
static void delayProcessPingPong(Delay& d, float* buf, const int16_t* fx, int16_t* out, int size, double dryWet) {
    const uint32_t half = d.mask >> 1;
    for (int i = 0; i < size; i++) {
        const int x0 = fx[2 * i], x1 = fx[2 * i + 1];
        int l = 0, r = 0;
        delayStep(d, true, buf, l, r);
        l = toI16(calcOnePole(d.tone, l));
        r = toI16(calcOnePole(d.tone2, r));
        buf[half + d.write] = toBuf(qadd16(x0, toI16(f(l * d.feedback))));
        buf[d.write++] = toBuf(qadd16(x1, toI16(f(r * d.feedback))));
        d.write &= half;
        out[2 * i] = (int16_t) qadd16(out[2 * i], wrap16(ssat16(x0 + ssat16(toI32(f((l - x0) * dryWet))))));
        out[2 * i + 1] = (int16_t) qadd16(out[2 * i + 1], wrap16(ssat16(x1 + ssat16(toI32(f((r - x1) * dryWet))))));
    }
}

FxBus::FxBus() : buffer(SYNC_BUFFER_SIZE + 1, 0.0f) {
    distortion.drive = f(1); distortion.p1 = f(1);
    Osc& o = ringmod.osc;
    o.freq = f(100); o.modNodeValue = f(1); o.waveform = REC; o.fmMod = 0; o.midiFreq = 70 << 8 | 64; o.pitchMod = f(1);
    osc_setFreq(o);
    compressor.tresh = f(3); compressor.attack = f(0.1); compressor.release = f(300); compressor.ratio = 0; compressor.markupGain = f(1);
    delay.mask = FX_BUFFER_SIZE; delay.write = FX_BUFFER_SIZE / 20 - 10; delay.feedback = f(0.7);
    delay.tone.r = f(0.01); delay.tone2.r = f(0.01);
}
void FxBus::setDelaySync(bool on, double samples) {
    Delay& d = delay;
    if (!on) { if (!d.hasSync) return; d.hasSync = false; d.mask = FX_BUFFER_SIZE; }
    else {
        d.mask = SYNC_BUFFER_SIZE;
        const double mx = (double) ((d.type == DELAY_NORMAL ? d.mask : d.mask >> 1) - 2);
        samples = std::max(1.0, std::min(mx, samples));
        if (d.hasSync && std::fabs(samples - d.sync) < 0.5) return;
        d.hasSync = true; d.sync = samples;
    }
    delaySetDelayTime(d, d.delayTime);
}
void FxBus::updateParameter() {
    const float* p = parameter;
    switch (fxType) {
        case FX_DISTORTION: distortion.drive = p[0]; distortion.p1 = p[1]; distortion.p2 = p[2]; distortion.p3 = p[3]; break;
        case FX_RING: ringmod.osc.freq = f(p[0] * 880.0); osc_setFreq(ringmod.osc); break;
        case FX_COMPRESSOR: {
            Compressor& c = compressor;
            c.attack = compExpCoef(f(f(0.03) + f(p[0] * 100.0)));
            c.release = compExpCoef(f(25 + f(p[1] * 175.0)));
            c.tresh = f(-96 + f(p[2] * 96.0));
            c.markupGain = f(1 + f(p[3] * 2.0));
            break;
        }
        case FX_DELAY: {
            Delay& d = delay;
            d.range = p[1]; delaySetDelayTime(d, d.delayTime);
            delaySetDelayTime(d, p[0]);
            d.tone.r = f((double) p[2] * p[2]);
            d.tone2.r = f((double) p[2] * p[2]);
            d.feedback = p[3];
            break;
        }
        default: break;
    }
}
void FxBus::process(int16_t* out, int size, Rng& rng) {
    const int16_t* fx = input; const double dw = forceWet ? 1 : dryWet;
    switch (fxType) {
        case FX_DISTORTION: distortionProcess(distortion, fx, out, size, dw); break;
        case FX_RING: ringmodProcess(ringmod, fx, out, size, dw, rng); break;
        case FX_COMPRESSOR: compressorProcess(compressor, fx, out, size, dw); break;
        case FX_DELAY:
            if (delay.type == DELAY_NORMAL) delayProcess(delay, buffer.data(), fx, out, size, dw);
            else delayProcessPingPong(delay, buffer.data(), fx, out, size, dw);
            break;
        default:
            for (int i = 0; i < size * 2; i++) out[i] = (int16_t) qadd16(out[i], fx[i]);
            break;
    }
}

// ---------------------------------------------------------------------------------------
// The engine
// ---------------------------------------------------------------------------------------
Engine::Engine(uint32_t seed) : rng(seed) {
    for (int i = 0; i < 3; i++) makeDrumVoice(drums[i], i);
    makeSnare(snare); makeMetal(hat); makeMetal(cymbal);
    Voice* v[6] = { &drums[0], &drums[1], &drums[2], &snare, &cymbal, &hat };
    for (int i = 0; i < 6; i++) { voices[i] = v[i]; lfos[i] = &v[i]->lfo; xOscs[i] = &v[i]->osc; }
}

void Engine::setCrossMod(const float* amounts, const XTarget* targets) {
    bool any = false;
    if (amounts) for (int i = 0; i < 36; i++) if (amounts[i] != 0) any = true;
    if (!any) { xmod.on = false; for (auto* o : xOscs) o->pm = nullptr; return; }
    if (!xmod.on) {
        xmod.on = true;
        std::memset(xmod.prev, 0, sizeof(xmod.prev)); std::memset(xmod.env, 0, sizeof(xmod.env)); std::memset(xmod.pm, 0, sizeof(xmod.pm));
    }
    std::memcpy(xmod.amt, amounts, sizeof(xmod.amt));
    std::memcpy(xmod.target, targets, sizeof(xmod.target));
}
void Engine::xmodBefore() {
    auto& x = xmod;
    for (int n = 0; n < 6; n++) {
        int pk = 0; for (int i = 0; i < BLOCK; i++) { const int a = x.prev[n][i] < 0 ? -x.prev[n][i] : x.prev[n][i]; if (a > pk) pk = a; }
        x.env[n] = (float) std::max(pk / 32768.0, (double) x.env[n] * 0.955);
    }
    for (int d = 0; d < 6; d++) {
        const XTarget t = x.target[d];
        bool any = false; for (int s = 0; s < 6; s++) if (x.amt[s * 6 + d] != 0) any = true;
        xOscs[d]->pm = nullptr;
        if (!any) continue;
        if (t == X_FM) {
            uint32_t* pm = x.pm[d];
            for (int i = 0; i < BLOCK; i++) {
                double c = 0;
                for (int s = 0; s < 6; s++) { const double a = x.amt[s * 6 + d]; if (a != 0) c += x.prev[s][i] / 32768.0 * a * 1.5; }
                pm[i] = (uint32_t) (int64_t) std::trunc((c - std::floor(c)) * 4294967296.0);
            }
            xOscs[d]->pm = pm;
        } else if (t == X_FILTER) {
            double oct = 0; for (int s = 0; s < 6; s++) oct += (double) x.amt[s * 6 + d] * x.env[s] * 4;
            Filter& fl = voices[d]->filter;
            const double fc = std::min(0.449, std::max(0.0003, fl.f * std::pow(2.0, oct)));
            fl.g = fastTan(PI * fc);
        }
    }
}
void Engine::xmodAfter() {
    auto& x = xmod;
    for (int d = 0; d < 6; d++) {
        if (x.target[d] != X_RING) continue;
        double sum = 0; for (int s = 0; s < 6; s++) sum += std::fabs((double) x.amt[s * 6 + d]);
        if (sum == 0) continue;
        const double dry = std::max(0.0, 1 - sum); int16_t* buf = out[d];
        for (int i = 0; i < BLOCK; i++) {
            double m = 0; for (int s = 0; s < 6; s++) { const double a = x.amt[s * 6 + d]; if (a != 0) m += x.prev[s][i] / 32768.0 * a * 2; }
            buf[i] = (int16_t) ssat16d(jsRound(buf[i] * (dry + m)));
        }
    }
    std::memcpy(x.prev, out, sizeof(x.prev));
}

void Engine::loadInitKit() {
    uint8_t kit[PAR::END_OF_SOUND_PARAMETERS] {};
    for (int i = 8; i < PAR::END_OF_SOUND_PARAMETERS; i++) kit[i - 8] = INIT_KIT[i];
    loadKit(kit, PAR::END_OF_SOUND_PARAMETERS);
}
void Engine::loadKit(const uint8_t* values, int n) {
    for (int i = 0; i < n && i < PAR::END_OF_SOUND_PARAMETERS; i++) activeParameters[i] = values[i];
    for (int i = 0; i < 6; i++) {
        setVelocityDestination(i, targetPar(activeParameters[PAR::VEL_DEST_1 + i]));
        setLfoDestination(i, targetPar(activeParameters[PAR::TARGET_LFO1 + i]));
    }
    for (int i = 0; i < PAR::END_OF_SOUND_PARAMETERS; i++) {
        const int p = PARAMETER_ORDER[i];
        setParam(p, activeParameters[p]);
    }
}
void Engine::setParam(int par, int value) {
    if (par < 0 || par >= PAR::NUM_PARAMS) return;
    activeParameters[par] = (uint8_t) value;
    if (par == 0) return;
    if (par >= PAR::VEL_DEST_1 && par <= PAR::VEL_DEST_6) setVelocityDestination(par - PAR::VEL_DEST_1, targetPar(value));
    if (par >= PAR::TARGET_LFO1 && par <= PAR::TARGET_LFO6) setLfoDestination(par - PAR::TARGET_LFO1, targetPar(value));
    dispatch(par, value, false, 0);
}
void Engine::dispatch(int par, int data2, bool isFloat, double v) {
    if (par < 128) ccHandler(false, (par + 1) & 0x7f, data2, isFloat, v);
    else ccHandler(true, (par - 128) & 0x7f, data2, isFloat, v);
}
int Engine::targetPar(int index) {
    const int n = (int) (sizeof(MOD_TARGET_PAR) / sizeof(MOD_TARGET_PAR[0]));
    return index >= 0 && index < n ? MOD_TARGET_PAR[index] : MOD_TARGET_PAR[0];
}
void Engine::trigger(int voice, int velocity, int note) {
    if (voice < 3) drumTrigger(voice, velocity, note);
    else if (voice < 4) snareTrigger(velocity, note);
    else if (voice < 5) cymbalTrigger(velocity, note);
    else hatTrigger(velocity, voice - 5, note);
}

void Engine::modUpdate(ModNode& vm, double val) {
    vm.lastVal = val;
    if (vm.destination < 0 || vm.destination >= PAR::NUM_PARAMS || !MODULATABLE[vm.destination]) return;
    const double original = f(activeParameters[vm.destination] / 127.0);
    double nv = f(original + f(vm.amount * f(f(val * 2) - 1)));
    nv = nv < 0 ? 0 : nv > 1 ? 1 : nv;
    dispatch(vm.destination, 0, true, nv);
}
void Engine::setOriginal(int par) {
    if (par < 0 || par >= PAR::NUM_PARAMS || !MODULATABLE[par]) return;
    dispatch(par, activeParameters[par], false, 0);
}
void Engine::resetTargets() {
    for (auto& vm : velocityModulators) setOriginal(vm.destination);
    for (auto* lfo : lfos) setOriginal(lfo->modTarget.destination);
}
void Engine::lfoRetrigger(int voice) { for (auto* lfo : lfos) if (lfo->retrigger == voice + 1) lfo->phase = lfo->phaseOffset; }
void Engine::lfoSetFreq(Lfo& lfo, double x) {
    x = f(f(0.0078125) + f(x * f(1 - f(0.0078125))));
    x = f(f(x * x) * x);
    lfo.freq = f(x * LFO_MAX_F);
    lfo.phaseInc = lfo_calcPhaseInc(lfo.freq, lfo.sync, bpm);
}

void Engine::drumTrigger(int n, int vel, int note) {
    DrumVoice& v = drums[n];
    lfoRetrigger(n);
    modUpdate(velocityModulators[n], f(vel / 127.0));
    double offset = 1;
    if (v.transGen.waveform == 1) offset = f(offset - v.transGen.volume);
    resetPhase(v.osc, offset, true);
    resetPhase(v.modOsc, 1, true);
    osc_setBaseNote(v.osc, note);
    osc_setBaseNote(v.modOsc, note);
    v.oscPitchEg.value = 1;
    slopeEg2_trigger(v.oscVolEg);
    v.velo = f(vel / 127.0);
    v.transGen.phase = 0;
    v.snapEg.value = 1;
    SVF_reset(v.filter);
    updatePan(v);
}
void Engine::snareTrigger(int vel, int note) {
    Snare& v = snare;
    lfoRetrigger(3);
    modUpdate(velocityModulators[3], f(vel / 127.0));
    double offset = 1;
    if (v.transGen.waveform == 1) offset = f(offset - v.transGen.volume);
    resetPhase(v.osc, offset, false);
    v.oscPitchEg.value = 1;
    slopeEg2_trigger(v.oscVolEg);
    v.velo = f(vel / 127.0);
    osc_setBaseNote(v.osc, note);
    v.transGen.phase = 0;
    v.snapEg.value = 1;
    updatePan(v);
}
void Engine::metalTrigger(Metal& v, int lfoVoice, int vel, int note) {
    lfoRetrigger(lfoVoice);
    modUpdate(velocityModulators[lfoVoice], f(vel / 127.0));
    double offset = 1;
    if (v.transGen.waveform == 1) offset = f(offset - v.transGen.volume);
    resetPhase(v.osc, offset, false);
    v.modOsc.phase = 0;
    v.modOsc2.phase = 0;
    osc_setBaseNote(v.osc, note);
    osc_setBaseNote(v.modOsc, note);
    osc_setBaseNote(v.modOsc2, note);
}
void Engine::cymbalTrigger(int vel, int note) {
    Metal& v = cymbal;
    metalTrigger(v, 4, vel, note);
    slopeEg2_trigger(v.oscVolEg);
    v.velo = f(vel / 127.0);
    v.transGen.phase = 0;
    v.snapEg.value = 1;
    updatePan(v);
}
void Engine::hatTrigger(int vel, int isOpen, int note) {
    Metal& v = hat;
    metalTrigger(v, 5, vel, note);
    v.isOpen = isOpen;
    v.oscVolEg.decay = isOpen ? v.decayOpen : v.decayClosed;
    slopeEg2_trigger(v.oscVolEg);
    v.velo = f(vel / 127.0);
    v.transGen.phase = 0;
    v.snapEg.value = 1;
    updatePan(v);
}

void Engine::drumAsync(DrumVoice& v) {
    const double egPitch = decayEg_calc(v.oscPitchEg);
    v.osc.pitchMod = f(1 + f(egPitch * v.egPitchModAmount));
    if (v.transGen.waveform == 0) {
        const double snap = snapEg_calc(v.snapEg, v.transGen.pitch);
        v.osc.pitchMod = f(v.osc.pitchMod + f(snap * v.transGen.volume));
    }
    v.osc.fmMod = f(v.fmModAmount * egPitch);
    SlopeEg& eg = v.oscVolEg;
    if (eg.attack == 1 && (eg.state == EG_A || eg.state == EG_REPEAT)) {
        v.ampFilterInput = slopeEg2_calc(eg);
        v.lastGain = v.ampFilterInput;
    } else {
        v.lastGain = v.ampFilterInput;
        v.ampFilterInput = slopeEg2_calc(eg);
    }
    osc_setFreq(v.osc);
    osc_setFreq(v.modOsc);
}
void Engine::snareAsync(Snare& v) {
    const double egPitch = decayEg_calc(v.oscPitchEg);
    v.osc.pitchMod = f(1 + f(egPitch * v.egPitchModAmount));
    v.egValueOscVol = f(slopeEg2_calc(v.oscVolEg));
    if (v.transGen.waveform == 0) {
        const double snap = snapEg_calc(v.snapEg, v.transGen.pitch);
        v.osc.pitchMod = f(v.osc.pitchMod + f(snap * v.transGen.volume));
    }
    osc_setFreq(v.osc);
    osc_setFreq(v.noiseOsc);
}
void Engine::metalAsync(Metal& v) {
    v.egValueOscVol = f(slopeEg2_calc(v.oscVolEg));
    if (v.transGen.waveform == 0) {
        const double snap = snapEg_calc(v.snapEg, v.transGen.pitch);
        v.osc.pitchMod = f(1 + f(snap * v.transGen.volume));
    }
    osc_setFreq(v.osc);
    osc_setFreq(v.modOsc);
    osc_setFreq(v.modOsc2);
}

void Engine::drumSync(int n, int16_t* buf) {
    DrumVoice& v = drums[n]; int16_t* mod = modBuf; const int size = BLOCK;
    calcNextOscSampleBlock(v.modOsc, mod, size, v.fmModAmount, v.pwm, rng);
    if (v.mixOscs) {
        calcNextOscSampleBlock(v.osc, buf, size, f(1 - v.fmModAmount), v.pwm, rng);
        for (int i = 0; i < size; i++) buf[i] = (int16_t) qadd16(buf[i], mod[i]);
    } else {
        calcNextOscSampleFmBlock(v.osc, mod, buf, size, 1, v.pwm, rng);
    }
    transient_calcBlock(v.transGen, mod, size);
    for (int i = 0; i < size; i++) buf[i] = (int16_t) qadd16(buf[i], mod[i]);
    SVF_calcBlockZDF(v.filter, v.filterType, buf, size);
    const double gain = f(v.ampFilterInput), last = f(v.lastGain), d = f(gain - last);
    for (int i = 0; i < size; i++) buf[i] = toI16(buf[i] * (last + f(i / f(size - 1)) * d));
    if (v.volumeMod) for (int i = 0; i < size; i++) buf[i] = toI16(f(buf[i] * v.velo));
    for (int i = 0; i < size; i++) buf[i] = distortSample(buf[i], calcOnePole(v.distortion.driveFilter, v.distortion.shape));
    for (int i = 0; i < size; i++) {
        const double mixVol = calcOnePole(v.mixerVolFilter, v.vol);
        v.combinedVolume = f(mixVol * calcOnePole(v.faderFilter, faderVolume[n]));
        buf[i] = toI16(f(buf[i] * v.combinedVolume));
    }
}
void Engine::snareSync(int16_t* buf) {
    Snare& v = snare; int16_t* osc = buf2; int16_t* trans = buf3; const int size = BLOCK;
    calcNoiseBlock(v.noiseOsc, buf, size, f(0.9), rng);
    SVF_calcBlockZDF(v.filter, v.filterType, buf, size);
    calcNextOscSampleBlock(v.osc, osc, size, f(1 - v.mix), v.pwm, rng);
    transient_calcBlock(v.transGen, trans, size);
    v.combinedVolume = v.volumeMod ? f(f(v.vol * v.velo) * v.egValueOscVol) : f(v.vol * v.egValueOscVol);
    for (int j = 0; j < size; j++) {
        double tmp = fmaf_(buf[j], v.mix, osc[j]);
        tmp = f(tmp + trans[j]);
        buf[j] = toI16(f(f(v.combinedVolume * tmp) * calcOnePole(v.faderFilter, faderVolume[3])));
    }
    for (int j = 0; j < size; j++) buf[j] = distortSample(buf[j], v.distortionShape);
}
void Engine::metalSync(Metal& v, double fmGain, int faderIdx, int16_t* buf) {
    int16_t* mod = buf2; int16_t* mod2 = buf3; const int size = BLOCK;
    calcNextOscSampleBlock(v.modOsc, mod, size, v.fmModAmount1, v.pwm, rng);
    calcNextOscSampleBlock(v.modOsc2, mod2, size, v.fmModAmount2, v.pwm, rng);
    for (int i = 0; i < size; i++) mod[i] = (int16_t) qadd16(mod[i], mod2[i]);
    calcNextOscSampleFmBlock(v.osc, mod, buf, size, fmGain, v.pwm, rng);
    SVF_calcBlockZDF(v.filter, v.filterType, buf, size);
    transient_calcBlock(v.transGen, mod, size);
    v.combinedVolume = v.volumeMod ? f(f(v.vol * v.velo) * v.egValueOscVol) : f(v.vol * v.egValueOscVol);
    for (int i = 0; i < size; i++) buf[i] = (int16_t) qadd16(buf[i], mod[i]);
    for (int j = 0; j < size; j++)
        buf[j] = toI16(f(buf[j] * f(v.combinedVolume * calcOnePole(v.faderFilter, faderVolume[faderIdx]))));
    for (int j = 0; j < size; j++) buf[j] = distortSample(buf[j], v.distortionShape);
}
void Engine::decimate(int n, int16_t* buf) {
    const double inc = f((double) decimationRate[n] * decimationRate[6]);
    for (int i = 0; i < BLOCK; i++) {
        decimationCnt[n] = (float) ((double) decimationCnt[n] + inc);
        if (decimationCnt[n] >= 1) {
            decimationCnt[n] = (float) ((double) decimationCnt[n] - 1);
            voiceSamples[n] = buf[i];
        }
        buf[i] = voiceSamples[n];
    }
}

void Engine::renderBlock() {
    resetTargets();
    for (auto& vm : velocityModulators) modUpdate(vm, vm.lastVal);
    for (auto* lfo : lfos) modUpdate(lfo->modTarget, lfo_calc(*lfo, rng));
    for (auto* v : voices) SVF_recalcFreq(v->filter);
    if (xmod.on) xmodBefore();
    for (auto& d : drums) drumAsync(d);
    snareAsync(snare);
    metalAsync(cymbal);
    metalAsync(hat);
    for (int n = 0; n < 3; n++) { drumSync(n, out[n]); decimate(n, out[n]); }
    snareSync(out[3]); decimate(3, out[3]);
    metalSync(cymbal, 1, 4, out[4]); decimate(4, out[4]);
    metalSync(hat, f(0.5), 5, out[5]); decimate(5, out[5]);
    if (xmod.on) xmodAfter();
}

const int16_t* Engine::renderFx(const float* gains, double width) {
    int16_t* inp = fx.input;
    std::memset(inp, 0, sizeof(fx.input)); std::memset(fxOut, 0, sizeof(fxOut));
    for (int n = 0; n < 6; n++) {
        if (audioRouting[n] != MIXER_ROUTING_FX_BUS) continue;
        const Voice& v = *voices[n]; const int16_t* o = out[n]; const double g = gains ? gains[n] : 1;
        for (int i = 0; i < BLOCK; i++) {
            const int s = g == 1 ? o[i] : toI16(f(o[i] * g));
            inp[2 * i] = (int16_t) qadd16(toI16(f(s * v.triggeredPanL)), inp[2 * i]);
            inp[2 * i + 1] = (int16_t) qadd16(toI16(f(s * v.triggeredPanR)), inp[2 * i + 1]);
        }
    }
    const bool dl = width >= 0 && width != 1 && fx.fxType == FX_DELAY, pp = dl && fx.delay.type != DELAY_NORMAL;
    if (pp) for (int i = 0; i < BLOCK; i++) {
        const double l = inp[2 * i], r = inp[2 * i + 1], mid = (l + r) / 2, side = (l - r) / 2;
        const double nl = width <= 1 ? mid + side * width : l + mid * (width - 1), nr = width <= 1 ? mid - side * width : r - mid * (width - 1);
        inp[2 * i] = (int16_t) ssat16d(jsRound(nl)); inp[2 * i + 1] = (int16_t) ssat16d(jsRound(nr));
    }
    fx.process(fxOut, BLOCK, rng);
    if (dl && !pp) for (int i = 0; i < BLOCK; i++) {
        const double mid = (fxOut[2 * i] + fxOut[2 * i + 1]) / 2.0, side = (fxOut[2 * i] - fxOut[2 * i + 1]) / 2.0 * width;
        fxOut[2 * i] = (int16_t) ssat16d(jsRound(mid + side)); fxOut[2 * i + 1] = (int16_t) ssat16d(jsRound(mid - side));
    }
    return fxOut;
}

// ---------------------------------------------------------------------------------------
// midiParser_ccHandler()
// ---------------------------------------------------------------------------------------
void Engine::ccHandler(bool isCC2, int num, int data2, bool isFloat, double v) {
    int itg; double flt;
    if (isFloat) { itg = toU8(f(v * 127)); flt = f(v); }
    else { itg = data2 & 0xff; flt = f(itg / 127.0); }
    DrumVoice &d0 = drums[0], &d1 = drums[1], &d2 = drums[2]; Snare& sn = snare; Metal &cy = cymbal, &hh = hat;
    auto wave = [&](int count) { if (!isFloat) return itg; const int w = toU8(f(count * flt)); return w >= count ? count - 1 : w; };
    auto pitchModAmount = [](int x) { const double val = f(x / 127.0); return f(f(val * val) * 32); };
    auto filterValue = [&]() { return valueShaperF2F(flt, FILTER_SHAPER); };
    auto transPitch = [&]() { return f(1 + f(f(itg / f(33.9)) - f(0.75))); };

    if (!isCC2) {
        switch (num) {
            case CC::NRPN_DATA_ENTRY_COARSE: ccHandler(true, activeNrpnNumber & 0xff, itg, false, 0); return;
            case CC::NRPN_FINE: activeNrpnNumber = (activeNrpnNumber & ~0x7f) | (itg & 0x7f); break;
            case CC::NRPN_COARSE: activeNrpnNumber = ((activeNrpnNumber & 0x7f) | (itg << 7)) & 0xffff; break;
            case CC::VOL_SLOPE1: slopeEg2_setSlope(d0.oscVolEg, itg); break;
            case CC::PITCH_SLOPE1: decayEg_setSlope(d0.oscPitchEg, itg); break;
            case CC::VOL_SLOPE2: slopeEg2_setSlope(d1.oscVolEg, itg); break;
            case CC::PITCH_SLOPE2: decayEg_setSlope(d1.oscPitchEg, itg); break;
            case CC::VOL_SLOPE3: slopeEg2_setSlope(d2.oscVolEg, itg); break;
            case CC::PITCH_SLOPE3: decayEg_setSlope(d2.oscPitchEg, itg); break;
            case CC::PITCH_SLOPE4: decayEg_setSlope(sn.oscPitchEg, itg); break;
            case CC::VOL_SLOPE6: slopeEg2_setSlope(hh.oscVolEg, itg); break;
            case CC::FILTER_FREQ_DRUM1: case CC::FILTER_FREQ_DRUM2: case CC::FILTER_FREQ_DRUM3:
                SVF_directSetFilterValue(drums[num - CC::FILTER_FREQ_DRUM1].filter, filterValue()); break;
            case CC::RESO_DRUM1: case CC::RESO_DRUM2: case CC::RESO_DRUM3:
                SVF_setReso(drums[num - CC::RESO_DRUM1].filter, flt); break;
            case CC::F_OSC1_COARSE: setCoarse(d0.osc, itg); break;
            case CC::F_OSC2_COARSE: setCoarse(d1.osc, itg); break;
            case CC::F_OSC3_COARSE: setCoarse(d2.osc, itg); break;
            case CC::F_OSC4_COARSE: setCoarse(sn.osc, itg); break;
            case CC::F_OSC5_COARSE: setCoarse(cy.osc, itg); break;
            case CC::F_OSC1_FINE: setFine(d0.osc, itg); break;
            case CC::F_OSC2_FINE: setFine(d1.osc, itg); break;
            case CC::F_OSC3_FINE: setFine(d2.osc, itg); break;
            case CC::F_OSC4_FINE: setFine(sn.osc, itg); break;
            case CC::F_OSC5_FINE: setFine(cy.osc, itg); break;
            case CC::F_OSC6_FINE: setFine(hh.osc, itg); break;
            case CC::OSC_WAVE_DRUM1: d0.osc.waveform = wave(NUM_OSC_WAVEFORMS); break;
            case CC::MOD_WAVE_DRUM1: d0.modOsc.waveform = wave(NUM_OSC_WAVEFORMS); break;
            case CC::OSC_WAVE_DRUM2: d1.osc.waveform = wave(NUM_OSC_WAVEFORMS); break;
            case CC::MOD_WAVE_DRUM2: d1.modOsc.waveform = wave(NUM_OSC_WAVEFORMS); break;
            case CC::OSC_WAVE_DRUM3: d2.osc.waveform = wave(NUM_OSC_WAVEFORMS); break;
            case CC::MOD_WAVE_DRUM3: d2.modOsc.waveform = wave(NUM_OSC_WAVEFORMS); break;
            case CC::OSC_WAVE_SNARE: sn.osc.waveform = wave(NUM_OSC_WAVEFORMS); break;
            case CC::OSC1_DIST: d0.distortion.shape = distShape(flt); break;
            case CC::OSC2_DIST: d1.distortion.shape = distShape(flt); break;
            case CC::OSC3_DIST: d2.distortion.shape = distShape(flt); break;
            case CC::VELOA1: d0.oscVolEg.attack = slopeEg2_calcTime(itg, TIME_K_ATTACK); break;
            case CC::VELOD1: d0.oscVolEg.decay = slopeEg2_calcTime(itg, TIME_K_DECAY); break;
            case CC::PITCHD1: d0.oscPitchEg.decay = decayEg_calcTime(itg); break;
            case CC::MODAMNT1: d0.egPitchModAmount = pitchModAmount(itg); break;
            case CC::FMAMNT1: d0.fmModAmount = flt; break;
            case CC::FMDTN1: setCoarse(d0.modOsc, itg); break;
            case CC::VOL1: d0.vol = flt; break;
            case CC::PAN1: d0.pan = itg; break;
            case CC::VELOA2: d1.oscVolEg.attack = slopeEg2_calcTime(itg, TIME_K_ATTACK); break;
            case CC::VELOD2: d1.oscVolEg.decay = slopeEg2_calcTime(itg, TIME_K_DECAY); break;
            case CC::PITCHD2: d1.oscPitchEg.decay = decayEg_calcTime(itg); break;
            case CC::MODAMNT2: d1.egPitchModAmount = pitchModAmount(itg); break;
            case CC::FMAMNT2: d1.fmModAmount = flt; break;
            case CC::FMDTN2: setCoarse(d1.modOsc, itg); break;
            case CC::VOL2: d1.vol = flt; break;
            case CC::PAN2: d1.pan = itg; break;
            case CC::VELOA3: d2.oscVolEg.attack = slopeEg2_calcTime(itg, TIME_K_ATTACK); break;
            case CC::VELOD3: d2.oscVolEg.decay = slopeEg2_calcTime(itg, TIME_K_DECAY); break;
            case CC::PITCHD3: d2.oscPitchEg.decay = decayEg_calcTime(itg); break;
            case CC::MODAMNT3: d2.egPitchModAmount = pitchModAmount(itg); break;
            case CC::FMAMNT3: d2.fmModAmount = flt; break;
            case CC::FMDTN3: setCoarse(d2.modOsc, itg); break;
            case CC::VOL3: d2.vol = flt; break;
            case CC::PAN3: d2.pan = itg; break;
            case CC::VOL4: sn.vol = flt; break;
            case CC::PAN4: sn.pan = itg; break;
            case CC::SNARE_NOISE_F: sn.noiseOsc.freq = f(flt * 22000); break;
            case CC::VELOA4: sn.oscVolEg.attack = slopeEg2_calcTime(itg, TIME_K_ATTACK); break;
            case CC::VELOD4: sn.oscVolEg.decay = slopeEg2_calcTime(itg, TIME_K_DECAY); break;
            case CC::PITCHD4: sn.oscPitchEg.decay = decayEg_calcTime(itg); break;
            case CC::MODAMNT4: sn.egPitchModAmount = pitchModAmount(itg); break;
            case CC::SNARE_FILTER_F: SVF_directSetFilterValue(sn.filter, filterValue()); break;
            case CC::SNARE_RESO: SVF_setReso(sn.filter, flt); break;
            case CC::SNARE_MIX: sn.mix = flt; break;
            case CC::VOL5: cy.vol = flt; break;
            case CC::PAN5: cy.pan = itg; break;
            case CC::VELOA5: cy.oscVolEg.attack = slopeEg2_calcTime(itg, TIME_K_ATTACK); break;
            case CC::VELOD5: cy.oscVolEg.decay = slopeEg2_calcTime(itg, TIME_K_DECAY); break;
            case CC::CYM_WAVE1: cy.osc.waveform = wave(NUM_OSC_WAVEFORMS); break;
            case CC::CYM_WAVE2: cy.modOsc.waveform = wave(NUM_OSC_WAVEFORMS); break;
            case CC::CYM_WAVE3: cy.modOsc2.waveform = wave(NUM_OSC_WAVEFORMS); break;
            case CC::CYM_MOD_OSC_F1: setCoarse(cy.modOsc, itg); break;
            case CC::CYM_MOD_OSC_F2: setCoarse(cy.modOsc2, itg); break;
            case CC::CYM_MOD_OSC_GAIN1: cy.fmModAmount1 = flt; break;
            case CC::CYM_MOD_OSC_GAIN2: cy.fmModAmount2 = flt; break;
            case CC::CYM_FIL_FREQ: SVF_directSetFilterValue(cy.filter, filterValue()); break;
            case CC::CYM_RESO: SVF_setReso(cy.filter, flt); break;
            case CC::CYM_REPEAT: cy.oscVolEg.repeat = itg; break;
            case CC::CYM_SLOPE: slopeEg2_setSlope(cy.oscVolEg, itg); break;
            case CC::WAVE1_HH: hh.osc.waveform = wave(NUM_OSC_WAVEFORMS); break;
            case CC::WAVE2_HH: hh.modOsc.waveform = wave(NUM_OSC_WAVEFORMS); break;
            case CC::WAVE3_HH: hh.modOsc2.waveform = wave(NUM_OSC_WAVEFORMS); break;
            case CC::VELOD6: hh.decayClosed = f(slopeEg2_calcTime(itg, TIME_K_DECAY)); break;
            case CC::VELOD6_OPEN: hh.decayOpen = f(slopeEg2_calcTime(itg, TIME_K_DECAY)); break;
            case CC::HAT_FILTER_F: SVF_directSetFilterValue(hh.filter, filterValue()); break;
            case CC::HAT_RESO: SVF_setReso(hh.filter, flt); break;
            case CC::VOL6: hh.vol = flt; break;
            case CC::PAN6: hh.pan = itg; break;
            case CC::F_OSC6_COARSE: setCoarse(hh.osc, itg); break;
            case CC::MOD_OSC_F1: setCoarse(hh.modOsc, itg); break;
            case CC::MOD_OSC_F2: setCoarse(hh.modOsc2, itg); break;
            case CC::MOD_OSC_GAIN1: hh.fmModAmount1 = flt; break;
            case CC::MOD_OSC_GAIN2: hh.fmModAmount2 = flt; break;
            case CC::VELOA6: hh.oscVolEg.attack = slopeEg2_calcTime(itg, TIME_K_ATTACK); break;
            case CC::REPEAT1: sn.oscVolEg.repeat = itg; break;
            case CC::EG_SNARE1_SLOPE: slopeEg2_setSlope(sn.oscVolEg, itg); break;
            case CC::SNARE_DISTORTION: sn.distortionShape = distShape(flt); break;
            case CC::CYMBAL_DISTORTION: cy.distortionShape = distShape(flt); break;
            case CC::HAT_DISTORTION: hh.distortionShape = distShape(flt); break;
            case CC::FREQ_LFO1: case CC::FREQ_LFO2: case CC::FREQ_LFO3: case CC::FREQ_LFO4: case CC::FREQ_LFO5: case CC::FREQ_LFO6:
                lfoSetFreq(*lfos[num - CC::FREQ_LFO1], flt); break;
            case CC::AMOUNT_LFO1: case CC::AMOUNT_LFO2: case CC::AMOUNT_LFO3: case CC::AMOUNT_LFO4: case CC::AMOUNT_LFO5: case CC::AMOUNT_LFO6:
                lfos[num - CC::AMOUNT_LFO1]->modTarget.amount = flt; break;
            case CC::VOICE_DECIMATION1: case CC::VOICE_DECIMATION2: case CC::VOICE_DECIMATION3: case CC::VOICE_DECIMATION4:
            case CC::VOICE_DECIMATION5: case CC::VOICE_DECIMATION6: case CC::VOICE_DECIMATION_ALL:
                decimationRate[num - CC::VOICE_DECIMATION1] = (float) valueShaperF2F(flt, f(-0.7)); break;
            default: break;
        }
        return;
    }

    switch (num) {
        case CC2::TRANS1_WAVE: case CC2::TRANS2_WAVE: case CC2::TRANS3_WAVE:
            transient_setWaveform(drums[num - CC2::TRANS1_WAVE].transGen, wave(NUM_WAVES)); break;
        case CC2::TRANS4_WAVE: transient_setWaveform(sn.transGen, wave(NUM_WAVES)); break;
        case CC2::TRANS5_WAVE: transient_setWaveform(cy.transGen, wave(NUM_WAVES)); break;
        case CC2::TRANS6_WAVE: transient_setWaveform(hh.transGen, wave(NUM_WAVES)); break;
        case CC2::TRANS1_VOL: case CC2::TRANS2_VOL: case CC2::TRANS3_VOL: drums[num - CC2::TRANS1_VOL].transGen.volume = flt; break;
        case CC2::TRANS4_VOL: sn.transGen.volume = flt; break;
        case CC2::TRANS5_VOL: cy.transGen.volume = flt; break;
        case CC2::TRANS6_VOL: hh.transGen.volume = flt; break;
        case CC2::TRANS1_FREQ: case CC2::TRANS2_FREQ: case CC2::TRANS3_FREQ: drums[num - CC2::TRANS1_FREQ].transGen.pitch = transPitch(); break;
        case CC2::TRANS4_FREQ: sn.transGen.pitch = transPitch(); break;
        case CC2::TRANS5_FREQ: cy.transGen.pitch = transPitch(); break;
        case CC2::TRANS6_FREQ: hh.transGen.pitch = transPitch(); break;
        case CC2::FILTER_TYPE_1: case CC2::FILTER_TYPE_2: case CC2::FILTER_TYPE_3:
        case CC2::FILTER_TYPE_4: case CC2::FILTER_TYPE_5: case CC2::FILTER_TYPE_6: {
            int type;
            if (isFloat) { type = toU8(f(f((int) FILTER_NAIVE_2_POLE * flt) + 1)); type = type >= FILTER_OFF ? FILTER_OFF - 1 : type; }
            else type = (itg + 1) & 0xff;
            voices[num - CC2::FILTER_TYPE_1]->filterType = type;
            break;
        }
        case CC2::FILTER_DRIVE_1: case CC2::FILTER_DRIVE_2: case CC2::FILTER_DRIVE_3:
        case CC2::FILTER_DRIVE_4: case CC2::FILTER_DRIVE_5: case CC2::FILTER_DRIVE_6:
            SVF_setDrive(voices[num - CC2::FILTER_DRIVE_1]->filter, flt); break;
        case CC2::MIX_MOD_1: case CC2::MIX_MOD_2: case CC2::MIX_MOD_3: drums[num - CC2::MIX_MOD_1].mixOscs = itg != 0 ? 1 : 0; break;
        case CC2::VOLUME_MOD_ON_OFF1: case CC2::VOLUME_MOD_ON_OFF2: case CC2::VOLUME_MOD_ON_OFF3:
        case CC2::VOLUME_MOD_ON_OFF4: case CC2::VOLUME_MOD_ON_OFF5: case CC2::VOLUME_MOD_ON_OFF6:
            voices[num - CC2::VOLUME_MOD_ON_OFF1]->volumeMod = itg != 0 ? 1 : 0; break;
        case CC2::VELO_MOD_AMT_1: case CC2::VELO_MOD_AMT_2: case CC2::VELO_MOD_AMT_3:
        case CC2::VELO_MOD_AMT_4: case CC2::VELO_MOD_AMT_5: case CC2::VELO_MOD_AMT_6:
            velocityModulators[num - CC2::VELO_MOD_AMT_1].amount = flt; break;
        case CC2::WAVE_LFO1: case CC2::WAVE_LFO2: case CC2::WAVE_LFO3:
        case CC2::WAVE_LFO4: case CC2::WAVE_LFO5: case CC2::WAVE_LFO6:
            lfos[num - CC2::WAVE_LFO1]->waveform = wave(NUM_LFO_WAVEFORMS); break;
        case CC2::RETRIGGER_LFO1: case CC2::RETRIGGER_LFO2: case CC2::RETRIGGER_LFO3:
        case CC2::RETRIGGER_LFO4: case CC2::RETRIGGER_LFO5: case CC2::RETRIGGER_LFO6:
            lfos[num - CC2::RETRIGGER_LFO1]->retrigger = isFloat ? toU8(f(7 * flt)) : itg; break;
        case CC2::SYNC_LFO1: case CC2::SYNC_LFO2: case CC2::SYNC_LFO3:
        case CC2::SYNC_LFO4: case CC2::SYNC_LFO5: case CC2::SYNC_LFO6: {
            Lfo& lfo = *lfos[num - CC2::SYNC_LFO1];
            lfo.sync = itg;
            lfo.phaseInc = lfo_calcPhaseInc(lfo.freq, lfo.sync, bpm);
            break;
        }
        case CC2::OFFSET_LFO1: case CC2::OFFSET_LFO2: case CC2::OFFSET_LFO3:
        case CC2::OFFSET_LFO4: case CC2::OFFSET_LFO5: case CC2::OFFSET_LFO6:
            lfos[num - CC2::OFFSET_LFO1]->phaseOffset = toU32(f(flt * F_2POW32)); break;
        case CC2::AUDIO_OUT1: case CC2::AUDIO_OUT2: case CC2::AUDIO_OUT3:
        case CC2::AUDIO_OUT4: case CC2::AUDIO_OUT5: case CC2::AUDIO_OUT6:
            audioRouting[num - CC2::AUDIO_OUT1] = (uint8_t) itg; break;
        case CC2::PWM_1: case CC2::PWM_2: case CC2::PWM_3: drums[num - CC2::PWM_1].pwm = flt; break;
        case CC2::FX_TYPE: fx.fxType = itg; fx.updateParameter(); break;
        case CC2::FX_ROUTING: fx.audioRouting = itg; break;
        case CC2::FX_AMOUNT: fx.dryWet = f(itg / 100.0); break;
        case CC2::FX_DIST_TYPE: fx.distortion.type = itg; break;
        case CC2::FX_P1: case CC2::FX_P2: case CC2::FX_P3: case CC2::FX_P4: fx.setParameter(num - CC2::FX_P1, flt); break;
        case CC2::FX_RATIO: compressorSetRatio(fx.compressor, itg); break;
        case CC2::FX_WAVE: fx.ringmod.osc.waveform = itg; break;
        case CC2::FX_DELAY_TYPE: fx.delay.type = itg; break;
        case CC2::PWM_4: sn.pwm = flt; break;
        case CC2::PWM_5: cy.pwm = flt; break;
        case CC2::PWM_6: hh.pwm = flt; break;
        default: break;
    }
}

// ---------------------------------------------------------------------------------------
// Reverb: lxr.js's ReverbBus (FX/Reverb.c, VerbTools and TiltEq.c from the firmware). See the comments there.
// ---------------------------------------------------------------------------------------
static const int VERB_SIZE = 0x800;
static void verbInit(VerbDelay& d, bool mod) {
    d.buf.assign(VERB_SIZE, 0.0f); d.mod = mod;
    if (mod) d.lp.r = f(1 - std::exp(-2 * PI * 0.0001308911269084528 / REAL_FS));
}
static void verbCalcLength(VerbDelay& d) {
    double pos = d.writePtr - f(d.length * d.roomSz);
    if (pos < 0) pos += VERB_SIZE;
    d.readPtr = (int) std::trunc(pos) & (VERB_SIZE - 1);
}
static void verbSetLength(VerbDelay& d, int l) { d.length = l; verbCalcLength(d); }
static void verbSetRoomSize(VerbDelay& d, double sz) { d.roomSz = sz; verbCalcLength(d); }
static double verbRead(VerbDelay& d) { const double r = d.buf[(size_t) d.readPtr]; d.readPtr = (d.readPtr + 1) & (VERB_SIZE - 1); return r; }
static void verbWrite(VerbDelay& d, double x) { d.buf[(size_t) d.writePtr] = (float) x; d.writePtr = (d.writePtr + 1) & (VERB_SIZE - 1); }
static double verbReadTap(const VerbDelay& d, double percent) {
    const int off = (int) std::trunc(percent / 100 * d.length) & 0xffff;
    return d.buf[(size_t) ((d.readPtr - off) & (VERB_SIZE - 1))];
}
static double verbReadTapSample(const VerbDelay& d, int idx) { return d.buf[(size_t) ((d.readPtr - idx) & (VERB_SIZE - 1))]; }
static double verbProcess(VerbDelay& d, double x) { verbWrite(d, x); return verbRead(d); }
static void verbModCalcLength(VerbDelay& d) {
    const double filtered = calcOnePole(d.lp, f(d.length * d.roomSz) + d.depth * d.modVal);
    double pos = d.writePtr - filtered;
    if (pos < 0) pos += VERB_SIZE;
    const double itg = std::trunc(pos);
    d.frac = pos - itg; d.readPtr = (int) itg & (VERB_SIZE - 1);
}
static void verbModSetLength(VerbDelay& d, int l) { d.length = l; d.lp.b = f(l * d.roomSz); }
static void verbModSetValue(VerbDelay& d, double v) { d.modVal = v; verbModCalcLength(d); }
static double allpassProcess(Allpass& a, double x) {
    if (a.d.mod) verbModCalcLength(a.d);
    a.out = verbRead(a.d);
    a.in = x + a.g * a.out;
    verbWrite(a.d, a.in);
    return a.out + -a.g * a.in;
}
static void tiltSetFreq(Tilt& e, double fc) { const double w = 2 * PI * fc, n = 1 / (3.0 * REAL_FS + w); e.a0 = 2 * w * n; e.b1 = (3.0 * REAL_FS - w) * n; }
static void tiltSetParameter(Tilt& e, double p) {
    p = p * p * p;
    if (p >= 0) { e.lgain = 0; e.hgain = 1; tiltSetFreq(e, 20000 * p); } else { e.lgain = 1; e.hgain = 0; tiltSetFreq(e, 20000 * (1 + p)); }
}
static double tiltProcess(Tilt& e, double x) { e.lp = e.a0 * x + e.b1 * e.lp; return e.lgain * e.lp + e.hgain * (x - e.lp); }
static const int VERB_EARLY[4] = { 12, 24, 38, 50 };

ReverbBus::ReverbBus() {
    tiltSetFreq(eq, 400); tiltSetFreq(eqFb, 400);
    lfoA = 2 * std::sin(PI * 1.31 / REAL_FS);
    lpFeedback.r = 0.5; lpImpulse.r = 0.5;
    for (auto& a : apDelay) verbInit(a.d, false);
    for (auto& a : apModDelay) verbInit(a.d, true);
    for (auto& a : inputAllpass) verbInit(a.d, false);
    for (auto& d : delay) verbInit(d, false);
    verbInit(early, false); verbInit(preDelay, false);
    verbSetLength(early, 2000); verbSetLength(preDelay, 2031);
    const int ia[4] = { 142, 107, 379, 277 }, dl[4] = { 2047, 1913, 1720, 1163 };
    const double ig[4] = { 0.75, 0.75, 0.625, 0.625 };
    for (int i = 0; i < 4; i++) { verbSetLength(inputAllpass[i].d, ia[i]); verbSetLength(delay[i], dl[i]); inputAllpass[i].g = ig[i]; }
    verbSetLength(apDelay[0].d, 1800); verbSetLength(apDelay[1].d, 2039);
    verbModSetLength(apModDelay[0].d, 672); verbModSetLength(apModDelay[1].d, 908);
    for (int i = 0; i < 4; i++) setParameter(i, parameter[i]);
}

void ReverbBus::setParameter(int idx, double v) {
    parameter[idx] = (float) v;
    if (idx == 0) verbSetRoomSize(preDelay, 0.02166 + v * 0.97784342688330871492);
    else if (idx == 1) feedback = v;
    else if (idx == 2) { tiltSetParameter(eq, -1 + 2 * v); tiltSetParameter(eqFb, -1 + 2 * v); lpFeedback.r = f(v); }
    else if (idx == 3) earlyVolume = v;
}

void ReverbBus::process(int16_t* out, int size) {
    const int16_t* fx = input;
    lfoS[0] = lfoS[0] - lfoA * lfoS[1]; lfoS[1] = lfoS[1] + lfoA * lfoS[0];
    verbModSetValue(apModDelay[1].d, lfoS[0]); verbModSetValue(apModDelay[0].d, lfoS[1]);
    for (int i = 0; i < size; i++) {
        const int mix = ssat16(fx[2 * i] / 2 + fx[2 * i + 1] / 2);
        double x = verbProcess(preDelay, mix / 32768.0);
        verbProcess(early, x);
        const double e = verbReadTap(early, VERB_EARLY[0]) + verbReadTap(early, VERB_EARLY[1]) * 0.5
                         + verbReadTap(early, VERB_EARLY[2]) * 0.125 + verbReadTap(early, VERB_EARLY[3]) * 0.25;
        x = 0.5 * tiltProcess(eq, x);
        const double in1 = allpassProcess(inputAllpass[0], allpassProcess(inputAllpass[1], x));
        const double in2 = allpassProcess(inputAllpass[2], allpassProcess(inputAllpass[3], x));
        double t = allpassProcess(apModDelay[0], in1 + lastOut2);
        t = verbProcess(delay[0], t) * feedback;
        t = allpassProcess(apDelay[1], t);
        t = verbProcess(delay[1], t);
        lastOut = calcOnePole(lpFeedback, t * feedback);
        t = allpassProcess(apModDelay[1], in2 + lastOut);
        t = verbProcess(delay[2], t) * feedback;
        t = allpassProcess(apDelay[1], t);
        t = verbProcess(delay[3], t);
        lastOut2 = t * feedback;
        double o2 = verbReadTapSample(delay[2], 266) * 0.6;
        o2 -= verbReadTapSample(apDelay[1].d, 1913) * 0.6;
        o2 += verbReadTapSample(delay[3], 996) * 0.6;
        o2 -= verbReadTapSample(apDelay[0].d, 187) * 0.6;
        o2 -= verbReadTapSample(delay[1], 1066) * 0.6;
        const double o = tiltProcess(eqFb, e * earlyVolume + o2);
        const int wet = (int) std::max(-32768.0, std::min(32767.0, std::trunc(o * 32768)));
        out[2 * i] = (int16_t) qadd16(out[2 * i], wet); out[2 * i + 1] = (int16_t) qadd16(out[2 * i + 1], wet);
    }
}

}

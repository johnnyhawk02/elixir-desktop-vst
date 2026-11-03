// C++ twin of Elixir Desktop's app/player.js (see Player.h). Comments on the sound are in the JS.
#include "Player.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace lxr {

static const double FS = REAL_FS;
static const double PI = 3.141592653589793;
static const double VERB_MAKEUP = 1;
static inline double jsRound(double x) { return std::floor(x + 0.5); }
static inline long jsMod(long a, long b) { return a % b; } // operands here are never negative

static bool barOk(int b, long bar) {
    if (!b) return true;
    if (b > 100) return jsMod(bar, b - 100) == 0;
    if (b > 0) return jsMod(bar, b) == b - 1;
    return jsMod(bar, -b) != -b - 1;
}
static double delaySamples(double pos, double bpm) { return 240 / bpm * std::pow(2.0, -7 * (1 - pos)) * FS; }
static double rushHash(int t, int pg, int s) {
    uint32_t h = (uint32_t) (t * 131 + pg * 17 + s + 1) * 0x9e3779b1u;
    h ^= h >> 15; h *= 0x85ebca77u; h ^= h >> 13;
    return h / 4294967296.0;
}

Player::Player(double outRate) {
    e.loadInitKit();
    ratio = FS / outRate;
    for (int n = 0; n < 6; n++) { gain[n] = gainTo[n] = 1; fxAmt[n] = 1; }
    hpLog = std::log(30.0);
    for (int k = 0; k < 8; k++) { spF[k] = std::tan(PI * 60 * std::pow(2.0, k * 1.1) / FS); spG[k] = 1; }
    delayPos = 1 + std::log2(3.0 / 16) / 7;
    dist.fxType = FX_DISTORTION; dist.forceWet = true;
    render(A); render(B);
}

void Player::start() {
    rushPhOn = false; rushHeadOn = false; step = 0; bar = 0; toNext = 0; nQueue = 0; fresh = true;
}
void Player::setPlaying(bool p) {
    if (p && !playing) start();
    playing = p;
    if (!playing) flushBar();
}

void Player::msg(const Msg& m) {
    if (m.atBar && playing) {
        // held until the next bar, in a free slot of atBarStore (the ones in use are in atBarQ)
        if (nAtBar >= 32) return;
        for (auto& s : atBarStore) {
            bool used = false;
            for (int q = 0; q < nAtBar; q++) if (atBarQ[q] == &s) used = true;
            if (!used) { s = m; atBarQ[nAtBar++] = &s; return; }
        }
        return;
    }
    apply(m);
}

void Player::apply(const Msg& m) {
    switch (m.type) {
        case Msg::Param: e.setParam(m.par, m.value); break;
        case Msg::Kit: e.loadKit(m.kit, m.kitN); break;
        case Msg::Trig: e.trigger(m.voice, m.vel, 60); gainTo[std::min(m.voice, 5)] = 1; break;
        case Msg::Rush: rush = m.rush; if (m.rushT >= 0) rushT = m.rushT; break;
        case Msg::RushSetT: {
            double* f[11] = { &rs.start, &rs.p32, &rs.top, &rs.hats, &rs.only, &rs.kicks, &rs.first, &rs.hp, &rs.hpTop, &rs.rollMs, &rs.hpMs };
            for (int i = 0; i < 11; i++) if (m.rsMask & (1 << i)) *f[i] = m.rs[i];
            break;
        }
        case Msg::Mute: for (int n = 0; n < 6; n++) muted[n] = m.muted[n] ? 1 : 0; break;
        case Msg::XMod: e.setCrossMod(m.xOn ? m.xAmt : nullptr, m.xTarget); break;
        case Msg::DelayT: delayPos = m.pos; break;
        case Msg::Mix: {
            if (m.hasMaster) {
                const double* v = m.master;
                auto has = [&](int i) { return (m.masterMask & (1 << i)) != 0; };
                if (has(0)) master.comp = v[0] != 0;
                if (has(1)) master.thresh = v[1];
                if (has(2)) master.ratio = v[2];
                if (has(3)) master.attack = v[3];
                if (has(4)) master.release = v[4];
                if (has(5)) master.makeup = v[5];
                if (has(6)) master.duck = v[6];
                if (has(7)) master.duckRelease = v[7];
                if (has(8)) master.duckFx = v[8] != 0;
                if (has(9)) master.duckOn = v[9] != 0;
                if (has(10)) master.space = v[10];
            }
            if (m.hasDist) {
                dist.distortion.type = m.distType;
                for (int i = 0; i < 4; i++) dist.setParameter(i, m.distP[i] / 127);
                distMix = m.distMix / 100;
                if (m.hasVerb) for (int i = 0; i < 4; i++) verb.setParameter(i, m.verb[i] / 127);
                std::memcpy(sendD, m.sendD, sizeof(sendD)); std::memcpy(sendR, m.sendR, sizeof(sendR));
                echoWidth = m.echoPct / 100;
                echoSync = m.echoSync; echoFree = m.echoFree; echoFine = m.echoFine;
            }
            std::memcpy(fxAmt, m.fx, sizeof(fxAmt));
            break;
        }
        case Msg::Seq: {
            // a live pattern change wins over one still waiting for the bar
            int k = 0;
            for (int q = 0; q < nAtBar; q++) if (atBarQ[q]->type != Msg::Seq) atBarQ[k++] = atBarQ[q];
            nAtBar = k;
            nPages = std::min(m.nPages, MAX_PAGES);
            for (int p = 0; p < std::max(1, nPages); p++) pat[p] = m.pages[p];
            patternOn = m.patternOn;
            pageBpm = m.bpm;
            if (!(hostOn && hostPlaying)) bpm = m.bpm;
            if (m.hasGroove) std::memcpy(grooveOff, m.groove, sizeof(grooveOff));
            vel = m.vel_; e.bpm = bpm;
            accGain = std::pow(10.0, -m.accentDb / 20);
            pagePlaying = m.playing;
            if (!(hostOn && hostPlaying)) setPlaying(m.playing);
            break;
        }
        case Msg::None: break;
    }
}

// a pattern loaded while playing: its messages wait for the first step of the next bar; a queued pattern never starts
// or stops the transport
void Player::flushBar() {
    Msg* q[32]; const int n = nAtBar;
    std::memcpy(q, atBarQ, sizeof(q)); nAtBar = 0;
    for (int i = 0; i < n; i++) {
        Msg& m = *q[i];
        if (m.type == Msg::Seq) m.playing = playing;
        apply(m);
    }
}

void Player::host(bool hasTransport, bool hp, double hbpm, double ppq) {
    hostOn = hasTransport;
    if (!hasTransport) { if (hostPlaying) { hostPlaying = false; bpm = pageBpm; e.bpm = bpm; setPlaying(false); } return; }
    if (hp) {
        if (hbpm > 0) { bpm = hbpm; e.bpm = hbpm; }
        const double len = FS * 60 / bpm / 4;
        // samples already rendered but not yet heard: the rest of A and all of B
        const double lat = (64 - idx) + 64 - frac;
        const double pos = ppq * 4 + lat / len; // 16ths, at the point the clock below has reached
        // the next step not yet heard: one already inside the rendered audio plays at once, a little late
        const double next = std::ceil(ppq * 4 - 1e-9);
        const double internal = playing ? (double) (bar * 16 + step) - toNext / len : -1e9;
        if (!hostPlaying || !playing || std::fabs(internal - pos) > 0.5) {
            if (!playing) { playing = true; }
            rushPhOn = false; rushHeadOn = false; nQueue = 0; fresh = true;
            const long n = (long) next;
            step = (int) (((n % 16) + 16) % 16); bar = std::max(0L, (n - step) / 16);
            toNext = (next - pos) * len;
        }
    } else if (hostPlaying) {
        bpm = pageBpm; e.bpm = bpm;
        setPlaying(false);
        pagePlaying = false;
    }
    hostPlaying = hp; lastPpq = ppq;
}

// the hits of step s, each due `at` samples from now: the grid time of the step moved by the groove
void Player::schedule(int s, long b, double at0) {
    const int pg = nPages ? (int) jsMod(b, nPages) : 0;
    const Page& P = pat[pg];
    const double len = FS * 60 / bpm / 4;
    nSched[s] = 0;
    if (rush != 0 && patternOn) { scheduleRush(s, b, pg, P, at0, len); return; }
    for (int t = 0; t < 7 && patternOn; t++) {
        if (!P.on[t][s] || !barOk(P.bar[t][s], b) || muted[std::min(t, 5)]) continue;
        const double off = grooveOff[t][s];
        push({ std::max(0.0, at0 + off * len), t, P.acc[t][s] != 0, 0, 0, false });
        sched[s][nSched[s]++] = t;
    }
}
void Player::scheduleRush(int s, long b, int pg, const Page& P, double at0, double len) {
    const int rt = rushT; const double R = rush; const RushSet& S = rs;
    const double p = std::pow(std::min(1.0, R / S.only), 2.5), pHat = std::pow(std::min(1.0, R / S.hats), 2.5);
    bool snare = R > S.start, sAcc = false, orig = false;
    for (int t = 0; t < 7; t++) {
        if (!P.on[t][s] || !barOk(P.bar[t][s], b)) continue;
        orig = true;
        const bool down = !t && !(s & 3);
        if (down && (R >= S.first || (R >= S.kicks && s))) continue;
        if (t == rt || (!down && rushHash(t, pg, s) < (t >= 5 ? pHat : p))) { snare = true; sAcc = sAcc || P.acc[t][s]; continue; }
        if (muted[std::min(t, 5)]) continue;
        push({ std::max(0.0, at0 + grooveOff[t][s] * len), t, P.acc[t][s] != 0, 0, 0, false });
        if (nSched[s] < 8) sched[s][nSched[s]++] = t;
    }
    if (snare && !muted[std::min(rt, 5)]) {
        const int n = R > (S.start + S.p32) / 2 ? 2 : 1;
        const double at = R > S.start ? at0 : at0 + grooveOff[rt][s] * len;
        const double sw = R > S.start ? 0.6 + 0.4 * (R - S.start) / (100 - S.start) : 1;
        for (int i = 0; i < n; i++) {
            const bool first = !i && orig;
            push({ std::max(0.0, at + i * len / n), rt, first && sAcc, sw, R > S.start ? (first ? 2 : 1) : 0, false });
        }
        if (nSched[s] < 8) sched[s][nSched[s]++] = 3;
    }
}

void Player::spacer(const float* key, float* XL, float* XR) {
    const double cut = master.space / 100 * 0.9, k = 1 / 1.4; double* S = spS;
    const double att = 1 - std::exp(-64 / (0.003 * FS)), rel = std::exp(-64000 / (master.duckRelease * FS));
    for (int b = 0; b < 8; b++) {
        const double g = spF[b], a = 1 / (1 + g * (g + k)); const int o = b * 6;
        double pk = 0, s1 = S[o], s2 = S[o + 1];
        for (int i = 0; i < 64; i++) {
            const double hp = (key[i] - (g + k) * s1 - s2) * a, bp = g * hp + s1;
            s1 = g * hp + bp; s2 += 2 * g * bp;
            const double m = bp < 0 ? -bp : bp; if (m > pk) pk = m;
        }
        S[o] = s1; S[o + 1] = s2;
        pk *= k;
        // (in the JS the stored Float32Array values are rounded, but e and g1 here are the unrounded doubles)
        const double env = pk > spEnv[b] ? spEnv[b] + (pk - spEnv[b]) * att : spEnv[b] * rel;
        spEnv[b] = (float) env;
        const double g0 = spG[b], g1 = 1 - cut * std::min(1.0, env / 0.04);
        spG[b] = (float) g1;
        if (g0 == 1 && g1 == 1) {
            for (int c = 0; c < 2; c++) {
                const float* X = c ? XR : XL; double t1 = S[o + 2 + 2 * c], t2 = S[o + 3 + 2 * c];
                for (int i = 0; i < 64; i++) { const double hp = (X[i] - (g + k) * t1 - t2) * a, bp = g * hp + t1; t1 = g * hp + bp; t2 += 2 * g * bp; }
                S[o + 2 + 2 * c] = t1; S[o + 3 + 2 * c] = t2;
            }
            continue;
        }
        for (int c = 0; c < 2; c++) {
            float* X = c ? XR : XL; double t1 = S[o + 2 + 2 * c], t2 = S[o + 3 + 2 * c];
            for (int i = 0; i < 64; i++) {
                const double hp = (X[i] - (g + k) * t1 - t2) * a, bp = g * hp + t1;
                t1 = g * hp + bp; t2 += 2 * g * bp;
                X[i] = (float) (X[i] + (g0 + (g1 - g0) * (i + 1) / 64 - 1) * bp * k);
            }
            S[o + 2 + 2 * c] = t1; S[o + 3 + 2 * c] = t2;
        }
    }
}

void Player::render(float (*dst)[64]) {
    if (playing) {
        if (toNext <= 0) {
            if (step == 0 && nAtBar) flushBar();
            const double len = FS * 60 / bpm / 4;
            if (fresh) { fresh = false; schedule(step, bar, toNext); }
            const int next = (step + 1) % 16;
            schedule(next, next == 0 ? bar + 1 : bar, toNext + len);
            Event ev; ev.type = Event::Step; ev.step = step; ev.bar = (int) bar; ev.gr = gr; ev.duck = duckMin;
            ev.nHits = nSched[step]; for (int i = 0; i < ev.nHits; i++) ev.hits[i] = sched[step][i];
            event(ev);
            nSched[step] = 0;
            gr = 0; duckMin = 1;
            step = next;
            if (step == 0) bar++;
            toNext += len;
        }
        toNext -= 64;
    } else nQueue = 0;
    const RushSet& S = rs;
    if (playing && patternOn && rush > S.p32) {
        const double len = FS * 60 / bpm / 4, target = std::min(1 + (rush - S.p32) * (S.top - 1) / (100 - S.p32), std::log2(len / 256));
        if (!rushPhOn) { rushPhOn = true; rushLog = target; rushPh = std::fmod(2 * (1 - (toNext + 64) / len), 1.0); }
        else rushLog += (target - rushLog) * (1 - std::exp(-64 / (S.rollMs / 1000 * FS)));
        const double rate = std::min(std::pow(2.0, rushLog), len / 256), d = rate * 64 / len;
        const double sw = 0.6 + 0.4 * (rush - S.start) / (100 - S.start);
        for (double k = std::floor(rushPh) + 1; k <= rushPh + d; k++) {
            const bool hOn = rushHeadOn; const Hit h = rushHead; rushHeadOn = false;
            push({ (k - rushPh) / d * 64, rushT, hOn && h.acc, sw, 0, true });
        }
        rushPh = std::fmod(rushPh + d, 1.0); rushGap = len / rate / 2;
    } else { rushPhOn = false; rushHeadOn = false; }
    if (nQueue) {
        int keep = 0;
        for (int i = 0; i < nQueue; i++) {
            Hit q = queue[i];
            if (q.at < 64 && rush > S.p32 && (q.roll || q.free) && (q.roll || clock - snareAt < rushGap)) { if (q.roll == 2) { rushHead = q; rushHeadOn = true; } }
            else if (q.at < 64) {
                if (q.t == rushT) snareAt = clock;
                if (!muted[std::min(q.t, 5)]) {
                    const double sw = q.sw != 0 ? q.sw : 1;
                    e.trigger(q.t, q.acc ? 127 : (int) std::max(1.0, jsRound(vel * sw)), 60);
                    gainTo[std::min(q.t, 5)] = (float) (q.acc ? 1 : accGain * sw);
                }
            } else { q.at -= 64; queue[keep++] = q; }
        }
        nQueue = keep;
    }
    clock += 64;
    e.renderBlock();
    const Voice* const* pans = e.voices;
    float* L = dst[0]; float* R = dst[1];
    std::fill(L, L + 64, 0.0f); std::fill(R, R + 64, 0.0f);
    const Master& M = master; const int16_t* k = e.out[0];
    const bool isDelay = e.fx.fxType == FX_DELAY;
    e.fx.forceWet = isDelay;
    const double eTime = echoSync ? delaySamples(delayPos, bpm) : (1 + 1999 * echoFree) / 1000 * REAL_FS;
    e.fx.setDelaySync(isDelay, isDelay ? eTime * (1 + echoFine / 10000) : 0);
    double pk = 0;
    for (int i = 0; i < 64; i++) pk = std::max(pk, (double) std::abs(k[i]));
    pk = pk / 32768 * gainTo[0];
    duckEnv = std::max(pk, duckEnv * std::exp(-64000 / (M.duckRelease * FS)));
    const double duck0 = duckAt, duck1 = 1 - (M.duckOn ? M.duck : 0) * std::min(1.0, duckEnv / 0.3);
    duckAt = duck1; duckMin = std::min(duckMin, duck1);
    const bool sp = M.duckOn && M.space > 0; float* XL = sp ? spL : L; float* XR = sp ? spR : R;
    if (sp) { std::fill(XL, XL + 64, 0.0f); std::fill(XR, XR + 64, 0.0f); }
    else if (spOn) { std::fill(spS, spS + 48, 0.0); std::fill(spEnv, spEnv + 8, 0.0f); std::fill(spG, spG + 8, 1.0f); }
    spOn = sp;
    for (int n = 0; n < 6; n++) {
        const double d0 = n ? duck0 : 1, d1 = n ? duck1 : 1;
        const double g0 = gain[n] * d0, g1 = gainTo[n] * d1, dg = (g1 - g0) / 64;
        const double fa = e.audioRouting[n] == MIXER_ROUTING_FX_BUS ? fxAmt[n] : 0;
        const double pl = pans[n]->triggeredPanL / 32768, pr = pans[n]->triggeredPanR / 32768;
        const int16_t* o = e.out[n];
        double lv = 0;
        float* OL = n ? XL : L; float* OR = n ? XR : R;
        for (int i = 0; i < 64; i++) {
            const double v = o[i] * (g0 + dg * i);
            OL[i] = (float) (OL[i] + v * pl); OR[i] = (float) (OR[i] + v * pr);
            const double a = v < 0 ? -v : v; if (a > lv) lv = a;
        }
        lv *= std::max(pl, pr); if (lv > lvl[n]) lvl[n] = (float) lv;
        fxGain[n] = (float) (g1 * fa);
        gain[n] = gainTo[n];
    }
    if (sp) {
        float* key = spKey;
        for (int i = 0; i < 64; i++) key[i] = (float) (((double) L[i] + R[i]) / 2);
        spacer(key, XL, XR);
        for (int i = 0; i < 64; i++) { L[i] = (float) ((double) L[i] + XL[i]); R[i] = (float) ((double) R[i] + XR[i]); }
    }
    if (++lvlN >= 16) {
        lvlN = 0;
        bool on = false; for (float v : lvl) if (v > 1e-4) on = true;
        if (on || lvlOn) { Event ev; ev.type = Event::Lvl; std::memcpy(ev.lvl, lvl, sizeof(lvl)); event(ev); }
        lvlOn = on; std::fill(lvl, lvl + 6, 0.0f);
    }
    const int16_t* fx = e.renderFx(fxGain, echoWidth);
    const double fd0 = M.duckFx ? duck0 : 1, fdd = M.duckFx ? (duck1 - duck0) / 64 : 0;
    {
        ReverbBus& bus = verb; const float* sends = sendR; const double mk = VERB_MAKEUP;
        float* bi = busIn; std::fill(bi, bi + 128, 0.0f);
        bool any = false;
        for (int n = 0; n < 6; n++) {
            const double g = sends[n] * (double) gainTo[n] * (n ? duck1 : 1);
            if (g == 0) continue;
            any = true;
            const int16_t* o = e.out[n]; const double pl = pans[n]->triggeredPanL * g, pr = pans[n]->triggeredPanR * g;
            for (int i = 0; i < 64; i++) { bi[2 * i] = (float) (bi[2 * i] + o[i] * pl); bi[2 * i + 1] = (float) (bi[2 * i + 1] + o[i] * pr); }
        }
        for (int i = 0; i < 128; i++) bus.input[i] = (int16_t) std::max(-32768.0, std::min(32767.0, jsRound(bi[i])));
        int16_t* bo = busOut; std::fill(bo, bo + 128, (int16_t) 0);
        bus.process(bo, 64);
        bool some = any; for (int i = 0; i < 128 && !some; i++) if (bo[i]) some = true;
        if (some) for (int i = 0; i < 64; i++) {
            const double g = mk / 32768 * (fd0 + fdd * i);
            L[i] = (float) (L[i] + bo[2 * i] * g); R[i] = (float) (R[i] + bo[2 * i + 1] * g);
        }
    }
    for (int i = 0; i < 64; i++) { const double g = (fd0 + fdd * i) / 32768; L[i] = (float) (L[i] + fx[2 * i] * g); R[i] = (float) (R[i] + fx[2 * i + 1] * g); }
    if (distMix != 0 || distAt != 0) {
        int16_t* bi = dist.input;
        for (int i = 0; i < 64; i++) {
            bi[2 * i] = (int16_t) std::max(-32768.0, std::min(32767.0, jsRound(L[i] * 32768.0)));
            bi[2 * i + 1] = (int16_t) std::max(-32768.0, std::min(32767.0, jsRound(R[i] * 32768.0)));
        }
        int16_t* bo = busOut; std::fill(bo, bo + 128, (int16_t) 0);
        dist.process(bo, 64, fxRng);
        const double m0 = distAt, m1 = distMix;
        for (int i = 0; i < 64; i++) {
            const double m = m0 + (m1 - m0) * (i + 1) / 64;
            L[i] = (float) (L[i] + (bo[2 * i] / 32768.0 - L[i]) * m); R[i] = (float) (R[i] + (bo[2 * i + 1] / 32768.0 - R[i]) * m);
        }
        distAt = m1;
    }
    if (rush > S.hp || hpOn) highPass(L, R);
    if (M.comp) {
        const double ca = std::exp(-1000 / (M.attack * FS)), cr = std::exp(-1000 / (M.release * FS));
        const double slope = 1 / M.ratio - 1, mk = std::pow(10.0, M.makeup / 20);
        double env = compEnv, g2 = gr;
        for (int i = 0; i < 64; i++) {
            const double peak = std::max(std::fabs((double) L[i]), std::fabs((double) R[i])) + 1e-9;
            const double over = std::max(0.0, 20 * std::log10(peak) - M.thresh);
            env = over + (over > env ? ca : cr) * (env - over);
            const double g = std::pow(10.0, env * slope / 20) * mk;
            L[i] = (float) (L[i] * g); R[i] = (float) (R[i] * g);
            g2 = std::max(g2, -env * slope);
        }
        compEnv = env; gr = g2;
    }
}

void Player::highPass(float* L, float* R) {
    const double lo = std::log(30.0); const bool up = rush > rs.hp;
    const double target = up ? lo + std::log(rs.hpTop / 30) * std::min(1.0, (rush - rs.hp) / (100 - rs.hp)) : lo;
    if (!hpOn) { hpOn = true; hpLog = lo; hpMix = 0; std::fill(hpS, hpS + 4, 0.0); hpG = std::tan(PI * 30 / FS); }
    hpLog += (target - hpLog) * (1 - std::exp(-64 / (rs.hpMs / 1000 * FS)));
    if (!up && hpLog - lo < 0.01) hpLog = lo;
    const double g0 = hpG, g1 = std::tan(PI * std::exp(hpLog) / FS), k = 1 / 0.9; double* S = hpS;
    const double m0 = hpMix, m1 = up || hpLog > lo ? std::min(1.0, m0 + 0.15) : std::max(0.0, m0 - 0.15);
    for (int i = 0; i < 64; i++) {
        const double g = g0 + (g1 - g0) * (i + 1) / 64, a = 1 / (1 + g * (g + k)), m = m0 + (m1 - m0) * (i + 1) / 64;
        for (int c = 0; c < 2; c++) {
            float* X = c ? R : L; const double x = X[i], s1 = S[2 * c], s2 = S[2 * c + 1];
            const double hp = (x - (g + k) * s1 - s2) * a, bp = g * hp + s1, lp = g * bp + s2;
            S[2 * c] = g * hp + bp; S[2 * c + 1] = g * bp + lp;
            X[i] = (float) (x + (hp - x) * m);
        }
    }
    hpG = g1; hpMix = m1;
    if (m1 == 0 && !up) { hpOn = false; std::fill(hpS, hpS + 4, 0.0); }
}

void Player::process(float* L, float* R, int n) {
    for (int j = 0; j < n; j++) {
        while (idx >= 64) {
            std::swap(A, B);
            render(B);
            idx -= 64;
        }
        const int i = idx; const double fr = frac;
        const double l0 = A[0][i], r0 = A[1][i];
        const double l1 = i < 63 ? A[0][i + 1] : B[0][0];
        const double r1 = i < 63 ? A[1][i + 1] : B[1][0];
        L[j] = (float) (l0 + (l1 - l0) * fr);
        R[j] = (float) (r0 + (r1 - r0) * fr);
        frac += ratio;
        const int k = (int) std::floor(frac);
        idx += k; frac -= k;
    }
}

}

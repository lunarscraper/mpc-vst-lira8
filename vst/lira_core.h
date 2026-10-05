// lira_core.h - LIRA-8 drone synth, native C++ rebuild of Mike Moreno's Pure Data patch
// (https://github.com/MikeMorenoDSP/LIRA-8, BSD-3). Object-by-object translation of
// LIRA-8.pd, lira.voice.pd, os.triangle~.pd, prm.*.pd, compressor~.pd, expander~.pd.
// No dependencies, no statics: one Lira object per plugin instance.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace lira {

enum {
    P_FAST12, P_FAST34, P_FAST56, P_FAST78,
    P_TUNE1, P_TUNE2, P_TUNE3, P_TUNE4, P_TUNE5, P_TUNE6, P_TUNE7, P_TUNE8,
    P_SHARP12, P_SHARP34, P_SHARP56, P_SHARP78,
    P_MOD12, P_MOD34, P_MOD56, P_MOD78,
    P_SRC12, P_SRC34, P_SRC56, P_SRC78,
    P_PITCH1234, P_PITCH5678, P_HOLD1234, P_HOLD5678,
    P_SWITCH, P_TOTALFB, P_VIBRATO,
    P_LFO_A, P_LFO_B, P_ANDOR, P_LINK,
    P_TIME1, P_TIME2, P_FEEDBACK, P_DELMIX, P_DMOD1, P_DMOD2, P_DELSRC, P_LFOWAV,
    P_DRIVE, P_DISTMIX, P_VOLUME, P_QUANTIZE,
    // additions for the Force (not in the original parameter list): the sensor
    // toggles of the Pd GUI, so a drone can be latched from the screen / a preset
    P_SENS1, P_SENS2, P_SENS3, P_SENS4, P_SENS5, P_SENS6, P_SENS7, P_SENS8,
    NUM_PARAMS
};

enum Kind { K_CONT, K_TOGGLE, K_OPT2, K_OPT3 };

struct ParamInfo {
    const char *key;   // chunk key, never change
    const char *name;  // shown by the host
    Kind kind;
    float def;         // in original units (0..127, 0/1, option index)
    const char *opts[3];
};

static const ParamInfo kParams[NUM_PARAMS] = {
    {"fast12", "Fast 12", K_TOGGLE, 1, {}}, {"fast34", "Fast 34", K_TOGGLE, 1, {}},
    {"fast56", "Fast 56", K_TOGGLE, 1, {}}, {"fast78", "Fast 78", K_TOGGLE, 1, {}},
    {"tune1", "Tune 1", K_CONT, 32, {}}, {"tune2", "Tune 2", K_CONT, 64, {}},
    {"tune3", "Tune 3", K_CONT, 32, {}}, {"tune4", "Tune 4", K_CONT, 64, {}},
    {"tune5", "Tune 5", K_CONT, 32, {}}, {"tune6", "Tune 6", K_CONT, 64, {}},
    {"tune7", "Tune 7", K_CONT, 32, {}}, {"tune8", "Tune 8", K_CONT, 64, {}},
    {"sharp12", "Sharp 12", K_CONT, 0, {}}, {"sharp34", "Sharp 34", K_CONT, 0, {}},
    {"sharp56", "Sharp 56", K_CONT, 0, {}}, {"sharp78", "Sharp 78", K_CONT, 0, {}},
    {"mod12", "Mod 12", K_CONT, 0, {}}, {"mod34", "Mod 34", K_CONT, 0, {}},
    {"mod56", "Mod 56", K_CONT, 0, {}}, {"mod78", "Mod 78", K_CONT, 0, {}},
    {"src12", "Source 12", K_OPT3, 1, {"34", "OFF", "LFO"}},
    {"src34", "Source 34", K_OPT3, 1, {"12", "OFF", "LFO"}},
    {"src56", "Source 56", K_OPT3, 1, {"78", "OFF", "LFO"}},
    {"src78", "Source 78", K_OPT3, 1, {"56", "OFF", "LFO"}},
    {"pitch1234", "Pitch 1234", K_CONT, 64, {}}, {"pitch5678", "Pitch 5678", K_CONT, 64, {}},
    {"hold1234", "Hold 1234", K_CONT, 0, {}}, {"hold5678", "Hold 5678", K_CONT, 0, {}},
    {"switch", "Switch", K_TOGGLE, 0, {}}, {"totalfb", "Total FB", K_TOGGLE, 0, {}},
    {"vibrato", "Vibrato", K_TOGGLE, 0, {}},
    {"lfoa", "LFO Freq A", K_CONT, 64, {}}, {"lfob", "LFO Freq B", K_CONT, 64, {}},
    {"andor", "LFO AND/OR", K_OPT2, 0, {"AND", "OR"}}, {"link", "LFO Link", K_TOGGLE, 0, {}},
    {"time1", "Delay Time 1", K_CONT, 64, {}}, {"time2", "Delay Time 2", K_CONT, 64, {}},
    {"feedback", "Delay Feedback", K_CONT, 64, {}}, {"delmix", "Delay Mix", K_CONT, 0, {}},
    {"dmod1", "Delay Mod 1", K_CONT, 0, {}}, {"dmod2", "Delay Mod 2", K_CONT, 0, {}},
    {"delsrc", "Delay Mod Source", K_OPT3, 2, {"SELF", "OFF", "LFO"}},
    {"lfowav", "Delay LFO Wave", K_OPT2, 0, {"TRI", "SQR"}},
    {"drive", "Dist Drive", K_CONT, 64, {}}, {"distmix", "Dist Mix", K_CONT, 64, {}},
    {"volume", "Volume", K_CONT, 127, {}}, {"quantize", "Quantize", K_TOGGLE, 0, {}},
    {"sens1", "Sensor 1", K_TOGGLE, 0, {}}, {"sens2", "Sensor 2", K_TOGGLE, 0, {}},
    {"sens3", "Sensor 3", K_TOGGLE, 0, {}}, {"sens4", "Sensor 4", K_TOGGLE, 0, {}},
    {"sens5", "Sensor 5", K_TOGGLE, 0, {}}, {"sens6", "Sensor 6", K_TOGGLE, 0, {}},
    {"sens7", "Sensor 7", K_TOGGLE, 0, {}}, {"sens8", "Sensor 8", K_TOGGLE, 0, {}},
};

// normalized (0..1, what the host stores) <-> original units
inline float toNorm(int p, float v) {
    switch (kParams[p].kind) {
    case K_CONT: return v / 127.f;
    case K_OPT3: return v / 2.f;
    default: return v;
    }
}
inline float fromNorm(int p, float n) {
    if (n < 0) n = 0;
    if (n > 1) n = 1;
    switch (kParams[p].kind) {
    case K_CONT: return n * 127.f;
    case K_OPT3: return std::floor(n * 2.f + 0.5f);
    default: return n >= 0.5f ? 1.f : 0.f;
    }
}

// tune ranges per voice as MIDI notes ("text define $0-tune" in LIRA-8.pd)
static const float kTuneLo[8] = {-16, -16, 7, 9, 20, 20, 33, 33};
static const float kTuneHi[8] = {93, 93, 109, 107, 116.54f, 116.54f, 126.24f, 131.22f};

static const float kTwoPi = 6.283185307179586f;
inline float mtof(float m) { return 8.17579891564f * std::exp(0.0577622650467f * m); }
inline float ftom(float f) { return f > 0 ? 17.3123405046f * std::log(0.12231220585f * f) : -1500.f; }
inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
// ma.tanh~: Pade approximation, input clipped to +-3
inline float tanhp(float x) {
    x = clampf(x, -3.f, 3.f);
    float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

struct Hip { // Pd hip~
    float coef = 0, norm = 1, last = 0;
    void set(float f, float sr) { coef = clampf(1.f - f * kTwoPi / sr, 0.f, 1.f); norm = 0.5f * (1.f + coef); }
    inline float run(float x) { float n = x + coef * last; float y = norm * (n - last); last = n; return y; }
};
struct Lop { // Pd lop~
    float c = 1, last = 0;
    void set(float f, float sr) { c = clampf(f * kTwoPi / sr, 0.f, 1.f); }
    inline float run(float x) { last += c * (x - last); return last; }
};
struct Smooth { // stands in for the "$1 23.22" line~ ramps
    float v = 0, t = 0;
    inline float run(float k) { v += (t - v) * k; return v; }
    void snap() { v = t; }
};

// os.triangle~: polyBLEP pulse (outlet 1, *0.59) and that pulse through a one-pole
// lowpass at f/4 (min 100 Hz), *2.65 - 0.145 (outlet 2, the "triangle")
struct Osc {
    float ph = 0, pw = 0.53125f, lp = 0, sr = 44100, a100 = 0;
    void init(float s) { sr = s; a100 = std::exp(-kTwoPi * 100.f / sr); }
    static inline float blep(float t, float dt) {
        float r = 0;
        if (t <= dt) { float u = t / dt - 1.f; r += u * u; }
        if (t >= 1.f - dt) { float u = (t - 1.f) / dt + 1.f; r -= u * u; }
        return r;
    }
    inline void run(float freq, float pwIn, float &sq, float &tri) {
        float f = std::fabs(freq);
        float dt = f / sr;
        if (dt > 0.49f) dt = 0.49f;
        float p = ph;
        ph += dt;
        if (ph >= 1.f) ph -= 1.f;
        float q = p + pw;
        if (q >= 1.f) q -= 1.f;
        float s1 = 2.f * p - 1.f, s2 = 2.f * q - 1.f;
        if (dt > 1e-7f) { s1 += blep(p, dt); s2 += blep(q, dt); }
        float s = s1 - s2 + 2.f * pw - 1.f;
        if (ph < p) pw = clampf(pwIn, 0.f, 1.f); // samphold~ on the phasor wrap
        float fc = f * 0.25f;
        float a = fc <= 100.f ? a100 : std::exp(-kTwoPi * (fc > sr * 0.5f ? sr * 0.5f : fc) / sr);
        lp = s * (1.f - a) + a * lp;
        sq = s * 0.59f;
        tri = lp * 2.65f - 0.145f;
    }
};

// prm.sensor: line~ to 1 (attack) or 0 (release); level = line^2, plus the
// one-cycle "touch" thump sin(2*pi*line)/2
struct Sensor {
    float l = 0, inc = 0, target = 0;
    bool gate = false;
    void go(bool g, float ms, float sr) {
        gate = g;
        target = g ? 1.f : 0.f;
        float n = ms * 0.001f * sr;
        inc = (target - l) / (n < 1.f ? 1.f : n);
    }
    inline void run(float &level, float &thump) {
        if (inc != 0.f) {
            l += inc;
            if ((inc > 0 && l >= target) || (inc < 0 && l <= target)) { l = target; inc = 0; }
            thump = 0.5f * std::sin(kTwoPi * l);
        } else thump = 0.f;
        level = l * l;
    }
};

struct Pair { // lira.voice: two oscillators sharing sharp / mod / fast / vibrato LFO
    Osc osc[2];
    Sensor sens[2];
    Smooth freq[2], sharp, mod;
    Hip hp;
    float vibPh = 0, vibInc = 0, out = 0, outHp = 0;
    bool fast = true;
};

struct Comp { // compressor~ 2.5 2.5 -12 5 (hv.envfollow + gain through ma.tanh~)
    float env = 0, c = 0;
    void init(float sr) { float n = 2.5f * sr / 1000.f; c = std::exp(-4.60517f / (n < 1 ? 1 : n)); }
    inline float run(float x) {
        float a = std::fabs(x);
        env = a * (1.f - c) + c * env;
        const float thr = 0.251188643f, inv = 0.2f;
        float g = env > 1e-20f ? clampf(((env - thr) * inv + thr) / env, 0.f, 1.f) : 0.f;
        return x * tanhp(g);
    }
};

struct Expander { // expander~ -60 5 (env~ 512, hop 256, line~ 11 ms)
    float buf[512], win[512];
    int pos = 0, hop = 0;
    float g = 1, inc = 0, target = 1;
    int rampLeft = 0, rampLen = 485;
    void init(float sr) {
        std::memset(buf, 0, sizeof buf);
        for (int i = 0; i < 512; i++) win[i] = (1.f - std::cos(kTwoPi * i / 512.f)) / 512.f;
        rampLen = (int)(0.011f * sr);
        if (rampLen < 1) rampLen = 1;
        pos = hop = 0; g = target = 1; inc = 0; rampLeft = 0;
    }
    inline float run(float x) {
        buf[pos] = x * x;
        pos = (pos + 1) & 511;
        if (++hop >= 256) {
            hop = 0;
            float p = 0;
            for (int i = 0; i < 512; i++) p += buf[(pos + i) & 511] * win[i];
            float db = p > 1e-10f ? 10.f * std::log10(p) : -100.f; // dBFS
            if (db < -100.f) db = -100.f;
            float gdb = (db + 60.f) * 0.8f; // (dB - thr) * (1 - 1/ratio)
            if (gdb > 0) gdb = 0;
            target = std::pow(10.f, gdb * 0.05f);
            inc = (target - g) / rampLen;
            rampLeft = rampLen;
        }
        if (rampLeft > 0) { g += inc; if (--rampLeft == 0) g = target; }
        return x * g;
    }
};

struct DelayLine { // delwrite~ / vd~ (4-point interpolation)
    std::vector<float> b;
    int mask = 0, w = 0;
    void init(float sr) {
        int need = (int)(6.1f * sr) + 8, n = 1;
        while (n < need) n <<= 1;
        b.assign(n, 0.f);
        mask = n - 1; w = 0;
    }
    inline void write(float x) { b[w] = x; w = (w + 1) & mask; }
    inline float read(float dsamp) const {
        int i = (int)dsamp;
        float fr = dsamp - i;
        int r = w - i + 1;
        float a = b[(r) & mask], bb = b[(r - 1) & mask], c = b[(r - 2) & mask], d = b[(r - 3) & mask];
        // a is the newest of the four taps; interpolate between bb and c
        float cmb = c - bb;
        return bb + fr * (cmb - 0.1666667f * (1.f - fr) * ((d - a - 3.f * cmb) * fr + (d + 2.f * a - 3.f * bb)));
    }
};

class Lira {
public:
    float prm[NUM_PARAMS]; // original units

    Lira() {
        for (int i = 0; i < NUM_PARAMS; i++) prm[i] = kParams[i].def;
        setSampleRate(44100);
    }

    void setSampleRate(float s) {
        sr = s < 8000 ? 44100 : s;
        kSm = 1.f - std::exp(-1.f / (0.008f * sr));
        for (int p = 0; p < 4; p++) {
            Pair &P = pair[p];
            P = Pair();
            for (int k = 0; k < 2; k++) P.osc[k].init(sr);
            P.hp.set(3, sr);
            // "random 1001 / 1000 * 3 + 0.5" Hz, one per pair
            P.vibInc = (0.5f + 3.f * (rnd() * 0.5f + 0.5f)) / sr;
        }
        for (int d = 0; d < 2; d++) {
            dl[d].init(sr);
            dHp[d] = Hip(); dHp[d].set(1, sr);
            dLp[d] = Lop(); dLp[d].set(4000, sr);
            selfLp[d] = Lop(); selfLp[d].set(689, sr);
            comp[d] = Comp(); comp[d].init(sr);
            expd[d].init(sr);
            lastWrite[d] = 0;
        }
        hp20 = Hip(); hp20.set(20, sr);
        hp10 = Hip(); hp10.set(10, sr);
        hp3 = Hip(); hp3.set(3, sr);
        lfoA = lfoB = 0; totalFb = 0; sqrLfo = 0;
        std::memset(noteHeld, 0, sizeof noteHeld);
        std::memset(noteCount, 0, sizeof noteCount);
        first = true;
    }

    // original: notes 36..43 -> sensors 1..8. Here every octave maps the same way.
    void noteOn(int note) {
        if (note < 0 || note > 127 || noteHeld[note]) return;
        noteHeld[note] = 1;
        noteCount[((note - 36) % 8 + 8) % 8]++;
    }
    void noteOff(int note) {
        if (note < 0 || note > 127 || !noteHeld[note]) return;
        noteHeld[note] = 0;
        int s = ((note - 36) % 8 + 8) % 8;
        if (noteCount[s] > 0) noteCount[s]--;
    }
    void allNotesOff() {
        std::memset(noteHeld, 0, sizeof noteHeld);
        std::memset(noteCount, 0, sizeof noteCount);
    }

    // knob laws of the patch (static: also used for the value display)
    static float voiceHz(int v, float tune, float pitch, bool quant) {
        float f = mtof(kTuneLo[v] + tune / 127.f * (kTuneHi[v] - kTuneLo[v]));
        f *= 0.01f + pitch / 127.f * 1.99f;
        if (quant) f = mtof(std::floor(ftom(f) + 0.5f));
        return f;
    }
    static float lfoLaw(float v) { float x = v / 127.f; return mtof(x * x * 127.f - 75.f); }
    static float delayLaw(float v) { return 1.45125f * std::pow(2.f, v / 127.f * 12.f); }
    float voiceFreq(int v) const {
        return voiceHz(v, prm[P_TUNE1 + v], prm[v < 4 ? P_PITCH1234 : P_PITCH5678], prm[P_QUANTIZE] >= 0.5f);
    }
    float lfoHz(int which) const { return lfoLaw(prm[which ? P_LFO_B : P_LFO_A]); }
    float delayMs(int which) const { return delayLaw(prm[which ? P_TIME2 : P_TIME1]); }

    void process(float *outL, float *outR, int n) {
        // ---- control rate -------------------------------------------------
        for (int p = 0; p < 4; p++) {
            Pair &P = pair[p];
            bool fast = prm[P_FAST12 + p] >= 0.5f;
            bool fastChanged = fast != P.fast || first;
            P.fast = fast;
            float x = prm[P_SHARP12 + p] / 127.f;
            P.sharp.t = x * x;
            x = prm[P_MOD12 + p] / 127.f;
            P.mod.t = x * x * x * x * 2.f;
            for (int k = 0; k < 2; k++) {
                int v = p * 2 + k;
                P.freq[k].t = voiceFreq(v);
                bool g = noteCount[v] > 0 || prm[P_SENS1 + v] >= 0.5f;
                Sensor &S = P.sens[k];
                if (g != S.gate || fastChanged)
                    S.go(g, g ? (fast ? 100.f : 200.f) : (fast ? 100.f : 8000.f), sr);
            }
            srcSel[p] = (int)prm[P_SRC12 + p];
        }
        float x = prm[P_HOLD1234] / 127.f; hold[0].t = x * x;
        x = prm[P_HOLD5678] / 127.f; hold[1].t = x * x;
        const float sw = prm[P_SWITCH] >= 0.5f ? 1.f : 0.f;
        const float tfb = prm[P_TOTALFB] >= 0.5f ? 1.f : 0.f;
        const float vib = prm[P_VIBRATO] >= 0.5f ? 1.f : 0.f;
        const float gSwOff = (1.f - sw) * 0.999f + 0.001f, gSwOn = sw * 0.999f + 0.001f;
        const float gTfbOn = tfb * 0.999f + 0.001f, gTfbOff = (1.f - tfb) * 0.999f + 0.001f;
        float gPair[4], gLfo[4];
        for (int p = 0; p < 4; p++) {
            gPair[p] = srcSel[p] == 0 ? 1.f : 0.001f;
            gLfo[p] = srcSel[p] == 2 ? 1.f : 0.001f;
        }
        fA.t = lfoHz(0); fB.t = lfoHz(1);
        const bool orMode = prm[P_ANDOR] >= 0.5f;
        const float link = prm[P_LINK] >= 0.5f ? 1.f : 0.f;
        time[0].t = delayMs(0); time[1].t = delayMs(1);
        x = prm[P_FEEDBACK] / 127.f; fb.t = std::pow(2.f, 2.f * x) * x;
        dmix.t = prm[P_DELMIX] / 127.f;
        x = prm[P_DMOD1] / 127.f; dmod[0].t = x * x * 10.f;
        x = prm[P_DMOD2] / 127.f; dmod[1].t = x * x * 10.f;
        const int delSrc = (int)prm[P_DELSRC];
        const bool lfoSqr = prm[P_LFOWAV] >= 0.5f;
        x = prm[P_DRIVE] / 127.f;
        drive.t = std::pow(10.f, (std::pow(3.f, 2.f * x + 1.f) + 3.f) * 0.05f);
        dstmix.t = prm[P_DISTMIX] / 127.f;
        x = prm[P_VOLUME] / 127.f; vol.t = x * x;
        if (first) {
            for (int p = 0; p < 4; p++) { pair[p].sharp.snap(); pair[p].mod.snap(); pair[p].freq[0].snap(); pair[p].freq[1].snap(); }
            hold[0].snap(); hold[1].snap(); fA.snap(); fB.snap(); time[0].snap(); time[1].snap();
            fb.snap(); dmix.snap(); dmod[0].snap(); dmod[1].snap(); drive.snap(); dstmix.snap(); vol.snap();
            first = false;
        }
        const float k = kSm, msToSamp = sr * 0.001f, minDel = 1.45125f;
        // pair p listens to A (switch off) or B (switch on): "34 > 56", "78 > 12"
        static const int srcA[4] = {1, 0, 3, 2}, srcB[4] = {3, 0, 1, 2};

        // ---- audio rate ---------------------------------------------------
        for (int i = 0; i < n; i++) {
            // hyper LFO
            float a = fA.run(k), b = fB.run(k);
            float sqA = (lfoA < 0.25f || lfoA > 0.75f) ? 1.f : -1.f;
            float sqB = (lfoB < 0.25f || lfoB > 0.75f) ? 1.f : -1.f;
            float triL = 0.5f * ((std::fmin(lfoA, 1.f - lfoA) * 4.f - 1.f) + (std::fmin(lfoB, 1.f - lfoB) * 4.f - 1.f));
            float sqSum = 0.5f * (sqA + sqB);
            float sqrNow = orMode ? sqSum : sqA * sqB;
            float delLfo = lfoSqr ? sqSum : triL;
            lfoA += a / sr; if (lfoA >= 1.f) lfoA -= 1.f;
            lfoB += b * (1.f + link * 0.5f * sqA) / sr; if (lfoB >= 1.f) lfoB -= 1.f; if (lfoB < 0.f) lfoB += 1.f;

            // voices (pair outputs of the previous sample feed the cross modulation)
            float prevHp[4] = {pair[0].outHp, pair[1].outHp, pair[2].outHp, pair[3].outHp};
            float lfoSrc = totalFb * gTfbOn + sqrLfo * gTfbOff;
            sqrLfo = sqrNow;
            float h[2] = {hold[0].run(k), hold[1].run(k)};
            float mix = 0;
            for (int p = 0; p < 4; p++) {
                Pair &P = pair[p];
                float src = (prevHp[srcA[p]] * gSwOff + prevHp[srcB[p]] * gSwOn) * gPair[p] + lfoSrc * gLfo[p];
                float m = src * P.mod.run(k);
                float sharp = P.sharp.run(k);
                float vo = 0;
                if (vib != 0.f) {
                    vo = std::sin(kTwoPi * P.vibPh);
                    P.vibPh += P.vibInc; if (P.vibPh >= 1.f) P.vibPh -= 1.f;
                }
                float pw = 0.53125f + 0.025f * vo;
                float fmul = (1.f + 0.005f * vo) * (1.f + m);
                float sum = 0;
                for (int v = 0; v < 2; v++) {
                    float sq, tri, lev, th;
                    P.osc[v].run(P.freq[v].run(k) * fmul, pw, sq, tri);
                    P.sens[v].run(lev, th);
                    float env = h[p >> 1] + lev; if (env > 1.f) env = 1.f;
                    sum += (sq * sharp + tri * (1.f - sharp) + th) * env;
                }
                P.out = sum;
                P.outHp = P.hp.run(sum);
                mix += sum;
            }
            float in = mix * 0.16f;

            // dual modulated delay
            float fbk = fb.run(k), wet = 0;
            float xin = in + rnd() * 0.001f;
            for (int d = 0; d < 2; d++) {
                float self = selfLp[d].run(lastWrite[d] * 0.5f);
                float msrc = delSrc == 2 ? delLfo : (delSrc == 0 ? self : 0.f);
                float ms = time[d].run(k) + msrc * dmod[d].run(k);
                if (ms < minDel) ms = minDel;
                if (ms > 6000.f) ms = 6000.f;
                float r = dl[d].read(ms * msToSamp);
                r = expd[d].run(comp[d].run(dLp[d].run(dHp[d].run(r))));
                lastWrite[d] = xin + fbk * r;
                dl[d].write(lastWrite[d]);
                wet += r;
            }
            float dm = dmix.run(k);
            float del = 0.7f * in * (1.f - dm) + dm * tanhp(wet / (fbk > 1.5f ? fbk : 1.5f));

            // distortion + master
            float drv = drive.run(k);
            float t = tanhp(hp20.run(del * drv));
            float t2 = t * t, t4 = t2 * t2, t8 = t4 * t4, t16 = t8 * t8;
            float s = hp10.run(t + 0.25f * (t16 * t8 * t4 * t2 * t)) / clampf(drv, 1.f, 4.f);
            float mx = dstmix.run(k);
            float y = del * (1.f - mx) + (0.1f * del + s) * mx;
            totalFb = hp3.run(tanhp(y));
            float o = y * vol.run(k);
            if (!(o > -8.f && o < 8.f)) { o = 0; panic(); } // also catches NaN
            outL[i] = outR[i] = o;
        }
    }

private:
    float sr = 44100, kSm = 0.01f;
    Pair pair[4];
    int srcSel[4] = {1, 1, 1, 1};
    Smooth hold[2], fA, fB, time[2], fb, dmix, dmod[2], drive, dstmix, vol;
    float lfoA = 0, lfoB = 0, totalFb = 0, sqrLfo = 0;
    DelayLine dl[2];
    Hip dHp[2], hp20, hp10, hp3;
    Lop dLp[2], selfLp[2];
    Comp comp[2];
    Expander expd[2];
    float lastWrite[2] = {0, 0};
    uint8_t noteHeld[128];
    int noteCount[8];
    uint32_t seed = 0x1234567u;
    bool first = true;

    inline float rnd() { // uniform -1..1 (noise~)
        seed = seed * 1664525u + 1013904223u;
        return (float)(int32_t)seed * (1.f / 2147483648.f);
    }
    void panic() {
        for (int d = 0; d < 2; d++) {
            std::fill(dl[d].b.begin(), dl[d].b.end(), 0.f);
            dHp[d].last = dLp[d].last = selfLp[d].last = comp[d].env = lastWrite[d] = 0;
            expd[d].init(sr);
        }
        for (int p = 0; p < 4; p++) {
            pair[p].hp.last = pair[p].out = pair[p].outHp = 0;
            pair[p].osc[0].lp = pair[p].osc[1].lp = 0;
        }
        hp20.last = hp10.last = hp3.last = 0; totalFb = 0;
    }
};

} // namespace lira

/* Offline x86 test of lira_vst.cpp (test.sh builds it with ASan/UBSan): silence without a
 * sensor, a note sounds and releases, tuning against the patch's formula, QUANTIZE, sample-
 * accurate note start, every octave maps to the sensors, HOLD and the sensor latches drone
 * without notes, SHARP, cross FM, hyper LFO, delay tail, everything at maximum stays bounded,
 * value display, the 32 preset slots (program list, factory sounds, SAVE/LOAD, shared bank
 * file), chunk restore, a random parameter/MIDI stress run, NaN-free output.
 * With "bench" as the second argument it only measures CPU load; with "wav <file>" it renders
 * a demo. Prints PASSED/FAILED. */
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <dlfcn.h>
#include <unistd.h>

struct AEffect;
typedef intptr_t (*audioMasterCallback)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
struct AEffect {
    int32_t magic;
    intptr_t (*dispatcher)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
    void (*process)(AEffect *, float **, float **, int32_t);
    void (*setParameter)(AEffect *, int32_t, float);
    float (*getParameter)(AEffect *, int32_t);
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t resvd1, resvd2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void *object, *user;
    int32_t uniqueID, version;
    void (*processReplacing)(AEffect *, float **, float **, int32_t);
    void (*processDoubleReplacing)(AEffect *, double **, double **, int32_t);
    char future[56];
};
typedef struct { int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset; unsigned char midiData[4]; char detune, noteOffVelocity, reserved1, reserved2; } VstMidiEvent;
typedef struct { int32_t numEvents; intptr_t reserved; VstMidiEvent *events[2]; } VstEvents;
static intptr_t master(AEffect *, int32_t, int32_t, intptr_t, void *, float) { return 0; }

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)
static const int SR = 44100, BS = 128;
typedef AEffect *(*MainFn)(audioMasterCallback);
static MainFn g_main;

struct Host {
    AEffect *fx;
    std::vector<float> l, r;
    Host() { fx = g_main(master); fx->dispatcher(fx, 0, 0, 0, 0, 0); fx->dispatcher(fx, 10, 0, 0, 0, (float)SR); fx->dispatcher(fx, 11, 0, BS, 0, 0); fx->dispatcher(fx, 12, 0, 1, 0, 0); }
    ~Host() { fx->dispatcher(fx, 1, 0, 0, 0, 0); }
    int param(const char *name) {
        char buf[64];
        for (int i = 0; i < fx->numParams; i++) { buf[0] = 0; fx->dispatcher(fx, 8, i, 0, buf, 0); if (!std::strcmp(buf, name)) return i; }
        std::printf("FAIL: no parameter %s\n", name); fails++; return 0;
    }
    /* knobs 0..127; switches and lists by index with their number of positions */
    void set(const char *name, double v, int positions = 0) { fx->setParameter(fx, param(name), (float)(positions ? v / (positions - 1) : v / 127.0)); }
    std::string display(const char *name) { char buf[64] = {0}; fx->dispatcher(fx, 7, param(name), 0, buf, 0); return buf; }
    void press(const char *name) { int p = param(name); fx->setParameter(fx, p, 1); run(0.01); fx->setParameter(fx, p, 0); }
    void note(int n, bool on, int delta = 0) {
        VstMidiEvent m; std::memset(&m, 0, sizeof m);
        m.type = 1; m.byteSize = sizeof m; m.deltaFrames = delta;
        m.midiData[0] = on ? 0x90 : 0x80; m.midiData[1] = (unsigned char)n; m.midiData[2] = on ? 100 : 0;
        VstEvents e; e.numEvents = 1; e.reserved = 0; e.events[0] = &m;
        fx->dispatcher(fx, 25, 0, 0, &e, 0);
    }
    void run(double sec) {
        int blocks = (int)(sec * SR / BS);
        if (blocks < 1) blocks = 1;
        float bl[BS], br[BS], *o[2] = {bl, br};
        for (int b = 0; b < blocks; b++) { fx->processReplacing(fx, 0, o, BS); l.insert(l.end(), bl, bl + BS); r.insert(r.end(), br, br + BS); }
    }
    void clear() { l.clear(); r.clear(); }
    double rms(size_t from = 0) const { double s = 0; for (size_t i = from; i < l.size(); i++) s += (double)l[i] * l[i]; return std::sqrt(s / (l.size() - from + 1e-9)); }
    double peak() const { double p = 0; for (float x : l) if (std::fabs(x) > p) p = std::fabs(x); return p; }
    bool finite() const { for (float x : l) if (!std::isfinite(x)) return false; return true; }
    double freq(size_t from) const {
        int c = 0; size_t first = 0, last = 0;
        for (size_t i = from + 1; i < l.size(); i++) if (l[i - 1] < 0 && l[i] >= 0) { if (!c) first = i; last = i; c++; }
        return c > 1 ? (double)(c - 1) * SR / (double)(last - first) : 0;
    }
};
static double hf(const Host &h) { double s = 0; for (size_t i = SR; i + 1 < h.l.size(); i++) { double d = h.l[i + 1] - h.l[i]; s += d * d; } return std::sqrt(s / (h.l.size() - SR)); }
static double rel(const Host &x, const Host &y) { double d = 0, s = 0; for (size_t i = SR; i < x.l.size(); i++) { d += (double)(x.l[i] - y.l[i]) * (x.l[i] - y.l[i]); s += (double)y.l[i] * y.l[i]; } return std::sqrt(d / s); }

static void wav(const char *path, const std::vector<float> &l, const std::vector<float> &r) {
    FILE *f = std::fopen(path, "wb"); if (!f) return;
    uint32_t n = (uint32_t)l.size(), d = n * 4, u32; uint16_t u16;
    std::fwrite("RIFF", 1, 4, f); u32 = 36 + d; std::fwrite(&u32, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
    u32 = 16; std::fwrite(&u32, 4, 1, f); u16 = 1; std::fwrite(&u16, 2, 1, f); u16 = 2; std::fwrite(&u16, 2, 1, f);
    u32 = SR; std::fwrite(&u32, 4, 1, f); u32 = SR * 4; std::fwrite(&u32, 4, 1, f); u16 = 4; std::fwrite(&u16, 2, 1, f); u16 = 16; std::fwrite(&u16, 2, 1, f);
    std::fwrite("data", 1, 4, f); std::fwrite(&d, 4, 1, f);
    for (uint32_t i = 0; i < n; i++) { int16_t s[2] = {(int16_t)(std::fmax(-1.f, std::fmin(1.f, l[i])) * 32767), (int16_t)(std::fmax(-1.f, std::fmin(1.f, r[i])) * 32767)}; std::fwrite(s, 2, 2, f); }
    std::fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 2) { std::printf("usage: host_test plugin.so [bench | wav file]\n"); return 2; }
    char bank[64]; std::snprintf(bank, sizeof bank, "/tmp/lira8_test_bank_%d.txt", (int)getpid());
    std::remove(bank);
    setenv("LIRA8_PRESETS", bank, 1);
    void *h = dlopen(argv[1], RTLD_NOW);
    if (!h) { std::printf("FAILED: dlopen %s\n", dlerror()); return 1; }
    g_main = (MainFn)dlsym(h, "VSTPluginMain");
    if (!g_main) { std::printf("FAILED: no VSTPluginMain\n"); return 1; }

    if (argc > 2 && !std::strcmp(argv[2], "bench")) {
        struct { const char *what; int prog; int notes; } B[] = {{"idle (no sensor)", 0, 0}, {"8 voices, FM, delay, distortion", 2, 8}, {"total feedback, 8 voices", 5, 8}};
        for (auto &b : B) {
            Host x; x.fx->dispatcher(x.fx, 2, 0, b.prog, 0, 0); x.set("Delay Mix", 100); x.set("Dist Mix", 127);
            for (int n = 0; n < b.notes; n++) x.note(36 + n, true);
            x.run(1.0); x.clear();
            auto t0 = std::chrono::steady_clock::now(); x.run(20.0);
            double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("bench (this CPU, one core): %-34s %.2f %%\n", b.what, 100 * s / 20.0);
        }
        return 0;
    }
    if (argc > 3 && !std::strcmp(argv[2], "wav")) {
        Host x; x.fx->dispatcher(x.fx, 2, 0, 1, 0, 0); x.run(4.0);
        int seq[] = {36, 38, 41, 43, 37, 40};
        for (int n : seq) { x.note(n, true); x.run(1.5); }
        for (int n : seq) x.note(n, false);
        x.run(6.0); wav(argv[3], x.l, x.r); std::printf("wrote %s\n", argv[3]);
        return 0;
    }

    { /* shell */
        Host x; char s[64];
        CHECK(x.fx->magic == 0x56737450 && x.fx->numOutputs == 2 && (x.fx->flags & 0x100) && x.fx->numPrograms == 32, "AEffect header");
        CHECK(x.fx->numParams >= 58, "parameter count %d", (int)x.fx->numParams);
        x.param("Fast 12"); x.param("Quantize"); x.param("Sensor 8"); x.param("Preset"); x.param("Load"); x.param("Save");
        CHECK(x.display("Source 12") == "OFF" && x.display("Delay Mod Source") == "LFO" && x.display("Fast 12") == "ON", "start values: %s %s", x.display("Source 12").c_str(), x.display("Delay Mod Source").c_str());
        CHECK(x.display("Tune 2").find("Hz") != std::string::npos && x.display("Delay Time 1").find("ms") != std::string::npos, "display units: %s / %s", x.display("Tune 2").c_str(), x.display("Delay Time 1").c_str());
        CHECK(x.fx->dispatcher(x.fx, 51, 0, 0, (void *)"receiveVstMidiEvent", 0) == 1, "canDo");
        x.fx->dispatcher(x.fx, 29, 1, 0, s, 0); CHECK(!std::strcmp(s, "DEEP DRONE"), "program name %s", s);
        x.fx->dispatcher(x.fx, 29, 20, 0, s, 0); CHECK(!std::strcmp(s, "21 (empty)"), "empty slot name %s", s);
    }
    { /* silence, note, release */
        Host x; x.run(1.0);
        CHECK(x.peak() < 1e-4, "no sensor touched: not silent (peak %g)", x.peak());
        x.clear(); x.note(36, true); x.run(1.0);
        double on = x.rms(SR / 2);
        CHECK(on > 0.02 && x.finite(), "note 36 silent (rms %g)", on);
        x.clear(); x.note(36, false); x.run(1.0);
        CHECK(x.rms(SR / 2) < on * 0.01, "FAST release: still sounding (rms %g)", x.rms(SR / 2));
        Host s; s.set("Fast 12", 0, 2); s.note(36, true); s.run(1.0); s.note(36, false); s.clear(); s.run(2.0);
        CHECK(s.rms(SR) > 0.01, "slow release (8 s) ended too early (rms %g)", s.rms(SR));
        Host o; o.note(36 + 24, true); o.run(1.0);
        CHECK(std::fabs(o.rms(SR / 2) - on) < on * 0.05, "note 60 should be sensor 1 as well (rms %g vs %g)", o.rms(SR / 2), on);
    }
    { /* tuning */
        Host x; x.note(37, true); x.run(3.0);
        double want = 8.17579891564 * std::exp(0.0577622650467 * (-16 + 64.0 / 127 * 109)) * (0.01 + 64.0 / 127 * 1.99);
        CHECK(std::fabs(x.freq(SR) - want) < want * 0.004, "voice 2: %.2f Hz, patch formula %.2f Hz", x.freq(SR), want);
        Host q; q.set("Quantize", 1, 2); q.note(37, true); q.run(3.0);
        double semi = 12 * std::log2(q.freq(SR) / 440.0);
        CHECK(std::fabs(semi - std::round(semi)) < 0.03, "QUANTIZE: %.2f Hz is off the semitone grid", q.freq(SR));
        Host p; p.set("Pitch 1234", 127); p.note(37, true); p.run(3.0);
        CHECK(std::fabs(p.freq(SR) / x.freq(SR) - 2.0 / (0.01 + 64.0 / 127 * 1.99)) < 0.02, "PITCH 1234 ratio %.3f", p.freq(SR) / x.freq(SR));
    }
    { /* sample-accurate note start */
        Host x; x.run(0.1); x.clear(); x.note(36, true, 100); x.run(0.02);
        size_t first = 0; while (first < x.l.size() && std::fabs(x.l[first]) < 1e-7) first++;
        CHECK(first >= 100 && first < 104, "note with deltaFrames 100 starts at frame %zu", first);
    }
    { /* hold, latches */
        Host x; x.set("Hold 1234", 100); x.run(1.0);
        CHECK(x.rms(SR / 2) > 0.02, "HOLD 1234: no drone (rms %g)", x.rms(SR / 2));
        Host g; g.set("Sensor 1", 1, 2); g.run(1.0);
        CHECK(g.rms(SR / 2) > 0.02, "Sensor 1 latch: no sound (rms %g)", g.rms(SR / 2));
    }
    { /* timbre and modulation */
        Host a; a.note(36, true); a.note(37, true); a.run(2.0);
        Host b; b.set("Sharp 12", 127); b.note(36, true); b.note(37, true); b.run(2.0);
        CHECK(hf(b) > hf(a) * 2, "SHARP adds no highs (%g -> %g)", hf(a), hf(b));
        Host n; for (int k = 36; k < 40; k++) n.note(k, true); n.run(2.0);
        Host c; c.set("Source 12", 0, 3); c.set("Mod 12", 110); for (int k = 36; k < 40; k++) c.note(k, true); c.run(2.0);
        CHECK(rel(c, n) > 0.3 && c.finite(), "cross FM 34 -> 12 changes nothing (%g)", rel(c, n));
        Host l; l.set("Source 12", 2, 3); l.set("Mod 12", 110); l.note(36, true); l.note(37, true); l.run(2.0);
        CHECK(rel(l, a) > 0.3, "hyper LFO -> 12 changes nothing (%g)", rel(l, a));
    }
    { /* delay, bounds */
        Host x; x.set("Delay Mix", 127); x.set("Delay Time 1", 100); x.set("Delay Time 2", 104); x.set("Delay Feedback", 80);
        x.note(40, true); x.run(0.5); x.note(40, false); x.run(0.3); x.clear(); x.run(1.0);
        CHECK(x.rms() > 0.003, "no delay tail (rms %g)", x.rms());
        Host o; o.set("Delay Mix", 127); o.set("Delay Feedback", 127); o.set("Delay Time 1", 60); o.set("Delay Time 2", 70);
        o.set("Hold 1234", 127); o.set("Hold 5678", 127); o.set("Dist Drive", 127); o.set("Dist Mix", 127); o.set("Total FB", 1, 2);
        const char *src[] = {"Source 12", "Source 34", "Source 56", "Source 78"}, *mod[] = {"Mod 12", "Mod 34", "Mod 56", "Mod 78"};
        for (int p = 0; p < 4; p++) { o.set(src[p], 2, 3); o.set(mod[p], 127); }
        o.run(10.0);
        CHECK(o.finite() && o.peak() <= 0.981, "everything at maximum: peak %g", o.peak());
    }
    { /* preset slots: factory sounds, SAVE/LOAD, bank file shared by instances */
        bool all = true;
        for (int p = 0; p < 8; p++) {
            Host x; x.fx->dispatcher(x.fx, 2, 0, p, 0, 0); x.note(36, true); x.note(39, true); x.note(41, true); x.run(4.0);
            if (!x.finite() || x.peak() > 0.981 || x.rms(SR) < 0.01) { all = false; std::printf("  program %d: rms %g peak %g\n", p, x.rms(SR), x.peak()); }
        }
        CHECK(all, "a factory program is silent or out of bounds");
        Host a; a.set("Tune 3", 99); a.set("Source 56", 2, 3); a.set("Preset", 19, 32); a.press("Save");
        CHECK(a.display("Preset") == "Lira 20", "saved slot title: %s", a.display("Preset").c_str());
        a.set("Tune 3", 5); a.set("Source 56", 1, 3); a.run(0.01);
        a.press("Load"); a.run(0.01);
        CHECK(std::fabs(a.fx->getParameter(a.fx, a.param("Tune 3")) - 99 / 127.0) < 1e-3 && a.display("Source 56") == "LFO", "LOAD does not bring the saved sound back");
        Host b; b.fx->dispatcher(b.fx, 2, 0, 19, 0, 0); b.run(0.01);
        CHECK(std::fabs(b.fx->getParameter(b.fx, b.param("Tune 3")) - 99 / 127.0) < 1e-3, "second instance does not see the saved slot");
        Host e; e.set("Tune 3", 77); e.set("Preset", 30, 32); e.press("Load"); e.run(0.01);
        CHECK(std::fabs(e.fx->getParameter(e.fx, e.param("Tune 3")) - 77 / 127.0) < 1e-3, "loading an empty slot changed the sound");
    }
    { /* project chunk */
        Host a; a.fx->dispatcher(a.fx, 2, 0, 3, 0, 0); a.set("Tune 2", 99); a.run(0.01); a.clear();
        void *chunk = 0; intptr_t n = a.fx->dispatcher(a.fx, 23, 0, 0, &chunk, 0);
        Host b; b.fx->dispatcher(b.fx, 24, 0, n, chunk, 0);
        bool same = n > 0;
        const int pre = a.param("Preset"), ld = a.param("Load"), sv = a.param("Save");
        for (int i = 0; i < a.fx->numParams; i++) if (i != pre && i != ld && i != sv && std::fabs(a.fx->getParameter(a.fx, i) - b.fx->getParameter(b.fx, i)) > 1e-3) same = false;
        CHECK(same && b.fx->dispatcher(b.fx, 3, 0, 0, 0, 0) == 3, "chunk restore: parameters or slot differ");
        b.run(2.0);
        CHECK(b.rms(SR) > 0.01, "restored project is silent");
    }
    { /* stress */
        Host x; uint32_t s = 1; bool ok = true;
        for (int it = 0; it < 300 && ok; it++) {
            for (int k = 0; k < 6; k++) { s = s * 1664525u + 1013904223u; int p = (s >> 8) % x.fx->numParams; s = s * 1664525u + 1013904223u; x.fx->setParameter(x.fx, p, (s >> 8) / 16777216.f); }
            s = s * 1664525u + 1013904223u; x.note(36 + (s >> 10) % 8, (s >> 20) & 1);
            x.clear(); x.run(0.1); ok = x.finite() && x.peak() <= 0.981;
        }
        CHECK(ok, "random parameter/note run: output not finite or above the ceiling");
    }
    std::remove(bank);
    std::printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails ? 1 : 0;
}

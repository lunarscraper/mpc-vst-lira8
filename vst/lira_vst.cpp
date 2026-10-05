/* =============================================================================
 * lira_vst.cpp - LIRA 8: Mike Moreno's LIRA-8 (a Pure Data rebuild of the SOMA Lyra-8 signal
 * flow) as a VST2 instrument for the MPC OS plugin host (Force, MPC Live/One/X/Key), armhf.
 * The sound is lira_core.h; this file is the plug-in around it: parameters, MIDI (sample-
 * accurate; notes 36..43 and every other octave = sensors 1..8), the 32 preset slots with
 * LOAD/SAVE (pattern of mpc-vst-acid / mpc-vst-rattler) and the project chunk.
 * BSD-3-Clause (see ../LICENSE). "Lyra-8" belongs to SOMA Laboratory; no affiliation.
 * ========================================================================== */
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "params.h"
#include "popup.h"    /* mpc-vst-plugins wrapper/popup.h, copied into build/ by build.sh */
#include "lira_core.h"

/* ---- VST2 ABI (hand-written; no Steinberg SDK) ---------------------------- */
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
typedef struct { int32_t type, byteSize, deltaFrames, flags; char data[16]; } VstEvent;
typedef struct {
    int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset;
    unsigned char midiData[4];
    char detune, noteOffVelocity, reserved1, reserved2;
} VstMidiEvent;
typedef struct { int32_t numEvents; intptr_t reserved; VstEvent *events[2]; } VstEvents;

enum {
    effOpen = 0, effClose = 1, effSetProgram = 2, effGetProgram = 3, effSetProgramName = 4,
    effGetProgramName = 5, effGetProgramNameIndexed = 29, effGetParamLabel = 6, effGetParamDisplay = 7, effGetParamName = 8,
    effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12, effGetChunk = 23,
    effSetChunk = 24, effProcessEvents = 25, effCanBeAutomated = 26, effGetPlugCategory = 35,
    effGetEffectName = 45, effGetVendorString = 47, effGetProductString = 48,
    effGetVendorVersion = 49, effCanDo = 51, effGetVstVersion = 58,
};
enum { audioMasterAutomate = 0, audioMasterGetTime = 7, audioMasterUpdateDisplay = 42 };
enum { kVstTransportPlaying = 1 << 1, kVstPpqPosValid = 1 << 9, kVstTempoValid = 1 << 10, kVstTimeSigValid = 1 << 13 };
typedef struct {
    double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
    int32_t timeSigNumerator, timeSigDenominator, smpteOffset, smpteFrameRate, samplesToNextClock, flags;
} VstTimeInfo;
enum { effFlagsCanReplacing = 1 << 4, effFlagsProgramChunks = 1 << 5, effFlagsIsSynth = 1 << 8 };
enum { kVstMidiType = 1 };

/* the parameters, by key (module.json): the 55 of lira_core.h in its order (0..46 = the
 * parameter list of the original plugin), then the preset bank. IDX[] is their position in
 * the generated PARAMS[] */
enum { SLOT = lira::NUM_PARAMS, LOAD, SAVE, NKEYS };
static const char *key_name(int k) {
    static const char *const BANK[3] = {"slot", "load", "save"};
    return k < lira::NUM_PARAMS ? lira::kParams[k].key : BANK[k - lira::NUM_PARAMS];
}
static int IDX[NKEYS];
static int KEY_OF[NPARAMS];   /* PARAMS[] position -> key enum, -1 = not ours */
/* slot, load and save belong to the preset bank: never part of a chunk or a preset */
static bool is_bank_key(int i) { const int k = KEY_OF[i]; return k == SLOT || k == LOAD || k == SAVE; }

struct MidiEv { int32_t frame; uint8_t d[3]; };

struct Plugin {
    AEffect fx;
    audioMasterCallback master = nullptr;
    std::atomic<float> cache[NPARAMS];
    std::atomic<int> notify[NPARAMS];
    float open[NPARAMS] = {0};
    volatile int release[NPARAMS] = {0};
    bool down[NPARAMS] = {false};   /* LOAD/SAVE: the host currently reports them pressed */
    std::atomic<bool> dirty{true};
    std::atomic<int> cur{0};        /* selected preset slot, 0-based */
    lira::Lira core;
    float sr = 44100;
    MidiEv ev[256];
    int nev = 0;
    std::vector<uint8_t> chunk;
};

static int param_index(const char *key) {
    for (int i = 0; i < NPARAMS; i++) if (!std::strcmp(PARAMS[i].key, key)) return i;
    return -1;
}
static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static void copy_str(void *dst, const char *s, size_t max) {
    std::strncpy((char *)dst, s, max - 1);
    ((char *)dst)[max - 1] = 0;
}
static int norm_to_ui(const param_t *p, float n) {
    if (p->nopts) return (int)std::lround(clamp01(n) * (p->nopts - 1));
    return (int)std::lround(p->min + (p->max - p->min) * clamp01(n));
}
static float ui_to_norm(const param_t *p, double v) {
    if (p->nopts) return p->nopts > 1 ? clamp01((float)(v / (p->nopts - 1))) : 0.0f;
    return p->max > p->min ? clamp01((float)((v - p->min) / (p->max - p->min))) : 0.0f;
}
/* a knob is used at full resolution (the integer range only names its ends), a switch as its index */
static float val_at(Plugin *w, int i) {
    const param_t *p = &PARAMS[i];
    const float n = w->cache[i].load();
    if (p->nopts) return (float)norm_to_ui(p, n);
    return p->min + (p->max - p->min) * clamp01(n);
}
static float val(Plugin *w, int k) { return IDX[k] < 0 ? 0.0f : val_at(w, IDX[k]); }
static bool sw(Plugin *w, int k) { return val(w, k) > 0.5f; }
static void set_val(Plugin *w, int i, double v) {
    if (i < 0) return;
    w->cache[i].store(ui_to_norm(&PARAMS[i], v));
    w->notify[i].store(1);
}
static void start(Plugin *w, int k, double v) { set_val(w, IDX[k], v); }

struct NoDenormals {
#if defined(__arm__) && defined(__ARM_FP)
    uint32_t old = 0;
    NoDenormals() { asm volatile("vmrs %0, fpscr" : "=r"(old)); asm volatile("vmsr fpscr, %0" : : "r"(old | (1u << 24))); }
    ~NoDenormals() { asm volatile("vmsr fpscr, %0" : : "r"(old)); }
#elif defined(__x86_64__) || defined(__i386__)
    unsigned old = __builtin_ia32_stmxcsr();
    NoDenormals() { __builtin_ia32_ldmxcsr(old | 0x8040); }
    ~NoDenormals() { __builtin_ia32_ldmxcsr(old); }
#endif
};

/* audio thread: hand the parameters to the engine in the patch's own units (0..127, switch index) */
static void configure(Plugin *w) {
    for (int k = 0; k < lira::NUM_PARAMS; k++) w->core.prm[k] = val(w, k);
}

/* the start patch: the defaults of the original plugin (silent until a sensor is touched) */
static void start_values(Plugin *w) {
    for (int k = 0; k < lira::NUM_PARAMS; k++) start(w, k, lira::kParams[k].def);
}

/* ---- state as text: "key=value;" for every sound parameter. Used for the project chunk
 * and for the preset slots alike; restored by key, so parameters of later phases keep old
 * projects and presets loading (missing keys stay at their start values). ------------------ */
static std::string build_state(Plugin *w) {
    std::string t;
    char buf[96];
    for (int i = 0; i < NPARAMS; i++) {
        if (is_bank_key(i)) continue;
        std::snprintf(buf, sizeof buf, "%s=%.3f;", PARAMS[i].key, (double)val_at(w, i));
        t += buf;
    }
    return t;
}
static void apply_state(Plugin *w, const std::string &t, bool with_slot) {
    for (size_t pos = 0; pos < t.size();) {
        size_t semi = t.find(';', pos);
        if (semi == std::string::npos) break;
        std::string kv = t.substr(pos, semi - pos);
        pos = semi + 1;
        size_t eq = kv.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = kv.substr(0, eq);
        if (key == "prog") {   /* which slot a project had selected; never loads the slot */
            const int v = std::atoi(kv.c_str() + eq + 1);
            if (with_slot && v >= 0 && v < 32) w->cur.store(v);
            continue;
        }
        const int i = param_index(key.c_str());
        if (i >= 0 && !is_bank_key(i)) set_val(w, i, std::atof(kv.c_str() + eq + 1));
    }
    w->dirty.store(true);
}

/* ---------------------------------------------------------------------------
 * Preset bank (pattern of mpc-vst-acid): NSLOTS slots shared by every LIRA instance and
 * every project, kept in one text file on the SD card (one line per saved slot:
 * "index<TAB>name<TAB>state"). The slots are also the plugin's VST programs, so the host's
 * PRESET list shows and selects them; the PRESET knob + LOAD/SAVE buttons on the PRESET tab
 * do the same from the skin. The first slots come with factory sounds until SAVE overwrites
 * them (a factory sound is never written to the file).
 * ------------------------------------------------------------------------- */
#define NSLOTS 32
static std::mutex g_bank_lock;
static bool g_bank_loaded;
static std::string g_slot_chunk[NSLOTS], g_slot_name[NSLOTS], g_bank_path;

static const struct { const char *name, *state; } FACTORY[] = {
    {"INIT", "volume=110;"},
    {"DEEP DRONE", "hold1234=96;hold5678=80;fast12=0;fast34=0;fast56=0;fast78=0;tune1=40;tune2=47;tune3=22;tune4=30;tune5=18;tune6=25;tune7=10;tune8=14;src12=0;mod12=38;src56=0;mod56=30;vibrato=1;time1=96;time2=103;feedback=72;delmix=56;drive=40;distmix=40;volume=100;"},
    {"FM SWARM", "fast12=0;fast34=0;fast56=0;fast78=0;src12=0;src34=0;src56=0;src78=0;mod12=72;mod34=64;mod56=80;mod78=58;sharp12=40;sharp56=64;tune1=52;tune2=71;tune3=44;tune4=59;tune5=38;tune6=66;tune7=30;tune8=49;vibrato=1;time1=84;time2=90;delmix=40;feedback=60;volume=100;"},
    {"HYPER LFO", "hold1234=70;hold5678=70;src12=2;src34=2;src56=2;src78=2;mod12=76;mod34=60;mod56=84;mod78=52;lfoa=84;lfob=70;andor=1;link=1;tune1=36;tune2=50;tune3=28;tune4=41;sharp12=30;sharp34=30;delmix=30;volume=96;"},
    {"GHOST DELAY", "fast12=0;fast34=0;fast56=0;fast78=0;time1=100;time2=107;feedback=92;delmix=84;dmod1=46;dmod2=60;delsrc=0;tune1=60;tune2=74;tune3=55;tune4=67;drive=30;distmix=30;volume=100;"},
    {"TOTAL FEEDBACK", "totalfb=1;hold1234=60;hold5678=40;src12=2;src34=2;src56=2;src78=2;mod12=62;mod34=70;mod56=55;mod78=66;drive=100;distmix=110;sharp12=50;sharp78=80;time1=70;time2=75;feedback=80;delmix=50;volume=84;"},
    {"ORGAN CLUSTER", "quantize=1;tune1=56;tune2=64;tune3=50;tune4=59;tune5=49;tune6=57;tune7=40;tune8=46;vibrato=1;time1=80;time2=86;feedback=50;delmix=30;drive=20;distmix=20;volume=104;"},
    {"METAL PULSE", "sharp12=127;sharp34=110;sharp56=127;sharp78=96;switch=1;src12=0;src56=0;mod12=58;mod56=66;lfowav=1;lfoa=96;lfob=88;dmod1=70;dmod2=50;time1=40;time2=52;feedback=86;delmix=60;drive=90;distmix=90;volume=88;"},
};
enum { NFACTORY = (int)(sizeof FACTORY / sizeof FACTORY[0]) };

/* Next to the plugin's own folder rather than inside it, so reinstalling the plugin folder
 * doesn't take the presets with it; inside it if the parent can't be written. No dladdr:
 * /proc/self/maps has the path. LIRA8_PRESETS overrides the place (offline test). */
static std::string so_dir() {
    std::string dir;
    if (FILE *f = std::fopen("/proc/self/maps", "r")) {
        char line[1024];
        while (std::fgets(line, sizeof line, f)) {
            char *p = std::strstr(line, "/lira8.so");
            char *first = std::strchr(line, '/');
            if (!p || !first || first > p) continue;
            dir.assign(first, (size_t)(p - first));
            break;
        }
        std::fclose(f);
    }
    return dir;
}
static void bank_load_locked() {
    if (g_bank_loaded) return;
    g_bank_loaded = true;
    g_bank_path.clear();
    if (const char *env = std::getenv("LIRA8_PRESETS")) g_bank_path = env;
    else {
        std::string dir = so_dir(), cand[2];
        if (dir.empty()) dir = "/tmp";
        size_t cut = dir.rfind('/');
        cand[0] = (cut != std::string::npos && cut > 0 ? dir.substr(0, cut) : dir) + "/lira8_presets.txt";
        cand[1] = dir + "/lira8_presets.txt";
        for (const std::string &c : cand)             /* an existing file wins ... */
            if (FILE *f = std::fopen(c.c_str(), "r")) { std::fclose(f); g_bank_path = c; break; }
        for (int i = 0; i < 2 && g_bank_path.empty(); i++)   /* ... else the first place we may write */
            if (FILE *f = std::fopen(cand[i].c_str(), "a")) { std::fclose(f); g_bank_path = cand[i]; }
    }
    if (g_bank_path.empty()) return;
    if (FILE *f = std::fopen(g_bank_path.c_str(), "r")) {
        static char line[4096];
        while (std::fgets(line, sizeof line, f)) {
            line[std::strcspn(line, "\r\n")] = 0;
            char *t1 = std::strchr(line, '\t');
            char *t2 = t1 ? std::strchr(t1 + 1, '\t') : nullptr;
            int idx = std::atoi(line);
            if (!t2 || idx < 1 || idx > NSLOTS) continue;
            *t1 = *t2 = 0;
            g_slot_name[idx - 1] = t1 + 1;
            g_slot_chunk[idx - 1] = t2 + 1;
        }
        std::fclose(f);
    }
}
static bool bank_write_locked() {   /* whole file, via a temp file so a power cut can't leave half a bank */
    if (g_bank_path.empty()) return false;
    std::string tmp = g_bank_path + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "w");
    if (!f) return false;
    for (int i = 0; i < NSLOTS; i++)
        if (!g_slot_chunk[i].empty())
            std::fprintf(f, "%d\t%s\t%s\n", i + 1, g_slot_name[i].c_str(), g_slot_chunk[i].c_str());
    return std::fclose(f) == 0 && std::rename(tmp.c_str(), g_bank_path.c_str()) == 0;
}
static std::string slot_title(int i) {
    std::lock_guard<std::mutex> lk(g_bank_lock);
    char buf[32];
    if (g_slot_chunk[i].empty()) {
        if (i < NFACTORY) return FACTORY[i].name;
        std::snprintf(buf, sizeof buf, "%02d (empty)", i + 1);
        return buf;
    }
    if (!g_slot_name[i].empty()) return g_slot_name[i];
    std::snprintf(buf, sizeof buf, "Lira %02d", i + 1);
    return buf;
}
static void slot_load(Plugin *w) {
    const int n = clampi(w->cur.load(), 0, NSLOTS - 1);
    std::string c;
    { std::lock_guard<std::mutex> lk(g_bank_lock); c = g_slot_chunk[n]; }
    if (c.empty() && n < NFACTORY) c = FACTORY[n].state;
    if (c.empty()) return;              /* an empty slot leaves the current sound alone */
    start_values(w);
    apply_state(w, c, false);
}
static bool slot_save(Plugin *w) {
    const int n = clampi(w->cur.load(), 0, NSLOTS - 1);
    const std::string c = build_state(w);
    std::lock_guard<std::mutex> lk(g_bank_lock);
    g_slot_chunk[n] = c;
    return bank_write_locked();
}

/* ---- MIDI ------------------------------------------------------------------ */
static void midi(Plugin *w, const uint8_t *d) {
    const int st = d[0] & 0xf0, n = d[1] & 0x7f;
    if (st == 0x90 && d[2] > 0) w->core.noteOn(n);
    else if (st == 0x80 || st == 0x90) w->core.noteOff(n);
    else if (st == 0xb0 && (n == 120 || n == 123)) w->core.allNotesOff();
}

static void processReplacing(AEffect *e, float **in, float **out, int32_t n) {
    Plugin *w = (Plugin *)e->object;
    NoDenormals nd;
    if (w->dirty.exchange(false)) configure(w);
    float *L = out[0], *R = out[1];

    int pos = 0, k = 0;
    while (pos < n) {
        while (k < w->nev && w->ev[k].frame <= pos) midi(w, w->ev[k++].d);
        int f = n;
        if (k < w->nev) f = std::min(f, std::max(pos + 1, (int)w->ev[k].frame));
        w->core.process(L + pos, R + pos, f - pos);
        pos = f;
    }
    while (k < w->nev) midi(w, w->ev[k++].d);
    w->nev = 0;

    for (int c = 0; c < 2; c++) {
        float *y = out[c];
        for (int i = 0; i < n; i++) {
            const float a = std::fabs(y[i]);              /* output safety above -3 dBFS, ceiling 0.98 */
            if (a > 0.7f) {
                float t = std::min((a - 0.7f) / 0.3f, 3.0f), t2 = t * t;
                y[i] = std::copysign(0.7f + 0.28f * t * (27 + t2) / (27 + 9 * t2), y[i]);
            }
        }
    }
    bool any = false;
    for (int i = 0; i < NPARAMS; i++) {
        if (w->release[i]) { w->release[i] = 0; any = true; w->master(&w->fx, audioMasterAutomate, i, 0, 0, 0.0f); }
        if (!w->notify[i].exchange(0)) continue;
        any = true;
        w->master(&w->fx, audioMasterAutomate, i, 0, 0, w->cache[i].load());
    }
    if (any) w->master(&w->fx, audioMasterUpdateDisplay, 0, 0, 0, 0.0f);
}

static intptr_t process_events(Plugin *w, const VstEvents *evs) {
    if (!evs) return 0;
    for (int i = 0; i < evs->numEvents; i++) {
        const VstEvent *ev = evs->events[i];
        if (!ev || ev->type != kVstMidiType || w->nev >= 256) continue;
        const VstMidiEvent *m = (const VstMidiEvent *)ev;
        MidiEv &q = w->ev[w->nev++];
        q.frame = m->deltaFrames;
        q.d[0] = m->midiData[0]; q.d[1] = m->midiData[1]; q.d[2] = m->midiData[2];
    }
    std::stable_sort(w->ev, w->ev + w->nev, [](const MidiEv &a, const MidiEv &b) { return a.frame < b.frame; });
    return 1;
}

static void setParameter(AEffect *e, int32_t i, float n) {
    Plugin *w = (Plugin *)e->object;
    if (i < 0 || i >= NPARAMS) return;
    const param_t *p = &PARAMS[i];
    if (popup_set(w->open, i, n)) return;
    const int key = KEY_OF[i];
    if (key == SLOT) {   /* browsing only: LOAD loads, so turning the knob can't wipe what is playing */
        const int v = (int)std::lround(clamp01(n) * (NSLOTS - 1));
        if (v != w->cur.load()) { w->cur.store(v); w->master(&w->fx, audioMasterUpdateDisplay, 0, 0, 0, 0.0f); }
        return;
    }
    if (key == LOAD || key == SAVE) {
        const bool down = n > 0.5f;
        const bool rising = down && !w->down[i];   /* an echo of our own automate must not fire again */
        w->down[i] = down;
        if (rising) {
            if (key == LOAD) slot_load(w); else slot_save(w);
            w->release[i] = 1;
            w->master(&w->fx, audioMasterUpdateDisplay, 0, 0, 0, 0.0f);
        }
        return;
    }
    bool nudge = false;
    if (p->nopts > 1) {
        float pos = clamp01(n) * (p->nopts - 1);
        if (std::fabs(pos - std::round(pos)) > 0.001f) {
            float cur = w->cache[i].load() * (p->nopts - 1);
            n = (float)clampi((int)std::lround(cur) + (pos > cur ? 1 : -1), 0, p->nopts - 1) / (p->nopts - 1);
            nudge = true;
        }
    }
    w->cache[i].store(clamp01(n));
    w->dirty.store(true);
    if (!nudge) popup_picked(w->open, w->release, i);
}
static float getParameter(AEffect *e, int32_t i) {
    Plugin *w = (Plugin *)e->object;
    if (i < 0 || i >= NPARAMS) return 0.0f;
    const int key = KEY_OF[i];
    if (key == SLOT) return (float)w->cur.load() / (NSLOTS - 1);
    if (key == LOAD || key == SAVE) return 0.0f;   /* triggers always read released */
    if (popup_is(i)) return w->open[i];
    return w->cache[i].load();
}

/* project chunk: "LIRA1;" + the state + "prog=<slot>;" */
static intptr_t get_chunk(Plugin *w, void **ptr) {
    std::string t = "LIRA1;" + build_state(w);
    char buf[24];
    std::snprintf(buf, sizeof buf, "prog=%d;", w->cur.load());
    t += buf;
    w->chunk.assign(t.begin(), t.end());
    *ptr = w->chunk.data();
    return (intptr_t)w->chunk.size();
}
static intptr_t set_chunk(Plugin *w, const void *data, intptr_t len) {
    if (!data || len < 6) return 0;
    std::string t((const char *)data, (size_t)len);
    t.resize(std::strlen(t.c_str()));   /* stop at a NUL the host may have included */
    if (t.compare(0, 6, "LIRA1;")) return 0;
    apply_state(w, t.substr(6), true);
    return 1;
}

static void display(Plugin *w, int idx, char *buf, size_t n) {
    using namespace lira;
    const param_t *pp = &PARAMS[idx];
    const int key = KEY_OF[idx];
    if (key == SLOT) { std::snprintf(buf, n, "%s", slot_title(clampi(w->cur.load(), 0, NSLOTS - 1)).c_str()); return; }
    if (key == LOAD || key == SAVE) { buf[0] = 0; return; }
    if (popup_is(idx)) {
        const int u = norm_to_ui(pp, w->open[idx]);
        if (pp->nopts) std::snprintf(buf, n, "%s", pp->opts[u]); else std::snprintf(buf, n, "%d", u);
        return;
    }
    const int u = norm_to_ui(pp, w->cache[idx].load());
    if (pp->nopts) { std::snprintf(buf, n, "%s", pp->opts[u]); return; }
    const float v = val_at(w, idx);
    float hz = -1;
    if (key >= P_TUNE1 && key <= P_TUNE8) {
        const int voice = key - P_TUNE1;
        hz = Lira::voiceHz(voice, v, val(w, voice < 4 ? P_PITCH1234 : P_PITCH5678), sw(w, P_QUANTIZE));
    } else if (key == P_LFO_A || key == P_LFO_B) hz = Lira::lfoLaw(v);
    if (hz >= 0) {
        if (hz >= 1000) std::snprintf(buf, n, "%.2f kHz", hz / 1000);
        else if (hz >= 100) std::snprintf(buf, n, "%.0f Hz", hz);
        else if (hz >= 10) std::snprintf(buf, n, "%.1f Hz", hz);
        else std::snprintf(buf, n, "%.2f Hz", hz);
    } else if (key == P_TIME1 || key == P_TIME2) {
        const float ms = Lira::delayLaw(v);
        if (ms < 100) std::snprintf(buf, n, "%.1f ms", ms);
        else if (ms < 1000) std::snprintf(buf, n, "%.0f ms", ms);
        else std::snprintf(buf, n, "%.2f s", ms / 1000);
    } else if (key == P_PITCH1234 || key == P_PITCH5678) std::snprintf(buf, n, "x %.2f", 0.01f + v / 127.0f * 1.99f);
    else std::snprintf(buf, n, "%d", u);
}

static intptr_t dispatcher(AEffect *e, int32_t op, int32_t idx, intptr_t v, void *p, float o) {
    Plugin *w = (Plugin *)e->object;
    switch (op) {
    case effOpen: return 1;
    case effClose: delete w; return 1;
    case effSetProgram:
        if (v >= 0 && v < NSLOTS) {
            w->cur.store((int)v);
            slot_load(w);
            w->master(&w->fx, audioMasterUpdateDisplay, 0, 0, 0, 0.0f);
        }
        return 0;
    case effGetProgram: return w->cur.load();
    case effGetProgramName: if (p) copy_str(p, slot_title(clampi(w->cur.load(), 0, NSLOTS - 1)).c_str(), 24); return 0;
    case effGetProgramNameIndexed:
        if (idx < 0 || idx >= NSLOTS || !p) return 0;
        copy_str(p, slot_title(idx).c_str(), 24);
        return 1;
    case effSetProgramName: {   /* only a saved slot has a line in the file to carry the name */
        const int n = clampi(w->cur.load(), 0, NSLOTS - 1);
        std::lock_guard<std::mutex> lk(g_bank_lock);
        if (!p || g_slot_chunk[n].empty()) return 0;
        std::string nm((const char *)p);
        for (char &c : nm) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
        g_slot_name[n] = nm.substr(0, 23);
        bank_write_locked();
        return 0;
    }
    case effGetPlugCategory: return 2;   /* kPlugCategSynth */
    case effGetEffectName:
    case effGetProductString: copy_str(p, PLUG_NAME, 32); return 1;
    case effGetVendorString: copy_str(p, PLUG_VENDOR, 32); return 1;
    case effGetVendorVersion: return PLUG_VERSION;
    case effGetVstVersion: return 2400;
    case effCanBeAutomated: return idx >= 0 && idx < NPARAMS && !is_bank_key(idx);
    case effGetParamName: if (idx >= 0 && idx < NPARAMS) copy_str(p, PARAMS[idx].name, 32); return 1;
    case effGetParamLabel: if (idx >= 0 && idx < NPARAMS) copy_str(p, PARAMS[idx].unit, 8); return 1;
    case effGetParamDisplay: {
        if (idx < 0 || idx >= NPARAMS) return 0;
        char buf[32];
        display(w, idx, buf, sizeof buf);
        copy_str(p, buf, 24);
        return 1;
    }
    case effSetSampleRate:
        if (o > 0) { w->sr = o; w->core.setSampleRate(o); w->nev = 0; w->dirty.store(true); }
        return 1;
    case effSetBlockSize: return 1;
    case effMainsChanged: if (!v) { w->core.allNotesOff(); w->nev = 0; } return 1;
    case effProcessEvents: return process_events(w, (const VstEvents *)p);
    case effCanDo:
        if (p && (!std::strcmp((const char *)p, "receiveVstEvents") || !std::strcmp((const char *)p, "receiveVstMidiEvent"))) return 1;
        return -1;
    case effGetChunk: return get_chunk(w, (void **)p);
    case effSetChunk: return set_chunk(w, p, v);
    default: return 0;
    }
}

extern "C" __attribute__((visibility("default"))) AEffect *VSTPluginMain(audioMasterCallback master) {
    static std::once_flag once;
    std::call_once(once, [] {
        for (int i = 0; i < NPARAMS; i++) KEY_OF[i] = -1;
        for (int k = 0; k < NKEYS; k++) { IDX[k] = param_index(key_name(k)); if (IDX[k] >= 0) KEY_OF[IDX[k]] = k; }
    });
    { std::lock_guard<std::mutex> lk(g_bank_lock); bank_load_locked(); }
    Plugin *w = new Plugin();
    w->master = master;
    for (int i = 0; i < NPARAMS; i++) { w->cache[i].store(PARAMS[i].def); w->notify[i].store(0); }
    start_values(w);
    w->core.setSampleRate(w->sr);
    AEffect *e = &w->fx;
    std::memset(e, 0, sizeof *e);
    e->magic = 0x56737450;
    e->dispatcher = dispatcher;
    e->setParameter = setParameter;
    e->getParameter = getParameter;
    e->processReplacing = processReplacing;
    e->numParams = NPARAMS;
    e->numPrograms = NSLOTS;
    e->numInputs = 0;
    e->numOutputs = 2;
    e->flags = effFlagsCanReplacing | effFlagsProgramChunks | effFlagsIsSynth;
    e->uniqueID = PLUG_UID;
    e->version = PLUG_VERSION;
    e->object = w;
    return e;
}

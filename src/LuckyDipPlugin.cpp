// Lucky Dip for MPC / Force: the native front end (framework/mpc_plugin.h) around the unchanged Lucky Dip engine (src/engine.cpp:
// sample library scan, 16 pads filled at random, one-shot playback, MPC program export).
//
// Signal flow: host MIDI (sample accurate) -> engine midi (pad notes 0-15 and 36-51) -> engine render (44.1 kHz int16 stereo) ->
// resampled to the host rate when it is not 44.1 kHz -> float outputs. The engine's render and MIDI paths are lock-free (pad triggers
// are atomics, sample buffers are swapped atomically), so the audio thread never takes the front end's lock.
//
// Parameters: the 0.9.2 wrapper build's 161 in the same order (vst/params.json -> tools/gen_params.py -> params_gen.h), then
// 1.0.0's appended pad1_kind .. pad16_kind, sel_kind (kKind: what each pad holds, for the skin's category pictures).
//   * values and option lists (kPolled): a host change goes to the engine as its own set_param string; the worker reads them back,
//     because the engine changes some by itself (the selected pad follows the pad played, its gain follows the selection).
//     Whole numbers and options settle toward the direction of a Q-Link move, so a small turn moves one step (the 1.x rule).
//   * toggles with a label (kToggle: pad locks, category keys): the value is the engine's "<key>_on", the text its label.
//   * momentary verbs (kMomentary): 0 -> 1 fires once, then the param drops back to 0. PAD < > (kStep) nudge the selected pad.
//   * readouts (kText): the value text is the engine's text; the value is a counter bumped when the text changes, because the host
//     only re-reads a param's text when its value moves.
// Parameter 0 is the status readout, so the host's nudge of parameter 0 on every prepare is harmless.
// State: the engine's own "state" text ("LD1" + settings + one line per pad) is the truth; it travels as the MPC chunk's extra state,
// and nothing is written to the engine while the MPC chunk restores parameter values. A project saved by the 1.x wrapper build stored
// only that text (NUL-terminated) as its chunk: it is still read (effSetChunk is wrapped below).
// Files: the engine keeps index.cache, prefs.txt and the optional luckydip.conf in <plugin folder>/luckydip, as 1.x did.

#include "mpc_plugin.h"

extern "C" {
#include "engine.h"   // the 1.x engine interface (C linkage, defined in src/engine.cpp)
}
#include "params_gen.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef LD_NAME
#define LD_NAME "Lucky Dip"
#endif
#ifndef LD_UID
#define LD_UID 'L', 'k', 'D', 'p'
#endif
#define LD_FOURCC(...) MPC_FOURCC(__VA_ARGS__)   // expands LD_UID before MPC_FOURCC counts its arguments
#ifndef LD_VERSION
#define LD_VERSION 1000
#endif

namespace {

const int kChunk = 128;
const int kStateMax = 8192;
const double kEngineRate = 44100.0;

mpc::PluginInfo makeInfo()
{
    mpc::PluginInfo i;
    i.name = LD_NAME;
    i.vendor = "sd88me";
    i.uniqueId = LD_FOURCC(LD_UID);
    i.version = LD_VERSION;
    i.isInstrument = true;
    i.numInputs = 0;
    i.numOutputs = 2;
    return i;
}

long long nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

uint32_t fnv(const char* s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

bool isState(const std::string& s) { return s.compare(0, 4, "LD1\n") == 0 || s == "LD1"; }

thread_local int t_pushing = -1;   // the parameter the worker is telling the host about right now (its echo is ignored)

} // namespace

class LuckyDipPlugin : public mpc::Plugin
{
public:
    LuckyDipPlugin() : mpc::Plugin(makeInfo())
    {
        for (int i = 0; i < kNumParams; ++i)
        {
            const PSpec& s = g_pspecs[i];
            mpc::Param p = (s.flags & kChoice)
                ? mpc::choiceParam(s.key, s.name, std::vector<std::string>(s.choices, s.choices + s.nchoices), (int)s.def)
                : mpc::intParam(s.key, s.name, (int)s.min, (int)s.max, (int)s.def);
            p.automatable = !(s.flags & kNoAuto);
            addParam(p);
        }
        const size_t n = (size_t)kNumParams;
        held_ = new std::atomic<bool>[n];
        release_ = new std::atomic<bool>[n];
        hostMoved_ = new std::atomic<bool>[n];
        hostAt_ = new std::atomic<long long>[n];
        told_ = new std::atomic<float>[n];
        for (size_t i = 0; i < n; ++i)
        {
            held_[i].store(false); release_[i].store(false); hostMoved_[i].store(false);
            hostAt_[i].store(0); told_[i].store(-1.0e9f);
        }
        lastPos_.assign(n, -1.0f);
        textHash_.assign(n, 0u);

        // 1.x: MODULE_SUBDIR "luckydip" inside the plugin folder. (Not mpc::contentPath(): a function-local static std::string.)
        dataDir_ = mpc::detail::computePluginFolder() + "/luckydip";
        eng_ = mpc_engine();
        inst_ = eng_->create(dataDir_.c_str());
        worker_ = std::thread(&LuckyDipPlugin::workerMain, this);
    }

    ~LuckyDipPlugin() override
    {
        quit_.store(true);
        if (worker_.joinable()) worker_.join();
        if (inst_) eng_->destroy(inst_);
        delete[] held_;
        delete[] release_;
        delete[] hostMoved_;
        delete[] hostAt_;
        delete[] told_;
    }

    void prepare(double sampleRate, int) override
    {
        step_ = sampleRate > 0 ? kEngineRate / sampleRate : 1.0;
        if (std::fabs(step_ - 1.0) < 1e-9) step_ = 1.0;
        frac_ = 0.0; have_ = 0; pos_ = 0;
    }

    void reset() override { closeList_.store(true); notifyAll_.store(true); }

    // ---- audio ----------------------------------------------------------------------------------------------------------------
    void process(mpc::ProcessContext& ctx) override
    {
        float* L = ctx.outputs[0];
        float* R = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;
        if (!inst_) { for (int i = 0; i < ctx.numFrames; ++i) { L[i] = 0; if (R) R[i] = 0; } return; }
        int frame = 0, mi = 0;
        while (frame < ctx.numFrames)
        {
            while (mi < ctx.numMidiEvents && ctx.midi[mi].sampleOffset <= frame)
            {
                const mpc::MidiEvent& e = ctx.midi[mi++];
                eng_->midi(inst_, e.data, e.size);
            }
            int end = ctx.numFrames;
            if (mi < ctx.numMidiEvents) end = std::min(std::max(ctx.midi[mi].sampleOffset, frame + 1), ctx.numFrames);
            if (step_ == 1.0)
                while (frame < end)
                {
                    const int n = std::min(end - frame, kChunk);
                    eng_->render(inst_, pcm_, n);
                    for (int i = 0; i < n; ++i)
                    {
                        L[frame + i] = pcm_[2 * i] * (1.0f / 32768.0f);
                        if (R) R[frame + i] = pcm_[2 * i + 1] * (1.0f / 32768.0f);
                    }
                    frame += n;
                }
            else
                for (; frame < end; ++frame)   // linear interpolation between engine frames a and a + 1 (prev_ holds a)
                {
                    while (frac_ >= 1.0 || have_ < 2)
                    {
                        if (pos_ >= kChunk) { eng_->render(inst_, pcm_, kChunk); pos_ = 0; }
                        prev_[0] = next_[0]; prev_[1] = next_[1];
                        next_[0] = pcm_[2 * pos_] * (1.0f / 32768.0f);
                        next_[1] = pcm_[2 * pos_ + 1] * (1.0f / 32768.0f);
                        ++pos_;
                        if (have_ < 2) ++have_; else frac_ -= 1.0;
                    }
                    const float t = (float)frac_;
                    L[frame] = prev_[0] + (next_[0] - prev_[0]) * t;
                    if (R) R[frame] = prev_[1] + (next_[1] - prev_[1]) * t;
                    frac_ += step_;
                }
        }
    }

    // ---- parameters -----------------------------------------------------------------------------------------------------------
    void parameterChanged(int index, float plain) override
    {
        if (index < 0 || index >= kNumParams || !inst_) return;
        if (t_pushing == index) return;          // the host echoing what the worker just told it
        const PSpec& s = g_pspecs[index];
        if (s.flags & kLocal) { localChanged(index, plain); return; }
        if (s.flags & (kText | kKind)) return;   // a readout's counter or a kind picture: nothing to send
        hostAt_[index].store(nowMs());
        if (loading_.load()) return;             // an MPC chunk is restoring values: the engine state follows as extra state
        if (s.flags & kMomentary)
        {
            if (plain < 0.5f) { held_[index].store(false); return; }
            if (held_[index].exchange(true)) return;
            if (s.flags & kStep) stepTarget(index);
            else engineSet(s.key, "1");
            release_[index].store(true);
            return;
        }
        if (s.flags & kToggle)
        {
            const bool on = plain >= 0.5f;
            told_[index] = on ? 1.0f : 0.0f;
            engineSet(s.key, on ? "1" : "0");
            hostMoved_[index].store(true);       // read "<key>_on" back: the engine may refuse or normalise it
            return;
        }
        // whole numbers and options: settle toward the move, then send
        const float span = s.max - s.min;
        float v = plain;
        if (span > 0)
        {
            const float pos = getNormalised(index) * span;
            const float cur = told_[index] > -1.0e8f ? told_[index] - s.min : pos;
            float steps = settle(pos, cur, lastPos_[(size_t)index]);
            lastPos_[(size_t)index] = pos;
            steps = std::min(std::max(steps, 0.0f), span);
            v = s.min + steps;
            // Where it landed reaches the host from the worker ~10 ms later, so a set reads back unchanged until then.
            if (std::fabs(getNormalised(index) * span - steps) > 0.001f) hostMoved_[index].store(true);
        }
        char buf[24];
        std::snprintf(buf, sizeof buf, "%d", (int)std::lround(v));
        told_[index] = v;
        engineSet(s.key, buf);
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        (void)plain;
        if (index < 0 || index >= kNumParams) return false;
        const PSpec& s = g_pspecs[index];
        char buf[128];
        if ((s.flags & kLocal) && (s.flags & kText)) { std::lock_guard<std::mutex> g(listMu_); out = listText_[(size_t)(index - kListTitle)]; return true; }
        if (s.flags & (kText | kToggle)) { out = engineGet(s.key, buf, sizeof buf) ? buf : ""; return true; }   // readouts and key labels
        if (s.flags & kMomentary) { out = ""; return true; }
        return false;
    }

    // ---- state ----------------------------------------------------------------------------------------------------------------
    void saveExtraState(std::vector<uint8_t>& out) override
    {
        const std::string s = engineState();
        out.assign(s.begin(), s.end());
    }

    void loadExtraState(const uint8_t* data, size_t size) override
    {
        if (!data || !size) return;
        applyState(std::string(reinterpret_cast<const char*>(data), strnlen(reinterpret_cast<const char*>(data), size)));
    }

    // A project saved by the 1.x wrapper build: its chunk is the engine's state text, NUL-terminated. Called from the wrapped effSetChunk.
    bool loadLegacy(const char* data, size_t size)
    {
        if (!data || !size) return false;
        const std::string s(data, strnlen(data, size));
        if (!isState(s)) return false;
        applyState(s);
        return true;
    }

    std::atomic<bool> loading_{false};   // set around an MPC chunk load (wrapped effSetChunk)
    void requestResync() { resync_.store(true); }

private:
    void engineSet(const char* key, const char* val)
    {
        std::lock_guard<std::mutex> l(lock_);
        eng_->set_param(inst_, key, val);
    }

    bool engineGet(const char* key, char* buf, int n)
    {
        int r;
        { std::lock_guard<std::mutex> l(lock_); r = eng_->get_param(inst_, key, buf, n); }
        if (r <= 0) { buf[0] = 0; return false; }
        buf[r < n ? r : n - 1] = 0;
        return true;
    }

    std::string engineState()
    {
        std::vector<char> buf((size_t)kStateMax);
        return inst_ && engineGet("state", buf.data(), kStateMax) ? std::string(buf.data()) : std::string();
    }

    void applyState(const std::string& s)
    {
        if (inst_ && isState(s)) engineSet("state", s.c_str());
        resync_.store(true);
    }

    // Set a parameter's value without calling parameterChanged (so it is not written back to the engine).
    void store(int i, float plain) { values_[(size_t)i]->store(param(i).toNormalised(plain), std::memory_order_relaxed); }

    // Where a whole-number / option param lands, in steps from its minimum (the 1.x wrapper's rule): a value on a step is a click,
    // a preset or automation; between steps it is a Q-Link / wheel / drag move, rounded toward the way it moves.
    static float settle(float pos, float cur, float last)
    {
        if (std::fabs(pos - std::round(pos)) <= 0.001f) return std::round(pos);
        const float dir = (last >= 0 && std::fabs(pos - last) < 0.5f) ? pos - last : pos - cur;
        if (dir == 0) return std::round(cur);
        return dir > 0 ? std::ceil(pos - 0.001f) : std::floor(pos + 0.001f);
    }

    void stepTarget(int index)   // PAD < >: nudge the selected pad from the engine's current value (clamped, as in 1.x)
    {
        const PSpec& s = g_pspecs[index];
        const PSpec& t = g_pspecs[s.stepTarget];
        char buf[32];
        if (!engineGet(t.key, buf, sizeof buf)) return;
        const int v = std::min(std::max(std::atoi(buf) + (int)s.stepDelta, (int)t.min), (int)t.max);
        std::snprintf(buf, sizeof buf, "%d", v);
        engineSet(t.key, buf);
        hostMoved_[s.stepTarget].store(true);
        selMoved_.store(true);
    }

    // ---- the SETTINGS folder pick-list (kLocal) -----------------------------------------------------------------------------------
    // ui_open k (1..4) shows the list of folder choice k - 1 (the engine's fl_* keys); 12 rows a page. A tap on a row (list_sel) picks
    // that folder and closes the list; list_sel otherwise shows the current folder's row (12 = not on this page).
    void localChanged(int index, float plain)
    {
        if (index == kUiOpen)
        {
            const int w = (int)std::lround(plain);
            listWhich_.store(w);
            if (w > 0)
            {
                char k[24], b[32];
                std::snprintf(k, sizeof k, "fl_cur:%d", w - 1);
                const int cur = engineGet(k, b, sizeof b) ? std::atoi(b) : 0;
                listPage_.store(cur / kListRows);
            }
            listDirty_.store(true);
            return;
        }
        if (index == kListSel)
        {
            const int row = (int)std::lround(plain), w = listWhich_.load();
            if (row < 0 || row >= kListRows || w <= 0 || row == listLit_.load()) return;
            char k[24], b[24];
            std::snprintf(k, sizeof k, "fl_pick:%d", w - 1);
            std::snprintf(b, sizeof b, "%d", listPage_.load() * kListRows + row);
            engineSet(k, b);
            closeList_.store(true);
            return;
        }
        if (index == kListPrev || index == kListNext)
        {
            if (plain < 0.5f) { held_[index].store(false); return; }
            if (held_[index].exchange(true)) return;
            release_[index].store(true);
            listPage_.store(std::max(0, listPage_.load() + (index == kListNext ? 1 : -1)));   // clamped when the rows are built
            listDirty_.store(true);
        }
    }

    // worker thread: close, rebuild the rows, tell the host
    void listTick()
    {
        if (closeList_.exchange(false))
        {
            listWhich_.store(0);
            if (get(kUiOpen) > 0.5f || told_[kUiOpen] != 0.0f) pushValue(kUiOpen, 0.0f);
            listDirty_.store(true);
        }
        if (!listDirty_.exchange(false)) return;
        const int w = listWhich_.load();
        std::vector<std::string> t((size_t)kListRows + 2);
        int lit = kListRows;
        if (w > 0)
        {
            static const char* const titles[] = {"SAMPLE SOURCE: LOCATION", "SAMPLE SOURCE: INSIDE", "EXPORT FOLDER: LOCATION", "EXPORT FOLDER: INSIDE"};
            char k[32], b[256];
            std::snprintf(k, sizeof k, "fl_count:%d", w - 1);
            const int n = engineGet(k, b, sizeof b) ? std::atoi(b) : 0;
            std::snprintf(k, sizeof k, "fl_cur:%d", w - 1);
            const int cur = engineGet(k, b, sizeof b) ? std::atoi(b) : 0;
            const int pages = std::max(1, (n + kListRows - 1) / kListRows);
            const int page = std::min(listPage_.load(), pages - 1);
            listPage_.store(page);
            t[0] = titles[w - 1];
            t[1] = pages > 1 ? std::to_string(page + 1) + " / " + std::to_string(pages) : std::string();
            for (int r = 0; r < kListRows; ++r)
            {
                const int i = page * kListRows + r;
                if (i >= n) continue;
                std::snprintf(k, sizeof k, "fl_item:%d:%d", w - 1, i);
                t[(size_t)r + 2] = engineGet(k, b, sizeof b) ? b : "";
                if (i == cur) lit = r;
            }
        }
        listLit_.store(lit);
        for (int j = 0; j < kListRows + 2; ++j)
        {
            bool changed;
            { std::lock_guard<std::mutex> g(listMu_); changed = listText_[(size_t)j] != t[(size_t)j]; if (changed) listText_[(size_t)j] = t[(size_t)j]; }
            if (changed) { counter_ = (counter_ + 1) % 1024; pushValue(kListTitle + j, (float)counter_); }
        }
        pushValue(kListSel, (float)lit);
        wantDisplay_ = true;
    }

    int kindOf(const char* catKey, const char* nameKey)
    {
        static const char* const cats[] = {"kick", "snare", "rim", "clap", "hat", "closed_hat", "open_hat", "tom", "conga", "percussion",
                                           "crash", "ride", "cymbal", "fx", "glitch", "vox", "bass", "synth", "stab", "chord", "lead", "pad", "other"};
        char buf[256];
        if (engineGet(nameKey, buf, sizeof buf) && buf[0] == '!' && buf[1] == ' ') return 24;   // the engine's "missing sample" mark
        if (!engineGet(catKey, buf, sizeof buf)) return 0;
        for (int k = 0; k < 23; ++k) if (!std::strcmp(buf, cats[k])) return k + 1;
        return 0;   // "-": empty
    }

    // ---- worker: momentary releases, everything the engine changes by itself -----------------------------------------------------
    void pushValue(int i, float plain)
    {
        told_[i] = plain;
        store(i, plain);
        t_pushing = i;
        notifyHostParameterChanged(i);
        t_pushing = -1;
    }

    void workerMain()
    {
        while (!quit_.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            tickOnce(false);
        }
    }

    // One worker pass. force = the offline test: read back everything now.
    void tickOnce(bool force)
    {
        if (!inst_) return;
        const long long now = nowMs();
        ++tick_;
        for (int i = 0; i < kNumParams; ++i)
            if (release_[i].exchange(false)) { held_[i].store(false); pushValue(i, 0.0f); }
        bool full = force || tick_ % 3 == 0;   // values: every 30 ms (engine gets are a short lock and a few string compares)
        if (resync_.exchange(false) || notifyAll_.exchange(false))
        {
            closeList_.store(true);   // a project never opens with a pick-list showing
            for (int i = 0; i < kNumParams; ++i) { told_[i].store(-1.0e9f); hostMoved_[i].store(true); }
            std::fill(textHash_.begin(), textHash_.end(), 0u);
            full = true;
        }
        char buf[256];
        int changed = 0;
        bool selChanged = selMoved_.exchange(false);
        for (int i = 0; i < kNumEngineParams; ++i)
        {
            const PSpec& s = g_pspecs[i];
            if (!(s.flags & kPolled)) continue;
            const bool moved = hostMoved_[i].exchange(false);
            if (!(moved || full)) continue;
            // while the host is still moving it, don't send the engine's value back under the finger; read it back once it has rested
            if (!force && now - hostAt_[i].load() < 300) { if (moved) hostMoved_[i].store(true); continue; }
            float v;
            if (s.flags & kToggle)
            {
                char k2[64];
                std::snprintf(k2, sizeof k2, "%s_on", s.key);
                if (!engineGet(k2, buf, sizeof buf)) continue;
                v = std::atoi(buf) ? 1.0f : 0.0f;
            }
            else
            {
                if (!engineGet(s.key, buf, sizeof buf)) continue;
                v = std::min(std::max((float)std::atof(buf), s.min), s.max);
            }
            if (moved || v != told_[i])
            {
                if (!moved && !std::strcmp(s.key, "sel_pad")) selChanged = true;
                pushValue(i, v);
                if (!moved) ++changed;
            }
        }

        listTick();

        // what each pad holds, for the skin's category pictures (0 empty, 1..23 category, 24 missing)
        if (full || selChanged)
            for (int k = 0; k <= 16; ++k)
            {
                const int i = kFirstKind + k;
                char kc[24], kn[24];
                if (k < 16) { std::snprintf(kc, sizeof kc, "pad%d_cat", k + 1); std::snprintf(kn, sizeof kn, "pad%d_name", k + 1); }
                else { std::snprintf(kc, sizeof kc, "sel_cat"); std::snprintf(kn, sizeof kn, "sel_name"); }
                const float v = (float)kindOf(kc, kn);
                if (v != told_[i]) pushValue(i, v);
            }

        // readouts: every 100 ms (and right after a selection change); a changed text bumps the counter (the host re-reads text when
        // the value moves)
        bool textChanged = false;
        if (force || full || selChanged || tick_ % 10 == 0)
            for (int i = 0; i < kNumEngineParams; ++i)
            {
                const PSpec& s = g_pspecs[i];
                if (!(s.flags & kText)) continue;
                const uint32_t h = engineGet(s.key, buf, sizeof buf) ? fnv(buf) | 1u : 1u;
                if (h == textHash_[(size_t)i]) continue;
                textHash_[(size_t)i] = h;
                textChanged = true;
                counter_ = (counter_ + 1) % 1024;
                pushValue(i, (float)counter_);
            }
        // Many values at once, a new selection (the category keys' labels follow it) or new text: one display update, at most every
        // 100 ms. Never periodically: it re-lists programs and closes the PRESET menu.
        if (textChanged || selChanged || changed > 8) wantDisplay_ = true;
        if (wantDisplay_ && (force || now - lastDisplay_ >= 100))
        {
            wantDisplay_ = false;
            lastDisplay_ = now;
            requestDisplayUpdate();
        }
    }

public:
#ifdef LD_TEST
    void dbgTick() { tickOnce(true); }
    std::string dbgEngine(const char* key)
    {
        char b[kStateMax];
        return engineGet(key, b, sizeof b) ? std::string(b) : std::string();
    }
    void dbgEngineSet(const char* key, const char* v) { engineSet(key, v); }
#endif

private:
    const mpc_engine_t* eng_ = nullptr;
    void* inst_ = nullptr;
    std::string dataDir_;
    std::mutex lock_;   // one engine get/set at a time (never taken on the audio thread)
    std::thread worker_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> resync_{true}, notifyAll_{false}, selMoved_{false};
    std::atomic<bool>* held_ = nullptr;
    std::atomic<bool>* release_ = nullptr;
    std::atomic<bool>* hostMoved_ = nullptr;   // read this one back and tell the host on the next pass
    std::atomic<long long>* hostAt_ = nullptr; // when the host last moved it (ms), per parameter
    std::atomic<float>* told_ = nullptr;       // the value the host was last told (or sent), per parameter
    std::vector<float> lastPos_;               // the host's last position, for settle()
    std::vector<uint32_t> textHash_;
    std::mutex listMu_;
    std::vector<std::string> listText_ = std::vector<std::string>((size_t)kListRows + 2);   // list_title, list_page, list_1 ..
    std::atomic<int> listWhich_{0}, listPage_{0}, listLit_{kListRows};
    std::atomic<bool> listDirty_{false}, closeList_{false};
    // audio thread: the engine's 44.1 kHz stream, resampled when the host runs at another rate
    int16_t pcm_[kChunk * 2] = {0};
    double step_ = 1.0, frac_ = 0.0;
    int pos_ = kChunk, have_ = 0;
    float prev_[2] = {0, 0}, next_[2] = {0, 0};
    int counter_ = 0;
    long tick_ = 0;
    bool wantDisplay_ = false;
    long long lastDisplay_ = 0;
};

// The same entry as MPC_PLUGIN_ENTRY, plus a wrapped effSetChunk: an "MPCP" chunk loads with engine writes held back (the engine
// state in its extra part is the truth); a 1.x chunk (the engine's "LD1 ..." text) goes to the engine as before.
namespace {
VstIntPtr (*g_origDispatcher)(AEffect*, VstInt32, VstInt32, VstIntPtr, void*, float) = nullptr;

VstIntPtr ldDispatcher(AEffect* e, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt)
{
    if (opcode == effSetChunk && e->object && ptr && value > 0)
    {
        LuckyDipPlugin* p = static_cast<LuckyDipPlugin*>(e->object);
        if (value >= 4 && std::memcmp(ptr, "MPCP", 4) == 0)
        {
            p->loading_.store(true);
            const VstIntPtr r = g_origDispatcher(e, opcode, index, value, ptr, opt);
            p->loading_.store(false);
            p->requestResync();
            return r;
        }
        return p->loadLegacy(static_cast<const char*>(ptr), (size_t)value) ? 1 : 0;
    }
    return g_origDispatcher(e, opcode, index, value, ptr, opt);
}
} // namespace

#ifdef LD_TEST   // offline test hooks (tests/host_test.cpp); absent from the release build
extern "C" MPC_EXPORT void ld_dbg_tick(AEffect* e) { static_cast<LuckyDipPlugin*>(e->object)->dbgTick(); }
extern "C" MPC_EXPORT int ld_dbg_engine(AEffect* e, const char* key, char* out, int n)
{
    const std::string s = static_cast<LuckyDipPlugin*>(e->object)->dbgEngine(key);
    std::snprintf(out, (size_t)n, "%s", s.c_str());
    return (int)s.size();
}
extern "C" MPC_EXPORT void ld_dbg_engine_set(AEffect* e, const char* key, const char* v) { static_cast<LuckyDipPlugin*>(e->object)->dbgEngineSet(key, v); }
#endif

extern "C" MPC_EXPORT AEffect* VSTPluginMain(audioMasterCallback host)
{
    if (host != nullptr && host(nullptr, audioMasterVersion, 0, 0, nullptr, 0.0f) == 0) return nullptr;   // host is VST1 only
    AEffect* e = mpc::detail::createEffect(new LuckyDipPlugin(), host);
    if (e && !g_origDispatcher) g_origDispatcher = e->dispatcher;
    if (e) e->dispatcher = ldDispatcher;
    return e;
}

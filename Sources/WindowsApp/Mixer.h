#pragma once
#include "DubDSP.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace dubwin {
constexpr unsigned rate = 48000, quantum = 256;
constexpr int tracks = 6, buses = 3;
static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<uint64_t>::is_always_lock_free);
struct Strip {
    // Only replace samples with the audio device stopped.
    std::vector<float> samples;
    std::atomic<float> level{0.7f};
    std::array<std::atomic<float>, buses> send{};
    std::atomic<bool> mute{false}, solo{false};
    float smoothLevel = 0;
    std::array<float, buses> smoothSend{};
};
class Mixer {
    std::array<DubEffect*, buses> fx{};
    DubEffect* limiter = nullptr;
    std::array<DubEffect*, 2> instruments{};
    float voice[2][quantum]{};
    uint64_t cursor = 0;
    float smoothMaster = 0;
    std::array<float, buses> smoothReturn{};
    float dry[2][quantum]{}, inputs[buses][2][quantum]{}, wet[2][quantum]{};
public:
    std::array<Strip, tracks> strip;
    std::array<std::atomic<float>, buses> returns{{0.5f, 0.5f, 0.5f}};
    std::atomic<float> master{0.5f}, peak{0};
    std::atomic<bool> playing{false};
    std::atomic<uint64_t> position{0};
    Mixer() {
        fx[0] = dub_effect_create(DUB_EFFECT_DELAY);
        fx[1] = dub_effect_create(DUB_EFFECT_REVERB_BUS);
        fx[2] = dub_effect_create(DUB_EFFECT_BUS3);
        limiter = dub_effect_create(DUB_EFFECT_LIMITER);
        instruments[0] = dub_effect_create(DUB_EFFECT_SIREN);
        instruments[1] = dub_effect_create(DUB_EFFECT_SYNDRUM);
        if (!fx[0] || !fx[1] || !fx[2] || !limiter || !instruments[0] || !instruments[1]) {
            for (auto* e : fx) dub_effect_destroy(e);
            dub_effect_destroy(limiter);
            for (auto* e : instruments) dub_effect_destroy(e);
            throw std::runtime_error("No hay memoria para los efectos");
        }
        strip[4].send[0]=0.5f; strip[5].send[0]=0.5f;
        reset();
    }
    ~Mixer() { for (auto* e : fx) dub_effect_destroy(e); dub_effect_destroy(limiter); for (auto* e : instruments) dub_effect_destroy(e); }
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;
    void setInstrument(int index, int parameter, float value) noexcept {
        if (index>=0 && index<2) dub_effect_set(instruments[index],parameter,value);
    }
    void setEffect(int bus, int parameter, float value) noexcept {
        if (bus>=0 && bus<buses) dub_effect_set(fx[bus],parameter,value);
    }
    void releaseInstruments() noexcept {
        setInstrument(0,DUB_SIREN_GATE,0); setInstrument(0,DUB_SIREN_RISE,0);
        setInstrument(1,DUB_SYNDRUM_ROLLING,0); setInstrument(1,DUB_SYNDRUM_HIT,0);
    }
    void panic() noexcept {
        releaseInstruments();
        for (auto* e : instruments) dub_effect_set(e,DUB_EFFECT_CLEAR,1);
        for (auto* e : fx) dub_effect_set(e,DUB_EFFECT_CLEAR,1);
        setEffect(0,DUB_DELAY_HOLD,0); setEffect(0,DUB_DELAY_STOP,0); setEffect(0,DUB_DELAY_DOUBLE,0);
    }
    bool isInstrument(int t) const { return t>=4 && t<6 && strip[t].samples.empty(); }
    // UI thread, only after ma_device_stop has joined the audio callback.
    void reset() {
        releaseInstruments();
        for (auto* e : instruments) dub_effect_prepare(e,rate);
        playing.store(false); cursor = 0; position.store(0); peak.store(0);
        smoothMaster = 0; smoothReturn.fill(0);
        for (auto& s : strip) { s.smoothLevel = 0; s.smoothSend.fill(0); }
        for (auto* e : fx) dub_effect_prepare(e, rate);
        dub_effect_prepare(limiter, rate);
    }
    size_t totalFrames() const {
        size_t n = 0; for (const auto& s : strip) n = std::max(n, s.samples.size()/2); return n;
    }
    // All arrays are allocated before audio starts. No I/O, allocation or mutex here.
    void render(float* output, unsigned frames) noexcept {
        float blockPeak = 0;
        for (unsigned offset = 0; offset < frames; offset += quantum) {
            const unsigned n = std::min(quantum, frames - offset);
            for (auto& c : dry) std::fill_n(c, n, 0.0f);
            for (auto& b : inputs) for (auto& c : b) std::fill_n(c, n, 0.0f);
            bool anySolo = false;
            for (auto& s : strip) anySolo |= s.solo.load(std::memory_order_relaxed);
            const bool run = playing.load(std::memory_order_relaxed);
            for (int t=0;t<tracks;++t) {
                auto& s=strip[t];
                const bool instrument=isInstrument(t);
                if(instrument) {
                    for(auto& channel:voice) std::fill_n(channel,n,0.0f);
                    dub_effect_process(instruments[t-4],voice[0],voice[1],voice[0],voice[1],static_cast<int>(n));
                }
                const float target = (s.mute.load() || (anySolo && !s.solo.load())) ? 0 : s.level.load();
                std::array<float, buses> targetSend{};
                for (int b = 0; b < buses; ++b) targetSend[b] = s.send[b].load();
                for (unsigned i = 0; i < n; ++i) {
                    s.smoothLevel += (target - s.smoothLevel) * (1.0f/480.0f);
                    const uint64_t p = (cursor+i)*2;
                    const bool valid = run && p+1 < s.samples.size();
                    const float l = (instrument ? voice[0][i] : (valid ? s.samples[static_cast<size_t>(p)] : 0))*s.smoothLevel;
                    const float r = (instrument ? voice[1][i] : (valid ? s.samples[static_cast<size_t>(p+1)] : 0))*s.smoothLevel;
                    dry[0][i] += l; dry[1][i] += r;
                    for (int b = 0; b < buses; ++b) {
                        s.smoothSend[b] += (targetSend[b]-s.smoothSend[b])*(1.0f/480.0f);
                        inputs[b][0][i] += l*s.smoothSend[b];
                        inputs[b][1][i] += r*s.smoothSend[b];
                    }
                }
            }
            for (int b = 0; b < buses; ++b) {
                dub_effect_process(fx[b], inputs[b][0], inputs[b][1], wet[0], wet[1], static_cast<int>(n));
                const float target = returns[b].load();
                for (unsigned i = 0; i < n; ++i) {
                    smoothReturn[b] += (target-smoothReturn[b])*(1.0f/480.0f);
                    dry[0][i] += wet[0][i]*smoothReturn[b]; dry[1][i] += wet[1][i]*smoothReturn[b];
                }
            }
            const float gain = master.load();
            for (unsigned i = 0; i < n; ++i) {
                smoothMaster += (gain-smoothMaster)*(1.0f/480.0f);
                dry[0][i] *= smoothMaster; dry[1][i] *= smoothMaster;
            }
            dub_effect_process(limiter, dry[0], dry[1], wet[0], wet[1], static_cast<int>(n));
            for (unsigned i = 0; i < n; ++i) for (unsigned c = 0; c < 2; ++c) {
                const float x = std::isfinite(wet[c][i]) ? std::clamp(wet[c][i], -1.0f, 1.0f) : 0;
                output[(offset+i)*2+c] = x; blockPeak = std::max(blockPeak, std::abs(x));
            }
            if (run) { cursor += n; if (cursor >= totalFrames()) playing.store(false); }
        }
        position.store(cursor, std::memory_order_relaxed); peak.store(blockPeak, std::memory_order_relaxed);
    }
};
// Soft takeover: first MIDI movement only takes over when it reaches/crosses the GUI value.
struct Pickup {
    bool caught = false; int previous = -1;
    void reset() { caught = false; previous = -1; }
    bool accept(int value, float target) {
        const int t = static_cast<int>(std::lround(std::clamp(target,0.0f,1.0f)*127));
        if (std::abs(value-t) <= 2 || (previous >= 0 && (previous-t)*(value-t) <= 0)) caught = true;
        previous = value; return caught;
    }
};
constexpr int knobCC[tracks][3] = {{16,17,18},{20,21,22},{24,25,26},{28,29,30},{46,47,48},{50,51,52}};
constexpr int faderCC[tracks] = {19,23,27,31,49,53};
}

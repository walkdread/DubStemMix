#pragma once
#include "DubDSP.h"
#include "Bands.h"
#include "EchoDub.h"
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
    // Replace only with the device stopped or after the track-change acknowledgement.
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
    DubEffect* crash = nullptr;
    float smoothCrash=0;
    unsigned crashQuiet=0; bool crashAwake=false;
    std::atomic<bool> crashTrigger{false},crashPanic{false};
    std::array<DubEffect*, 2> instruments{};
    float voice[2][quantum]{};
    uint64_t cursor = 0;
    // 0=normal, 1=request, 2=UI owns sample buffers, 3=resume requested.
    // The audio callback continues instruments/FX without touching file buffers in state 2.
    std::atomic<int> trackChange{0};
    bool changeInstruments[2]{true,true};
    uint64_t nextFrame=0;

    std::atomic<uint64_t> seekTarget{UINT64_MAX};
    unsigned seekFade=0;
    float lastOutput[2]{},seekFrom[2]{};
    float smoothMaster = 0;
    std::array<float, buses> smoothReturn{};
    float dry[2][quantum]{}, inputs[buses][2][quantum]{}, wet[2][quantum]{};
Bands crossover;
    float bandAudio[4][2][quantum]{};
public:
    EchoDub echo;
    bool bandsMode=false,liveInput=false; // UI changes only while device is stopped.
    std::atomic<float> inputGain{1},inputPeak{0};
    std::array<Strip, tracks> strip;
    std::array<std::atomic<float>, buses> returns{{0.5f, 0.5f, 0.5f}};
    std::atomic<float> master{0.5f}, peak{0};
    std::array<std::atomic<float>,6> channelPeaks{};
    std::array<std::atomic<float>,3> returnPeaks{};
    std::atomic<float> crashLevel{1.0f};
    std::atomic<bool> ended{false};
    void triggerCrash() noexcept { crashTrigger=true; }
    void setCrashIntensity(float v) noexcept { dub_effect_set(crash,DUB_SPRING_INTENSITY,v); }
    std::atomic<bool> playing{false};
    std::atomic<uint64_t> position{0};
    Mixer() {
        fx[0] = dub_effect_create(DUB_EFFECT_DELAY);
        fx[1] = dub_effect_create(DUB_EFFECT_REVERB_BUS);
        fx[2] = nullptr; // EchoDub owns the third bus.
        crash = dub_effect_create(DUB_EFFECT_SPRING);
        limiter = dub_effect_create(DUB_EFFECT_LIMITER);
        instruments[0] = dub_effect_create(DUB_EFFECT_SIREN);
        instruments[1] = dub_effect_create(DUB_EFFECT_SYNDRUM);
        if (!crash || !fx[0] || !fx[1] || !limiter || !instruments[0] || !instruments[1]) {
            for (auto* e : fx) dub_effect_destroy(e);
            dub_effect_destroy(limiter); dub_effect_destroy(crash);
            for (auto* e : instruments) dub_effect_destroy(e);
            throw std::runtime_error("No hay memoria para los efectos");
        }
        strip[4].send[0]=0.5f; strip[5].send[0]=0.5f;
        reset();
    }
    ~Mixer() { dub_effect_destroy(crash); for (auto* e : fx) dub_effect_destroy(e); dub_effect_destroy(limiter); for (auto* e : instruments) dub_effect_destroy(e); }
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;
    void setInstrument(int index, int parameter, float value) noexcept {
        if (index>=0 && index<2) dub_effect_set(instruments[index],parameter,value);
    }
    void setEffect(int bus, int parameter, float value) noexcept {
        if (bus>=0 && bus<buses && fx[bus]) dub_effect_set(fx[bus],parameter,value);
    }
    void releaseInstruments() noexcept {
        setInstrument(0,DUB_SIREN_GATE,0); setInstrument(0,DUB_SIREN_RISE,0);
        setInstrument(1,DUB_SYNDRUM_ROLLING,0); setInstrument(1,DUB_SYNDRUM_HIT,0);
    }
    void panic() noexcept {
        releaseInstruments();echo.panic();crashTrigger=false;
        crashPanic=true;
        for (auto* e : instruments) dub_effect_set(e,DUB_EFFECT_CLEAR,1);
        for (auto* e : fx) if(e) dub_effect_set(e,DUB_EFFECT_CLEAR,1);
        setEffect(0,DUB_DELAY_HOLD,0); setEffect(0,DUB_DELAY_STOP,0); setEffect(0,DUB_DELAY_DOUBLE,0);
    }
    bool isInstrument(int t) const { return t>=4 && t<6 && (liveInput || strip[t].samples.empty()); }
    void requestTrackChange() noexcept {int expected=0;trackChange.compare_exchange_strong(expected,1);}
    bool trackChangeReady() const noexcept {return trackChange.load(std::memory_order_acquire)==2;}
    void finishTrackChange(bool resume,uint64_t frame=0) noexcept {
        nextFrame=frame;seekTarget=UINT64_MAX;ended=false;playing=resume;
        trackChange.store(3,std::memory_order_release);
    }
    // Audio thread, or UI only when the device is stopped. Never waits or allocates.
    bool serviceTrackChange() noexcept {
        int state=trackChange.load(std::memory_order_acquire);
        if(state==1){for(int i=0;i<2;++i)changeInstruments[i]=isInstrument(i+4);
            trackChange.store(2,std::memory_order_release);return true;}
        if(state==2)return true;
        if(state==3){
            crossover.reset();cursor=std::min<uint64_t>(nextFrame,totalFrames());position=cursor;
            ended=false;
            // Fade new file channels in, preserving instruments, tails, limiter and master.
            for(int t=0;t<4;++t){strip[t].smoothLevel=0;strip[t].smoothSend.fill(0);}
            trackChange.store(0,std::memory_order_release);
        }return false;
    }
    // UI thread, only after ma_device_stop has joined the audio callback.
    void reset() {
        trackChange=0;
        crossover.reset();echo.reset();inputPeak=0;ended=false;
        dub_effect_prepare(crash,rate);crashTrigger=false;crashPanic=false;crashAwake=false;crashQuiet=0;smoothCrash=0;
        releaseInstruments();
        for (auto* e : instruments) dub_effect_prepare(e,rate);
        playing.store(false); seekTarget.store(UINT64_MAX);seekFade=0;lastOutput[0]=lastOutput[1]=0; cursor = 0; position.store(0); peak.store(0);
        smoothMaster = 0; smoothReturn.fill(0);
        for(auto& p:channelPeaks)p=0;for(auto& p:returnPeaks)p=0;
        for (auto& s : strip) { s.smoothLevel = 0; s.smoothSend.fill(0); }
        for (auto* e : fx) if(e) dub_effect_prepare(e, rate);
        dub_effect_prepare(limiter, rate);
    }
    void seek(uint64_t frame) noexcept { ended=false; seekTarget.store(std::min<uint64_t>(frame,totalFrames()),std::memory_order_release); }
    size_t totalFrames() const {
        size_t n = 0; for (const auto& s : strip) n = std::max(n, s.samples.size()/2); return n;
    }
    // All arrays are allocated before audio starts. No I/O, allocation or mutex here.
    void render(float* output, unsigned frames,const float* capture=nullptr) noexcept {
        float blockPeak = 0,capturePeak=0;std::array<float,6> channelMax{};std::array<float,3> returnMax{};
        for (unsigned offset = 0; offset < frames; offset += quantum) {
            const bool changing=serviceTrackChange();
            const auto targetPosition=changing?UINT64_MAX:seekTarget.exchange(UINT64_MAX,std::memory_order_acquire);
            if(targetPosition!=UINT64_MAX){crossover.reset();cursor=std::min<uint64_t>(targetPosition,totalFrames());seekFade=128;seekFrom[0]=lastOutput[0];seekFrom[1]=lastOutput[1];}
            const unsigned n = std::min(quantum, frames - offset);
            for (auto& c : dry) std::fill_n(c, n, 0.0f);
            for (auto& b : inputs) for (auto& c : b) std::fill_n(c, n, 0.0f);
            bool anySolo = false;
            for (auto& s : strip) anySolo |= s.solo.load(std::memory_order_relaxed);
            const bool run = !changing && playing.load(std::memory_order_relaxed);
            if(!changing&&bandsMode){float gain=inputGain.load();for(unsigned i=0;i<n;++i)for(int c=0;c<2;++c){
                size_t p=size_t(cursor+i)*2+c;float x=liveInput?(capture?capture[(offset+i)*2+c]*gain:0):(p<strip[0].samples.size()?strip[0].samples[p]:0);
                if(!std::isfinite(x))x=0;capturePeak=std::max(capturePeak,std::abs(x));auto parts=crossover.process(run?x:0,c);
                for(int t=0;t<4;++t)bandAudio[t][c][i]=parts[t];
            }}
            for (int t=0;t<tracks;++t) {
                auto& s=strip[t];
                const bool instrument=changing?(t>=4&&changeInstruments[t-4]):isInstrument(t);
                if(instrument) {
                    for(auto& channel:voice) std::fill_n(channel,n,0.0f);
                    dub_effect_process(instruments[t-4],voice[0],voice[1],voice[0],voice[1],static_cast<int>(n));
                }
                const float target = (anySolo ? !s.solo.load() : s.mute.load()) ? 0 : s.level.load();
                std::array<float, buses> targetSend{};
                for (int b = 0; b < buses; ++b) targetSend[b] = s.send[b].load();
                for (unsigned i = 0; i < n; ++i) {
                    s.smoothLevel += (target - s.smoothLevel) * (1.0f/480.0f);
                    const uint64_t p = (cursor+i)*2;
                    const bool valid = run && p+1 < s.samples.size();
                    const float l = (!changing&&bandsMode&&t<4?bandAudio[t][0][i]:(instrument ? voice[0][i] : (valid ? s.samples[static_cast<size_t>(p)] : 0)))*s.smoothLevel;
                    const float r = (!changing&&bandsMode&&t<4?bandAudio[t][1][i]:(instrument ? voice[1][i] : (valid ? s.samples[static_cast<size_t>(p+1)] : 0)))*s.smoothLevel;
                    channelMax[t]=std::max(channelMax[t],std::max(std::abs(l),std::abs(r)));
                    dry[0][i] += l; dry[1][i] += r;
                    for (int b = 0; b < buses; ++b) {
                        s.smoothSend[b] += (targetSend[b]-s.smoothSend[b])*(1.0f/480.0f);
                        inputs[b][0][i] += l*s.smoothSend[b];
                        inputs[b][1][i] += r*s.smoothSend[b];
                    }
                }
            }
            for (int b = 0; b < buses; ++b) {
                if(b==2)echo.process(inputs[b][0],inputs[b][1],wet[0],wet[1],n);else dub_effect_process(fx[b], inputs[b][0], inputs[b][1], wet[0], wet[1], static_cast<int>(n));
                const float target = returns[b].load();
                for (unsigned i = 0; i < n; ++i) {
                    smoothReturn[b] += (target-smoothReturn[b])*(1.0f/480.0f);
                    returnMax[b]=std::max(returnMax[b],std::max(std::abs(wet[0][i]),std::abs(wet[1][i]))*smoothReturn[b]);
                    dry[0][i] += wet[0][i]*smoothReturn[b]; dry[1][i] += wet[1][i]*smoothReturn[b];
                }
            }
            if(crashPanic.exchange(false)&&crashAwake)dub_effect_set(crash,DUB_EFFECT_CLEAR,1);
            if(crashTrigger.exchange(false)){dub_effect_set(crash,DUB_SPRING_CRASH,1);crashAwake=true;crashQuiet=0;}
            if(crashAwake){
                for(auto& c:voice)std::fill_n(c,n,0.f);
                dub_effect_process(crash,voice[0],voice[1],wet[0],wet[1],int(n));
                float cp=0;const float target=crashLevel.load();
                for(unsigned i=0;i<n;++i){smoothCrash+=(target-smoothCrash)/480.f;
                    for(int c=0;c<2;++c){cp=std::max(cp,std::abs(wet[c][i]));dry[c][i]+=wet[c][i]*smoothCrash;}}
                crashQuiet=cp<0.000001f?crashQuiet+n:0;if(crashQuiet>rate)crashAwake=false;
            }
            const float gain = master.load();
            for (unsigned i = 0; i < n; ++i) {
                smoothMaster += (gain-smoothMaster)*(1.0f/480.0f);
                dry[0][i] *= smoothMaster; dry[1][i] *= smoothMaster;
            }
            dub_effect_process(limiter, dry[0], dry[1], wet[0], wet[1], static_cast<int>(n));
            for (unsigned i = 0; i < n; ++i) for (unsigned c = 0; c < 2; ++c) {
                const float x = std::isfinite(wet[c][i]) ? std::clamp(wet[c][i], -1.0f, 1.0f) : 0;
                const float blend=seekFade?float(seekFade)/128.0f:0;
                const float sample=x*(1-blend)+seekFrom[c]*blend;
                output[(offset+i)*2+c] = sample;lastOutput[c]=sample;
                if(c==1 && seekFade)--seekFade;
                blockPeak = std::max(blockPeak, std::abs(sample));
            }
            if (run && !liveInput) { cursor += n; if (totalFrames() && cursor >= totalFrames()) { cursor=totalFrames();playing.store(false);ended.store(true,std::memory_order_release); } }
        }
        for(int t=0;t<6;++t)channelPeaks[t].store(channelMax[t],std::memory_order_relaxed);
        for(int b=0;b<3;++b)returnPeaks[b].store(returnMax[b],std::memory_order_relaxed);
        inputPeak.store(capturePeak);position.store(cursor, std::memory_order_relaxed); peak.store(blockPeak, std::memory_order_relaxed);
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

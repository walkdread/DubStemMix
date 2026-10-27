#include "Mixer.h"
#include "Overlap.h"
#include "PlaybackCache.h"
#include "WarmCache.h"
#include "Session.h"
#include <cstdio>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"Failed line %d: %s\n",__LINE__,#x); return 1;}}while(0)
int main() {
    dubwin::Mixer m;
    float out[1024]{};
    m.render(out,512);
    for(float x:out) CHECK(x==0);
    for(int t=0;t<2;++t) {
        m.strip[t].samples.resize(48000*2);
        for(size_t i=0;i<48000;++i) {
            float v = 0.1f*std::sin(static_cast<float>(i)*0.07f);
            m.strip[t].samples[i*2]=v; m.strip[t].samples[i*2+1]=(t==0?v:-v);
        }
    }
    m.playing=true;
    float energyL=0,energyR=0;
    for(int b=0;b<12;++b) {m.render(out,512); for(int i=0;i<512;++i){energyL+=std::abs(out[i*2]);energyR+=std::abs(out[i*2+1]);}}
    CHECK(energyL>1 && energyR<0.0001f); // stems share one cursor: right channels cancel
    CHECK(m.position.load()==12*512);
    m.playing=false; auto pos=m.position.load(); m.render(out,127); CHECK(m.position.load()==pos);
    m.reset(); CHECK(m.position.load()==0 && !m.playing.load());
    m.strip[0].samples.assign(2,0.5f); m.strip[1].samples.clear(); m.playing=true;
    m.render(out,512); CHECK(!m.playing.load());
    m.reset(); for (auto& s:m.strip) {s.samples.assign(48000*2,10); s.level=1; for(auto& send:s.send) send=1;}
    m.master=1; m.playing=true;
    for(int b=0;b<50;++b) {m.render(out,512); for(float x:out) CHECK(std::isfinite(x) && std::abs(x)<=1);}
    // Instruments must sound without stems or playing transport, and stop safely.
    m.reset();for(auto& s:m.strip)s.samples.clear();
    m.setInstrument(0,DUB_SIREN_GATE,1);
    float sirenEnergy=0;for(int b=0;b<8;++b){m.render(out,512);for(float x:out)sirenEnergy+=std::abs(x);}
    CHECK(sirenEnergy>1);CHECK(!m.playing.load());
    m.reset();m.setInstrument(1,DUB_SYNDRUM_HIT,1);
    float hitEnergy=0;for(int b=0;b<8;++b){m.render(out,512);for(float x:out)hitEnergy+=std::abs(x);}
    CHECK(hitEnergy>0.1f);
    m.reset();m.setInstrument(0,DUB_SIREN_GATE,1);m.strip[4].samples.assign(48000*2,0);
    for(int b=0;b<4;++b){m.render(out,512);for(float x:out)CHECK(x==0);} // loaded stem disables siren
    m.strip[4].samples.clear();m.panic();
    for(int b=0;b<500;++b)m.render(out,512);
    for(float x:out)CHECK(std::isfinite(x) && std::abs(x)<0.001f);
    // Solo matches macOS: overrides mute, permits several stems, restores mute afterwards.
    {
        dubwin::Mixer soloMix;
        soloMix.strip[0].samples.assign(48000*2,0.1f);
        soloMix.strip[1].samples.assign(48000*2,0.2f);
        soloMix.strip[0].mute=true;soloMix.strip[1].mute=true;
        soloMix.playing=true;
        auto settledEnergy=[&](){float energy=0;for(int b=0;b<16;++b)soloMix.render(out,512);for(float x:out)energy+=std::abs(x);return energy;};
        CHECK(settledEnergy()<0.001f);
        soloMix.strip[0].solo=true;
        const float one=settledEnergy();CHECK(one>1);
        soloMix.strip[1].solo=true;
        CHECK(settledEnergy()>one*2);
        soloMix.strip[0].solo=false;
        CHECK(settledEnergy()>one*1.5f);
        soloMix.strip[1].solo=false;
        CHECK(settledEnergy()<0.001f);
        CHECK(soloMix.strip[0].mute && soloMix.strip[1].mute);
    }
    // Seeking uses one shared audio cursor and also works while paused and near EOF.
    {
        dubwin::Mixer seekMix;
        seekMix.strip[0].samples.assign(48000*2,0.1f);seekMix.strip[1].samples.assign(48000*2,0.2f);
        seekMix.seek(12000);seekMix.render(out,512);CHECK(seekMix.position==12000);CHECK(!seekMix.playing);
        seekMix.playing=true;seekMix.seek(24000);seekMix.render(out,512);CHECK(seekMix.position==24512);
        for(float x:out)CHECK(std::isfinite(x));
        seekMix.seek(UINT64_MAX);seekMix.render(out,512);CHECK(!seekMix.playing);
        seekMix.reset();seekMix.render(out,512);CHECK(seekMix.position==0);
    }
    // Playback cache is bit-exact, bounded and rejects changed sources, corruption and truncation.
    {
        dubwin::StemAudio source;for(int t=0;t<4;++t){source[t].assign(9600,0);source[t][4567]=0.2f*(t+1);}
        auto peaks=dubwin::makeWaveform(source);CHECK(*std::max_element(peaks.begin(),peaks.end())==1);
        auto path=std::filesystem::temp_directory_path()/"dubstem-cache-regression.pcm";std::atomic<bool> cancel{false};
        CHECK(dubwin::writePlaybackCache(path,42,source,peaks,cancel));
        dubwin::StemAudio restored;std::vector<float> restoredPeaks;
        CHECK(dubwin::readPlaybackCache(path,42,restored,restoredPeaks,cancel));CHECK(source==restored && peaks==restoredPeaks);
        CHECK(!dubwin::readPlaybackCache(path,43,restored,restoredPeaks,cancel));
        {std::fstream f(path,std::ios::binary|std::ios::in|std::ios::out);f.seekp(sizeof(dubwin::PlaybackHeader)+11);f.put(char(123));}
        CHECK(!dubwin::readPlaybackCache(path,42,restored,restoredPeaks,cancel));
        std::filesystem::resize_file(path,8);CHECK(!dubwin::readPlaybackCache(path,42,restored,restoredPeaks,cancel));
        CHECK(dubwin::writePlaybackCache(path,42,source,peaks,cancel));cancel=true;bool stopped=false;
        try{dubwin::readPlaybackCache(path,42,restored,restoredPeaks,cancel);}catch(const std::exception&){stopped=true;}CHECK(stopped);
        std::filesystem::remove(path);
    }
    // Per-strip meters measure actual post-fader/post-mute signal, independently of master gain.
    {
        dubwin::Mixer metered;metered.strip[0].samples.assign(96000,0.5f);metered.strip[1].samples.assign(96000,0.1f);metered.playing=true;
        for(int b=0;b<16;++b)metered.render(out,512);
        CHECK(metered.channelPeaks[0]>0.3f && metered.channelPeaks[1]>0.06f);
        metered.strip[0].mute=true;for(int b=0;b<16;++b)metered.render(out,512);
        CHECK(metered.channelPeaks[0]<0.001f && metered.channelPeaks[1]>0.06f);
        metered.reset();for(auto& peak:metered.channelPeaks)CHECK(peak==0);
    }
    // Memory cache moves sample ownership, enforces its budget and evicts oldest buffers.
    {
        dubwin::WarmCache cache(4096);auto make=[](const char* key){dubwin::WarmAudio audio;audio.key=key;for(auto& stem:audio.stems)stem.assign(128,0.25f);return audio;};
        cache.put(make("a"));cache.put(make("b"));CHECK(cache.bytes()==4096);cache.put(make("c"));CHECK(!cache.contains("a") && cache.contains("b"));
        dubwin::WarmAudio hit;CHECK(cache.take("b",hit));CHECK(hit.stems[0][1]==0.25f && !cache.contains("b"));CHECK(cache.bytes()==2048);
        cache.reserve(4096);CHECK(cache.bytes()==0);
    }
    // Named libraries preserve Unicode paths, order and durations, and replace atomically.
    {
        auto file=std::filesystem::temp_directory_path()/"dubstem-session-test.dubset";
        std::vector<dubwin::SessionEntry> list{{std::filesystem::u8path(u8"musica/canción ñ.wav"),"stems/one",true,123.25},{"second.mp3","",false,-1}};
        dubwin::writeSession(file,list);auto restored=dubwin::readSession(file);CHECK(restored.size()==2);
        CHECK(restored[0].source==std::filesystem::absolute(list[0].source) && restored[0].duration==123.25 && restored[0].fast);
        CHECK(restored[1].source==std::filesystem::absolute(list[1].source) && !restored[1].fast && restored[1].cache.empty());
        dubwin::writeSession(file,{});CHECK(dubwin::readSession(file).empty());
        {std::ofstream bad(file);bad<<"DubStemMix Library 1\n2\n";}bool rejected=false;
        try{dubwin::readSession(file);}catch(const std::exception&){rejected=true;}CHECK(rejected);std::filesystem::remove(file);
    }
    // Weighted overlap must reconstruct a constant without gaps, including the padded tail.
    const size_t total=dubwin::modelFrames+dubwin::stride+37;
    dubwin::Accumulator accumulation(total);std::vector<float> ones(dubwin::modelFrames,1);
    for(size_t start=0;start<total;start+=dubwin::stride)accumulation.add(ones.data(),ones.data(),start,std::min(dubwin::modelFrames,total-start));
    accumulation.normalize();for(float x:accumulation.samples)CHECK(std::abs(x-1)<1e-6);
    dubwin::Pickup p; CHECK(!p.accept(0,0.5f)); CHECK(!p.accept(40,0.5f)); CHECK(p.accept(80,0.5f));
    p.reset(); CHECK(!p.accept(127,0.5f)); CHECK(p.accept(64,0.5f));
    std::puts("PASS: silence, sample alignment, pause, reset, EOF, overload, instruments, panic, overlap-add MIDI pickup, seeking, playback cache, meters, warm cache and sessions");
}

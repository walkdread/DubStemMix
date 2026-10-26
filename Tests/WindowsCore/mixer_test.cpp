#include "Mixer.h"
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
    dubwin::Pickup p; CHECK(!p.accept(0,0.5f)); CHECK(!p.accept(40,0.5f)); CHECK(p.accept(80,0.5f));
    p.reset(); CHECK(!p.accept(127,0.5f)); CHECK(p.accept(64,0.5f));
    std::puts("PASS: silence, sample alignment, pause, reset, EOF, overload and MIDI pickup");
}

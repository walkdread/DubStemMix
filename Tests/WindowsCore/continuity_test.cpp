#include "Mixer.h"
#include <memory>
#include <cstdio>
#include <thread>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(){
    // Two identical DSP histories: replacing silent files must leave every instrument
    // and effect sample identical to the uninterrupted reference, including oscillator phase.
    for(int gesture=0;gesture<4;++gesture){
        auto a=std::make_unique<dubwin::Mixer>(),b=std::make_unique<dubwin::Mixer>();
        for(auto* m:{a.get(),b.get()}){
            m->strip[0].samples.assign(48000*2,0);m->playing=true;
            m->strip[4].send[1]=.5f;m->strip[5].send[2]=.5f;
            if(gesture==0)m->setInstrument(0,DUB_SIREN_GATE,1);
            if(gesture==1){m->setInstrument(0,DUB_SIREN_GATE,1);m->setInstrument(0,DUB_SIREN_RISE,1);}
            if(gesture==2)m->setInstrument(1,DUB_SYNDRUM_HIT,1);
            if(gesture==3)m->setInstrument(1,DUB_SYNDRUM_ROLLING,1);
        }
        float left[512],right[512];double energy=0;
        for(int block=0;block<100;++block){
            if(block==3)a->requestTrackChange();
            if(block==4){CHECK(a->trackChangeReady());a->strip[0].samples.assign(96000,0);a->bandsMode=true;}
            if(block==30)a->finishTrackChange(true);
            a->render(left,256);b->render(right,256);
            for(int i=0;i<512;++i){CHECK(std::isfinite(left[i]));CHECK(left[i]==right[i]);if(block>=4&&block<30)energy+=left[i]*left[i];}
        }CHECK(energy>.00001);
        a->releaseInstruments();b->releaseInstruments();
        for(int block=0;block<500;++block){a->render(left,256);b->render(right,256);for(int i=0;i<512;++i)CHECK(left[i]==right[i]);}
        a->panic();for(int block=0;block<100;++block)a->render(left,256);for(float x:left)CHECK(std::abs(x)<.0001f);
        a->reset();CHECK(!a->playing&&!a->trackChangeReady());
    }
    // Concurrent handoff: mutate/reallocate only after callback acknowledges ownership.
    auto mixer=std::make_unique<dubwin::Mixer>();mixer->setInstrument(0,DUB_SIREN_GATE,1);
    std::atomic<bool> done{false},bad{false};
    std::thread audio([&]{float out[1024];while(!done){mixer->render(out,512);for(float x:out)if(!std::isfinite(x))bad=true;}});
    for(int n=0;n<100;++n){
        while(!mixer->trackChangeReady()){mixer->requestTrackChange();std::this_thread::yield();}
        mixer->strip[0].samples.assign((n+1)*400,0);mixer->bandsMode=n%2;
        mixer->finishTrackChange(n%3!=0);
    }
    done=true;audio.join();CHECK(!bad);
    std::puts("PASS sample-exact HOLD/RISE/HIT/ROLL and FX continuity, release, panic, concurrent file replacement");
}

#include "Mixer.h"
#include "PerformanceControls.h"
#include <cstdio>
#include <memory>
#include <cstdlib>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static double crashEnergy(float level,float intensity){
    auto m=std::make_unique<dubwin::Mixer>();m->crashLevel=level;m->setCrashIntensity(intensity);m->triggerCrash();
    float out[1024];double energy=0;for(int b=0;b<190;++b){m->render(out,512);for(float x:out){if(!std::isfinite(x)||std::abs(x)>1)std::abort();energy+=x*x;}}return energy;
}
int main(){
    dubwin::SendAllGate gate;int applied=0,toggles=0;auto apply=[&](uint32_t){++applied;};
    for(int cc=0;cc<128;++cc)if(gate.factoryCC(cc))toggles+=gate.push(0xb0|(cc<<8)|(127<<16),apply);
    CHECK(toggles==1&&applied==0&&gate.empty());
    gate.push(0xb0|(16<<8),apply);gate.push(0xb0|(16<<8)|(1<<16),apply);gate.flush(apply);CHECK(applied==2);
    for(int i=0;i<300;++i)CHECK(!gate.push(0xb0|(54<<8),apply));gate.flush(apply);CHECK(applied==302);
    for(int from=0;from<5;++from)for(int gap=0;gap<=5;++gap)for(int active=-1;active<5;++active){
        std::vector<int> list{0,1,2,3,4};int job=active;int to=dubwin::reorder(list,from,gap,job);
        CHECK(list[to]==from);CHECK(active<0?job==-1:list[job]==active);auto sorted=list;std::sort(sorted.begin(),sorted.end());CHECK((sorted==std::vector<int>{0,1,2,3,4}));
    }
    auto mixer=std::make_unique<dubwin::Mixer>();mixer->strip[0].samples.assign(514,.1f);mixer->playing=true;float output[1024];
    mixer->render(output,512);CHECK(mixer->ended.exchange(false)&&!mixer->playing&&mixer->position==257);
    mixer->render(output,512);CHECK(!mixer->ended);mixer->reset();CHECK(!mixer->ended);
    mixer->playing=false;mixer->render(output,512);CHECK(!mixer->ended);
    mixer->bandsMode=mixer->liveInput=true;mixer->playing=true;mixer->render(output,512);CHECK(!mixer->ended&&mixer->playing);
    double quiet=crashEnergy(.25f,1),loud=crashEnergy(1,1);CHECK(quiet>0&&loud>quiet*5);CHECK(crashEnergy(0,1)==0);CHECK(crashEnergy(1,0)==0);
    auto* delay=dub_effect_create(DUB_EFFECT_DELAY);CHECK(delay);
    CHECK(dub_effect_get(delay,DUB_DELAY_SPEED_FACTOR)==2);CHECK(dub_effect_get(delay,DUB_EFFECT_CLEAR)==0);
    for(float extreme:{0.f,1.f}){dub_effect_set(delay,DUB_DELAY_HOLD_LEVEL,extreme*2);dub_effect_set(delay,DUB_DELAY_SPEED_FACTOR,.5f+3.5f*extreme);
        dub_effect_set(delay,DUB_DELAY_STOP_SECONDS,.05f+4.95f*extreme);dub_effect_set(delay,DUB_DELAY_START_SECONDS,.05f+4.95f*extreme);
        dub_effect_prepare(delay,48000);float in[256],left[256],right[256];for(int i=0;i<256;++i)in[i]=.2f*std::sin(i*.1f);
        for(int b=0;b<500;++b){dub_effect_set(delay,DUB_DELAY_DOUBLE,b>50);dub_effect_set(delay,DUB_DELAY_HOLD,b>100&&b<200);dub_effect_set(delay,DUB_DELAY_STOP,b>200&&b<300);
            dub_effect_process(delay,in,in,left,right,256);for(float x:left)CHECK(std::isfinite(x)&&std::abs(x)<=4);}}
    dub_effect_destroy(delay);
    std::puts("PASS SEND ALL isolation, ordinary CC delivery, reorder identities, EOF vs pause/live, independent CRASH, gesture extremes");
}

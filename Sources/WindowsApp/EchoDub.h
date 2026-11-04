#pragma once
#include <array>
#include <vector>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace dubwin {
class EchoDub {
    static constexpr size_t length=1u<<18,mask=length-1;
    std::vector<float> line[2];size_t head=0;
    float delay=24000,fb=.5f,lp[2]{},hp[2]{},drive=1;
    std::atomic<bool> clear{false};
public:
    std::atomic<float> seconds{.5f},feedback{.5f},filter{.5f},saturation{.2f};
    EchoDub(){for(auto& v:line)v.resize(length);}
    void reset(){for(auto& v:line)std::fill(v.begin(),v.end(),0);head=0;delay=seconds.load()*48000;fb=feedback.load();drive=1+saturation.load()*3;lp[0]=lp[1]=hp[0]=hp[1]=0;clear=false;}
    void panic(){clear=true;}
    void process(const float* l,const float* r,float* ol,float* oright,unsigned n){
        if(clear.exchange(false))reset();
        float d=std::clamp(seconds.load(),.02f,3.5f)*48000,f=std::clamp(feedback.load(),0.f,1.05f),targetDrive=1+3*std::clamp(saturation.load(),0.f,1.f);
        float cutoff=400*std::pow(30.f,std::clamp(filter.load(),0.f,1.f));
        float a=1-std::exp(-6.2831853f*cutoff/48000),h=1-std::exp(-6.2831853f*120/48000);
        for(unsigned i=0;i<n;++i){delay+=(d-delay)*.00018f;fb+=(f-fb)*.001f;drive+=(targetDrive-drive)*.001f;
            size_t back=size_t(delay);float fraction=delay-float(back);
            for(int c=0;c<2;++c){float wet=line[c][(head-back)&mask]*(1-fraction)+line[c][(head-back-1)&mask]*fraction;
                float input=(c?r[i]:l[i])+fb*wet;hp[c]+=h*(input-hp[c]);input-=hp[c];lp[c]+=a*(input-lp[c]);
                if(std::abs(lp[c])<1e-20f)lp[c]=0;if(std::abs(hp[c])<1e-20f)hp[c]=0;
                line[c][head]=std::tanh(lp[c]*drive)/drive;(c?oright:ol)[i]=wet;}
            head=(head+1)&mask;
        }
    }
};
class TapTempo {
    uint64_t last=0;std::array<double,4> intervals{};unsigned count=0,index=0;
public:
    double bpm=120;
    bool tap(uint64_t ms){if(!last||ms<=last||ms-last>2000){last=ms;count=index=0;return false;}
        double delta=double(ms-last);if(delta<200)return false;last=ms;intervals[index++%4]=delta;count=std::min(4u,count+1);
        double sum=0;for(unsigned i=0;i<count;++i)sum+=intervals[i];bpm=60000/(sum/count);return true;}
    double seconds(int division)const{return 60/bpm*(division==1?.5:division==2?.75:1);}
};
}

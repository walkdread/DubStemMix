#include "Mixer.h"
#include <chrono>
#include <cstdio>
#include <memory>

// Exercise device-sized and non-aligned buffers in all source modes. Report
// throughput, not a machine-dependent timing threshold, to keep CI reproducible.
int main() {
    constexpr unsigned sizes[]{1, 127, 256, 511, 512, 1024, 2049};
    for (int mode=0; mode<3; ++mode) {
        auto mixer=std::make_unique<dubwin::Mixer>();
        mixer->bandsMode=mode!=0; mixer->liveInput=mode==2;
        for(int t=0;t<(mode==0?4:1);++t) {
            auto& audio=mixer->strip[t].samples;
            audio.resize(48000*12*2);
            for(size_t i=0;i<audio.size();++i)audio[i]=.1f*std::sin(float(i/2)*.03f*(t+1));
        }
        for(auto& strip:mixer->strip)for(auto& send:strip.send)send=.3f;
        std::vector<float> input(2049*2,.1f),output(2049*2+2);
        mixer->playing=true;
        uint64_t frames=0;unsigned iteration=0;
        auto start=std::chrono::steady_clock::now();
        while(frames<48000*10) {
            unsigned n=sizes[iteration++%7];
            output[0]=output[n*2+1]=12345;
            mixer->render(output.data()+1,n,mode==2?input.data():nullptr);
            if(output[0]!=12345||output[n*2+1]!=12345)return 1;
            for(unsigned i=1;i<=n*2;++i)if(!std::isfinite(output[i])||std::abs(output[i])>1)return 2;
            frames+=n;
        }
        auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::printf("PERF mode=%s audio=%.2fs wall=%.3fs throughput=%.1fx real-time\n",
            mode==0?"stems":mode==1?"bands-file":"bands-input",double(frames)/48000,elapsed,double(frames)/48000/elapsed);
        if(mixer->position.load()!=(mode==2?0:frames))return 3;
    }
    std::puts("PASS: variable callback buffers, output bounds, transport and sustained FX");
}

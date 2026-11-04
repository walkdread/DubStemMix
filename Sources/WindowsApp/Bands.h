#pragma once
#include <array>
#include <cmath>
namespace dubwin {
// LR4 tree with phase compensation: summed bands have unity magnitude.
class Bands {
    struct Biquad {
        double b0=0,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
        void setup(double hz,bool high) {
            const double k=std::tan(3.141592653589793*hz/48000), norm=1/(1+std::sqrt(2.0)*k+k*k);
            b0=(high?1:k*k)*norm;b1=(high?-2:2)*b0;b2=b0;
            a1=2*(k*k-1)*norm;a2=(1-std::sqrt(2.0)*k+k*k)*norm;z1=z2=0;
        }
        double tick(double x){double y=b0*x+z1;z1=b1*x-a1*y+z2;z2=b2*x-a2*y;
            if(std::abs(z1)<1e-25)z1=0;if(std::abs(z2)<1e-25)z2=0;return y;}
    };
    struct Split {
        Biquad lo[2],hi[2];
        void setup(double hz){for(auto& b:lo)b.setup(hz,false);for(auto& b:hi)b.setup(hz,true);}
        std::array<double,2> tick(double x){return {lo[1].tick(lo[0].tick(x)),hi[1].tick(hi[0].tick(x))};}
        double allpass(double x){auto y=tick(x);return y[0]+y[1];}
    };
    struct Channel {Split mid,low,high,compLow[2],compHigh[2];} channels[2];
public:
    Bands(){reset();}
    void reset(){for(auto& c:channels){c.mid.setup(250);c.low.setup(80);c.high.setup(3000);for(auto& s:c.compLow)s.setup(3000);for(auto& s:c.compHigh)s.setup(80);}}
    std::array<float,4> process(float x,int channel){auto& c=channels[channel];auto m=c.mid.tick(x);auto lo=c.low.tick(m[0]),hi=c.high.tick(m[1]);
        return {float(c.compLow[0].allpass(lo[0])),float(c.compLow[1].allpass(lo[1])),float(c.compHigh[0].allpass(hi[0])),float(c.compHigh[1].allpass(hi[1]))};}
};
}

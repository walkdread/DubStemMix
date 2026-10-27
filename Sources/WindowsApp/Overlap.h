#pragma once
#include <algorithm>
#include <vector>
#include <stdexcept>
namespace dubwin {
constexpr size_t modelFrames=343980, overlap=modelFrames/4, stride=modelFrames-overlap;
inline float windowWeight(size_t i) {
    if(i<overlap) return float(i+1)/float(overlap);
    if(i>=modelFrames-overlap) return float(modelFrames-i)/float(overlap);
    return 1;
}
struct Accumulator {
    std::vector<float> samples, weights;
    explicit Accumulator(size_t frames):samples(frames*2,0),weights(frames,0){}
    void add(const float* l,const float* r,size_t start,size_t length) {
        if(start>weights.size() || length>weights.size()-start || length>modelFrames)throw std::runtime_error("Invalid overlap");
        for(size_t i=0;i<length;++i){auto w=windowWeight(i);samples[(start+i)*2]+=l[i]*w;samples[(start+i)*2+1]+=r[i]*w;weights[start+i]+=w;}
    }
    void normalize(){for(size_t i=0;i<weights.size();++i){auto w=std::max(weights[i],1e-8f);samples[i*2]/=w;samples[i*2+1]/=w;}}
};
}

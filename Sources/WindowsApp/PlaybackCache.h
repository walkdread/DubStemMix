#pragma once
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
namespace dubwin {
using StemAudio=std::array<std::vector<float>,4>;
constexpr size_t waveBins=4096;
inline uint64_t cacheHash(const void* data,size_t bytes,uint64_t hash=14695981039346656037ull){
    const auto* p=static_cast<const unsigned char*>(data);for(size_t i=0;i<bytes;++i){hash^=p[i];hash*=1099511628211ull;}return hash;
}
inline std::vector<float> makeWaveform(const StemAudio& stems){
    size_t frames=0;for(auto& s:stems)frames=std::max(frames,s.size()/2);
    std::vector<float> peaks(waveBins,0);if(!frames)return peaks;
    // Peak envelope over every sample, not sparse snapshots; stereo stems contribute equally.
    for(auto& s:stems)for(size_t f=0;f<s.size()/2;++f){size_t bin=std::min(waveBins-1,f*waveBins/frames);
        peaks[bin]=std::max(peaks[bin],std::max(std::abs(s[f*2]),std::abs(s[f*2+1])));}
    float top=*std::max_element(peaks.begin(),peaks.end());if(top>0)for(auto& p:peaks)p/=top;
    return peaks;
}
struct PlaybackHeader {uint64_t magic=0x314d435058494d44ull,signature=0,frames=0,bins=waveBins;uint64_t hashes[5]{};};
inline void cacheCancelled(const std::atomic<bool>& cancel){if(cancel.load())throw std::runtime_error("Carga cancelada");}
inline bool readPlaybackCache(const std::filesystem::path& path,uint64_t signature,StemAudio& stems,std::vector<float>& peaks,const std::atomic<bool>& cancel){
    std::ifstream in(path,std::ios::binary);if(!in)return false;PlaybackHeader h{},expected;in.read(reinterpret_cast<char*>(&h),sizeof h);
    if(!in||h.magic!=expected.magic||h.signature!=signature||!h.frames||h.frames>48000*600ull||h.bins!=waveBins)return false;
    std::error_code ec;auto bytes=std::filesystem::file_size(path,ec);
    if(ec||bytes!=sizeof h+(h.frames*8+waveBins)*sizeof(float))return false;
    for(int t=0;t<5;++t){cacheCancelled(cancel);auto& target=t<4?stems[t]:peaks;target.resize(t<4?size_t(h.frames)*2:waveBins);
        auto* dest=reinterpret_cast<char*>(target.data());size_t remain=target.size()*sizeof(float);uint64_t hash=14695981039346656037ull;
        while(remain){cacheCancelled(cancel);size_t n=std::min<size_t>(remain,1024*1024);in.read(dest,n);if(!in)return false;hash=cacheHash(dest,n,hash);dest+=n;remain-=n;}
        if(hash!=h.hashes[t])return false;
    }return true;
}
inline bool writePlaybackCache(const std::filesystem::path& path,uint64_t signature,const StemAudio& stems,const std::vector<float>& peaks,const std::atomic<bool>& cancel){
    PlaybackHeader h;h.signature=signature;h.frames=stems[0].size()/2;
    if(!h.frames||peaks.size()!=waveBins)return false;
    for(auto& s:stems)if(s.size()!=h.frames*2)return false;
    auto temp=path;temp+=L".tmp";
    try{std::ofstream out(temp,std::ios::binary|std::ios::trunc);if(!out)return false;
        out.write(reinterpret_cast<const char*>(&h),sizeof h);
        for(int t=0;t<5;++t){auto& source=t<4?stems[t]:peaks;auto* data=reinterpret_cast<const char*>(source.data());size_t remain=source.size()*sizeof(float);uint64_t hash=14695981039346656037ull;
            while(remain){cacheCancelled(cancel);size_t n=std::min<size_t>(remain,1024*1024);out.write(data,n);hash=cacheHash(data,n,hash);data+=n;remain-=n;}h.hashes[t]=hash;}
        out.seekp(0);out.write(reinterpret_cast<const char*>(&h),sizeof h);out.close();
        if(!out)throw std::runtime_error("No se pudo guardar la cache de reproduccion");
        std::error_code ec;std::filesystem::remove(path,ec);std::filesystem::rename(temp,path);return true;
    }catch(...){std::error_code ec;std::filesystem::remove(temp,ec);if(cancel.load())throw;return false;}
}
}

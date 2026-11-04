#pragma once
#include "PlaybackCache.h"
#include <list>
namespace dubwin {
struct WarmAudio {std::filesystem::path key;StemAudio stems;std::vector<float> wave;size_t bytes()const{size_t n=wave.capacity()*sizeof(float);for(auto& s:stems)n+=s.capacity()*sizeof(float);return n;}};
// UI-thread owner. Worker results are handed over only after acquire + join.
class WarmCache {
    std::list<WarmAudio> entries;
    size_t limit;
public:
    explicit WarmCache(size_t budget=size_t(1024)*1024*1024):limit(budget){}
    size_t bytes()const{size_t n=0;for(auto& e:entries)n+=e.bytes();return n;}
    bool contains(const std::filesystem::path& key)const{for(auto& e:entries)if(e.key==key)return true;return false;}
    void clear(){entries.clear();}
    void erase(const std::filesystem::path& key){entries.remove_if([&](auto& e){return e.key==key;});}
    void reserve(size_t incoming){while(!entries.empty() && bytes()+incoming>limit)entries.pop_front();}
    bool take(const std::filesystem::path& key,WarmAudio& out){for(auto it=entries.begin();it!=entries.end();++it)if(it->key==key){out=std::move(*it);entries.erase(it);return true;}return false;}
    void put(WarmAudio&& entry){erase(entry.key);size_t n=entry.bytes();if(n>limit)return;reserve(n);entries.push_back(std::move(entry));}
};
}

#pragma once
#include <array>
#include <cstdint>
#include <algorithm>
#include <vector>
namespace dubwin {
// MIDImix factory SEND ALL emits all 33 CCs, not a button release.
// Hold a short packet burst before applying any CC, so a full snapshot is atomic.
class SendAllGate {
    std::array<uint32_t,128> messages{};
    std::array<bool,128> seen{};
    unsigned size=0,unique=0;
public:
    static bool factoryCC(int cc){return (cc>=16&&cc<=31)||(cc>=46&&cc<=62);}
    bool empty()const{return size==0;}
    template<class Apply> bool push(uint32_t message,Apply apply){
        int cc=(message>>8)&127;
        if(size==messages.size())flush(apply);
        messages[size++]=message;
        if(factoryCC(cc)&&!seen[cc]){seen[cc]=true;++unique;}
        if(unique==33){clear();return true;}return false;
    }
    void clear(){size=unique=0;seen.fill(false);}
    template<class Apply> void flush(Apply apply){auto count=size;size=unique=0;seen.fill(false);for(unsigned i=0;i<count;++i)apply(messages[i]);}
};
// Move to a gap [0,size], preserving the identity of an in-progress preparation.
inline int movedIndex(int index,int from,int to){
    if(index==from)return to;
    if(from<to && index>from&&index<=to)return index-1;
    if(to<from && index>=to&&index<from)return index+1;
    return index;
}
template<class T> int reorder(std::vector<T>& list,int from,int gap,int& active){
    if(from<0||from>=int(list.size()))return -1;
    gap=std::clamp(gap,0,int(list.size()));int to=gap>from?gap-1:gap;
    if(to==from)return from;
    auto item=std::move(list[from]);list.erase(list.begin()+from);list.insert(list.begin()+to,std::move(item));
    active=movedIndex(active,from,to);return to;
}
}

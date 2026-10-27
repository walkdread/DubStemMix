#pragma once
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>
#include <cmath>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace dubwin {
struct SessionEntry {std::filesystem::path source,cache;bool fast=true;double duration=-1;};
inline std::vector<SessionEntry> readSession(const std::filesystem::path& file){
    std::ifstream in(file,std::ios::binary);std::string magic;std::getline(in,magic);
    if(magic!="DubStemMix Library 1")throw std::runtime_error("No es una biblioteca DubStemMix compatible");
    size_t count=0;if(!(in>>count)||count>1000)throw std::runtime_error("Numero de canciones invalido");
    std::vector<SessionEntry> result;result.reserve(count);
    for(size_t i=0;i<count;++i){std::string source,cache;int fast=0;double duration=0;
        if(!(in>>std::quoted(source)>>std::quoted(cache)>>fast>>duration)||source.empty()||source.size()>131072||cache.size()>131072||(fast!=0&&fast!=1)||!std::isfinite(duration)||duration<-2||duration>2678400)
            throw std::runtime_error("La biblioteca esta incompleta o contiene datos invalidos");
        SessionEntry e{std::filesystem::u8path(source),std::filesystem::u8path(cache),fast!=0,duration};
        if(e.source.is_relative())e.source=file.parent_path()/e.source;if(!e.cache.empty()&&e.cache.is_relative())e.cache=file.parent_path()/e.cache;
        result.push_back(std::move(e));
    }
    in>>std::ws;if(!in.eof())throw std::runtime_error("Datos adicionales en la biblioteca");return result;
}
inline void writeSession(const std::filesystem::path& file,const std::vector<SessionEntry>& entries){
    if(entries.size()>1000)throw std::runtime_error("Demasiadas canciones");auto temp=file;temp+=L".tmp";
    try{std::ofstream out(temp,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("No se pudo guardar la biblioteca");
        out<<"DubStemMix Library 1\n"<<entries.size()<<"\n"<<std::setprecision(17);
        for(auto& e:entries)out<<std::quoted(std::filesystem::absolute(e.source).u8string())<<' '<<std::quoted(e.cache.empty()?std::string():std::filesystem::absolute(e.cache).u8string())<<' '<<int(e.fast)<<' '<<e.duration<<'\n';
        out.close();if(!out)throw std::runtime_error("No hay espacio para guardar la biblioteca");
#ifdef _WIN32
        if(!MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("No se pudo reemplazar la biblioteca");
#else
        std::filesystem::rename(temp,file);
#endif
    }catch(...){std::error_code ec;std::filesystem::remove(temp,ec);throw;}
}
}

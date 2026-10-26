#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <dxgi.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <future>
#include <chrono>
#include "Mixer.h"
#include "onnxruntime_cxx_api.h"
#include "miniaudio.h"
#include "Separator.h"
#include "Overlap.h"

extern "C" OrtStatus* ORT_API_CALL OrtSessionOptionsAppendExecutionProvider_DML(OrtSessionOptions*, int);

namespace dubwin {
namespace fs=std::filesystem;
namespace {
constexpr const wchar_t* revision=L"8616370ed541bc183dbe15fb0d54d5f49918f47e";
constexpr uint64_t modelBytes=165612636;
const wchar_t* modelNames[]={L"drums",L"bass",L"other",L"vocals"};
const char* hashes[]={"047764dff888cfb87da917013377d4ec7a134f7419cbe486d9c339aa17975ddd",
    "b533037176b14b2df31c92a5d5b3d5660d0811b9b360d3db761964768b079961",
    "b739171a7057b3107bb0711c6222d4a619b41b13a8f04026431d30f32ad2bd71",
    "0cbe651f535415c9d26a7bb614f7d322dd5a080fa0298f2e50f478030a994dce"};
void cancelled(const std::atomic<bool>& c){if(c.load())throw std::runtime_error("Preparacion cancelada");}
struct Internet {HINTERNET h{};explicit Internet(HINTERNET value):h(value){if(!h)throw std::runtime_error("No se pudo conectar a Internet");}~Internet(){WinHttpCloseHandle(h);}operator HINTERNET()const{return h;}};
struct Hash {
    BCRYPT_ALG_HANDLE provider{};BCRYPT_HASH_HANDLE handle{};
    Hash(){if(BCryptOpenAlgorithmProvider(&provider,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 no disponible");
        if(BCryptCreateHash(provider,&handle,nullptr,0,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(provider,0);throw std::runtime_error("SHA256 no disponible");}}
    ~Hash(){BCryptDestroyHash(handle);BCryptCloseAlgorithmProvider(provider,0);}
};
std::string sha256(const fs::path& file,const std::atomic<bool>& cancel) {
    std::ifstream in(file,std::ios::binary);if(!in)throw std::runtime_error("No se pudo leer un archivo");
    Hash hash;std::vector<unsigned char> bytes(1024*1024);
    while(in){cancelled(cancel);in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        if(in.gcount()>0 && BCryptHashData(hash.handle,bytes.data(),static_cast<ULONG>(in.gcount()),0)<0)throw std::runtime_error("Error SHA256");}
    if(!in.eof())throw std::runtime_error("Error al leer archivo");
    unsigned char digest[32];if(BCryptFinishHash(hash.handle,digest,32,0)<0)throw std::runtime_error("Error SHA256");
    std::ostringstream out;for(auto b:digest)out<<std::hex<<std::setw(2)<<std::setfill('0')<<int(b);return out.str();
}
constexpr const wchar_t* fastRevision=L"d54ed9eb60e258ea82131c6ee14578628816456a";
constexpr const char* fastHash="d05c269d0178d2a72ad484b10b11dd370193fc923201c3b27a99f848745db70a";
fs::path modelPath(int row,bool fast=false){if(fast)return dataRoot()/L"Models"/fastRevision/L"htdemucs_fp16weights.onnx";return dataRoot()/L"Models"/revision/(std::wstring(L"htdemucs_ft_")+modelNames[row]+L"_fp16weights.onnx");}
void ensureModel(int row,std::atomic<bool>& cancel,const Progress& progress,bool fast=false) {
    const auto expectedHash=fast?fastHash:hashes[row];
    const auto final=modelPath(row,fast);fs::create_directories(final.parent_path());
    progress(L"Verificando modelo "+std::wstring(modelNames[row])+L"...");
    if(fs::exists(final) && fs::file_size(final)==modelBytes && sha256(final,cancel)==expectedHash)return;
    const auto part=fs::path(final.wstring()+L".part");
    try {
        Internet session(WinHttpOpen(L"DubStemMix-Windows/0.2",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
        WinHttpSetTimeouts(session,10000,15000,30000,30000);
        Internet connection(WinHttpConnect(session,L"huggingface.co",INTERNET_DEFAULT_HTTPS_PORT,0));
        const auto url=std::wstring(fast?L"/StemSplitio/htdemucs-onnx/resolve/":L"/StemSplitio/htdemucs-ft-onnx/resolve/")+std::wstring(fast?fastRevision:revision)+L"/"+final.filename().wstring();
        Internet request(WinHttpOpenRequest(connection,L"GET",url.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
        if(!WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0) || !WinHttpReceiveResponse(request,nullptr))
            throw std::runtime_error("Descarga fallida. Comprueba Internet e intenta de nuevo");
        DWORD status=0,size=sizeof status;
        if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX) || status!=200)
            throw std::runtime_error("El servidor no permitio descargar el modelo");
        std::ofstream out(part,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("No se pudo guardar el modelo");
        std::vector<char> bytes(256*1024);uint64_t done=0,lastReport=0;
        for(;;){cancelled(cancel);DWORD read=0;if(!WinHttpReadData(request,bytes.data(),static_cast<DWORD>(bytes.size()),&read))throw std::runtime_error("Descarga interrumpida");
            if(!read)break;done+=read;if(done>modelBytes)throw std::runtime_error("Tamano de modelo inesperado");
            out.write(bytes.data(),read);if(!out)throw std::runtime_error("No hay espacio para guardar el modelo");
            if(done-lastReport>2*1024*1024){progress(L"Modelo "+std::to_wstring(row+1)+L"/4: "+std::to_wstring(done/(1024*1024))+L" / 158 MB");lastReport=done;}}
        out.close();if(done!=modelBytes || sha256(part,cancel)!=expectedHash)throw std::runtime_error("El modelo esta incompleto o no paso la verificacion SHA256");
        if(!MoveFileExW(part.c_str(),final.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("No se pudo finalizar el modelo");
    }catch(...){std::error_code ec;fs::remove(part,ec);throw;}
}
std::vector<float> decode(const fs::path& file,unsigned rate,std::atomic<bool>& cancel) {
    ma_decoder d{};auto config=ma_decoder_config_init(ma_format_f32,2,rate);
    if(ma_decoder_init_file_w(file.c_str(),&config,&d)!=MA_SUCCESS)throw std::runtime_error("No se pudo leer el audio. Prueba un WAV");
    std::vector<float> data;
    try {float block[8192];for(;;){cancelled(cancel);ma_uint64 read=0;auto r=ma_decoder_read_pcm_frames(&d,block,4096,&read);
        if(r!=MA_SUCCESS && r!=MA_AT_END)throw std::runtime_error("Error de lectura de audio");
        if(data.size()+read*2>size_t(rate)*600*2)throw std::runtime_error("Esta version permite canciones de hasta 10 minutos");
        for(size_t i=0;i<read*2;++i)if(!std::isfinite(block[i]))throw std::runtime_error("El audio contiene muestras invalidas");
        data.insert(data.end(),block,block+read*2);if(!read || r==MA_AT_END)break;}}
    catch(...){ma_decoder_uninit(&d);throw;}ma_decoder_uninit(&d);
    if(data.empty())throw std::runtime_error("El archivo no tiene audio");return data;
}
void writeWave(const fs::path& file,const std::vector<float>& data) {
    std::ofstream out(file,std::ios::binary);if(!out)throw std::runtime_error("No se pudo escribir el stem");
    auto u32=[&](uint32_t v){out.write(reinterpret_cast<const char*>(&v),4);};auto u16=[&](uint16_t v){out.write(reinterpret_cast<const char*>(&v),2);};
    uint32_t size=static_cast<uint32_t>(data.size()*4);
    out.write("RIFF",4);u32(36+size);out.write("WAVEfmt ",8);u32(16);u16(3);u16(2);u32(44100);u32(44100*8);u16(8);u16(32);
    out.write("data",4);u32(size);out.write(reinterpret_cast<const char*>(data.data()),size);out.close();
    if(!out)throw std::runtime_error("No hay espacio para los stems");
}
}
fs::path configRoot(){PWSTR value=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&value)))throw std::runtime_error("No se encontro la carpeta de datos");fs::path root(value);CoTaskMemFree(value);return root/L"DubStemMix";}
fs::path dataRoot(){auto root=configRoot();wchar_t custom[32768]{};GetPrivateProfileStringW(L"Storage",L"Folder",L"",custom,32768,(root/L"settings.ini").c_str());return custom[0]?fs::path(custom):root;}
void setDataRoot(const fs::path& folder){fs::create_directories(folder);auto root=configRoot();fs::create_directories(root);
    if(!WritePrivateProfileStringW(L"Storage",L"Folder",folder.c_str(),(root/L"settings.ini").c_str()))throw std::runtime_error("No se pudo guardar la carpeta de datos");}

std::vector<std::pair<int,std::wstring>> gpuAdapters(){
    std::vector<std::pair<int,std::wstring>> result;IDXGIFactory1* factory=nullptr;
    if(FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(&factory))))return result;
    for(UINT i=0;;++i){IDXGIAdapter1* a=nullptr;if(factory->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND)break;if(!a)break;
        DXGI_ADAPTER_DESC1 d{};if(SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))result.emplace_back(int(i),d.Description);a->Release();}
    factory->Release();return result;
}
fs::path prepareSong(const fs::path& file,std::atomic<bool>& cancel,const Progress& progress,SeparationOptions settings) {
    cancelled(cancel);progress(L"Leyendo cancion...");auto fingerprint=sha256(file,cancel);
    auto folder=dataRoot()/L"Stems"/(fingerprint+(settings.fast?"-d54ed9eb":"-8616370e"));
    bool cached=false;
    if(fs::exists(folder/L"complete.txt")){
        std::ifstream manifest(folder/L"complete.txt");cached=true;
        for(int row=0;row<4;++row){std::string expected;manifest>>expected;const auto wav=folder/(std::wstring(modelNames[row])+L".wav");
            if(expected.empty() || !fs::exists(wav) || sha256(wav,cancel)!=expected){cached=false;break;}}
    }
    if(!cached){
        for(int row=0;row<(settings.fast?1:4);++row){cancelled(cancel);ensureModel(row,cancel,progress,settings.fast);}
        auto mix=decode(file,44100,cancel);const size_t total=mix.size()/2;
        auto staging=fs::path(folder.wstring()+L".part-"+std::to_wstring(GetCurrentProcessId()));
        fs::create_directories(staging);
        try {
            Ort::Env env(ORT_LOGGING_LEVEL_WARNING,"DubStemMix");
            const auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
            const int64_t shape[]={1,2,static_cast<int64_t>(modelFrames)};
            std::vector<float> input(modelFrames*2,0);
            const size_t chunks=(total+stride-1)/stride;
            for(int row=0;row<(settings.fast?1:4);++row){
                cancelled(cancel);progress(L"Preparando "+std::wstring(modelNames[row])+L"...");
                Ort::SessionOptions options;options.SetIntraOpNumThreads(2);options.SetInterOpNumThreads(1);options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
                options.AddConfigEntry("session.intra_op.allow_spinning","0");
                if(settings.gpu>=0){options.DisableMemPattern();options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
                    Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(options,settings.gpu));}
                Ort::Session session(env,modelPath(row,settings.fast).c_str(),options);
                std::vector<Accumulator> output;for(int i=0;i<(settings.fast?4:1);++i)output.emplace_back(total);
                for(size_t chunk=0;chunk<chunks;++chunk){
                    cancelled(cancel);const size_t start=chunk*stride,length=std::min(modelFrames,total-start);
                    progress(std::wstring(settings.gpu>=0?L"GPU / ":L"CPU / ")+std::wstring(settings.fast?L"Rapido":modelNames[row])+L" — bloque "+std::to_wstring(chunk+1)+L"/"+std::to_wstring(chunks)+L" ("+std::to_wstring(row+1)+(settings.fast?L"/1)":L"/4)"));
                    std::fill(input.begin(),input.end(),0);
                    for(size_t i=0;i<length;++i){input[i]=mix[(start+i)*2];input[modelFrames+i]=mix[(start+i)*2+1];}
                    auto tensor=Ort::Value::CreateTensor<float>(memory,input.data(),input.size(),shape,3);
                    const char* inputs[]={"mix"};const char* outputs[]={"stems"};
                    auto result=session.Run(Ort::RunOptions{nullptr},inputs,&tensor,1,outputs,1);
                    auto info=result.at(0).GetTensorTypeAndShapeInfo();
                    if(info.GetShape()!=std::vector<int64_t>({1,4,2,static_cast<int64_t>(modelFrames)}) || info.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
                        throw std::runtime_error("Salida ONNX incompatible");
                    for(int stem=0;stem<(settings.fast?4:1);++stem){
                        const float* data=result[0].GetTensorData<float>()+(settings.fast?stem:row)*2*modelFrames;
                        for(size_t i=0;i<modelFrames*2;++i)if(!std::isfinite(data[i]))throw std::runtime_error("El modelo produjo audio invalido");
                        output[stem].add(data,data+modelFrames,start,length);
                    }
                }
                for(int stem=0;stem<(settings.fast?4:1);++stem){cancelled(cancel);output[stem].normalize();
                    writeWave(staging/(std::wstring(modelNames[settings.fast?stem:row])+L".wav"),output[stem].samples);}

            }
            std::ofstream manifest(staging/L"complete.txt");
            for(int row=0;row<4;++row)manifest<<sha256(staging/(std::wstring(modelNames[row])+L".wav"),cancel)<<"\n";
            manifest.close();if(!manifest)throw std::runtime_error("No se pudo finalizar la preparacion");
            cancelled(cancel);if(fs::exists(folder))fs::remove_all(folder);fs::rename(staging,folder);
        }catch(...){std::error_code ec;fs::remove_all(staging,ec);throw;}
    }
    return folder;
}
std::array<std::vector<float>,4> loadPrepared(const fs::path& folder,std::atomic<bool>& cancel){
    std::ifstream manifest(folder/L"complete.txt");
    for(int row=0;row<4;++row){std::string hash;manifest>>hash;if(hash.empty() || sha256(folder/(std::wstring(modelNames[row])+L".wav"),cancel)!=hash)throw std::runtime_error("Stems incompletos. Reintenta el analisis.");}
    std::array<std::vector<float>,4> result;const int order[]={3,0,1,2};
    for(int t=0;t<4;++t)result[t]=decode(folder/(std::wstring(modelNames[order[t]])+L".wav"),48000,cancel);
    for(auto& stem:result)if(stem.size()!=result[0].size())throw std::runtime_error("Stems desalineados");
    return result;
}
std::array<std::vector<float>,4> separateSong(const fs::path& file,std::atomic<bool>& cancel,const Progress& progress){
    auto folder=prepareSong(file,cancel,progress,{false,-1});return loadPrepared(folder,cancel);
}
}

namespace dubwin {
int separationSelfTest() {
    try {
        const auto file=dataRoot()/L"Test"/L"synthetic.wav";fs::create_directories(file.parent_path());
        std::vector<float> signal(44100*2,0);
        for(size_t i=0;i<44100;++i){const float sample=0.1f*std::sin(float(i)*0.062689377f)+0.07f*std::sin(float(i)*0.013819206f);
            signal[i*2]=sample;signal[i*2+1]=sample;}
        writeWave(file,signal);std::atomic<bool> cancel{false};
        const auto result=separateSong(file,cancel,[](const std::wstring& text){std::fwprintf(stderr,L"%ls\n",text.c_str());std::fflush(stderr);});
        size_t length=result[0].size();if(length<95000 || length>97000)throw std::runtime_error("Resample length failed");
        double energy=0;for(const auto& stem:result){if(stem.size()!=length)throw std::runtime_error("Stem alignment failed");for(float x:stem){if(!std::isfinite(x))throw std::runtime_error("Non-finite audio");energy+=double(x)*x;}}
        if(energy<1e-6)throw std::runtime_error("Separation produced silence");
        // Real inference runs beside a live mixer render loop, without sharing its buffers.
        Mixer mixer;mixer.strip[0].samples.assign(48000*2*600,0.01f);mixer.playing=true;
        auto future=std::async(std::launch::async,[&]{return prepareSong(file,cancel,[](const std::wstring& t){std::fwprintf(stderr,L"%ls\n",t.c_str());},{true,-1});});
        float rendered[960];size_t blocks=0;
        while(future.wait_for(std::chrono::milliseconds(10))!=std::future_status::ready){mixer.render(rendered,480);++blocks;for(float v:rendered)if(!std::isfinite(v))throw std::runtime_error("Concurrent mixer nonfinite");}
        auto fastFolder=future.get();if(blocks && mixer.position.load()!=blocks*480)throw std::runtime_error("Concurrent mixer interrupted");
        auto fast=loadPrepared(fastFolder,cancel);double fastEnergy=0;
        for(auto& stem:fast){if(stem.size()!=length)throw std::runtime_error("Fast model length failed");for(float v:stem){if(!std::isfinite(v))throw std::runtime_error("Fast nonfinite");fastEnergy+=double(v)*v;}}
        if(fastEnergy<1e-6)throw std::runtime_error("Fast model silence");
        if(prepareSong(file,cancel,[](const std::wstring&){},{true,-1})!=fastFolder)throw std::runtime_error("Cache failed");
        std::fprintf(stderr,"PASS: fast model + cache; four real ONNX models, output shape, finite audio, matching stem lengths and resampling\n");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
}

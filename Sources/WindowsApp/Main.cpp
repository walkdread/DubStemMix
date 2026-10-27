#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <shlobj.h>
#include <filesystem>
#include <memory>
#include <string>
#include <cwctype>
#include <thread>
#include <mutex>
#include <fstream>
#include <utility>
#include "Separator.h"
#include "Theme.h"
#include "Dialogs.h"
#include "PlaybackCache.h"
#include "WarmCache.h"
#include "Session.h"
#include <set>
#include "miniaudio.h"
#include "Mixer.h"

namespace {
constexpr UINT midiMessage = WM_APP + 1;
constexpr int idPlay=10,idPause=11,idStop=12,idAudio=13,idMidi=14,idDevices=15,idNext=16,idTogglePlay=17;
constexpr int idSong=20,idCancel=21,idPanic=22,idClearSolo=24,idDataFolder=23,idClear=30,idInstrument=40,idFX=60,idEffectDial=700;
constexpr int idLoad=100,idLevel=200,idMute=300,idSolo=400,idSend=500,idReturn=600,idMaster=610;
HWND window{},status{},audioLabel{},midiLabel{};
HWND levels[6]{},names[6]{},mutes[6]{},solos[6]{},sends[6][3]{},returns[3]{},masterSlider{};
HFONT font{},headingFont{},smallFont{};
struct Placement{HWND h;int x,y,w,height;};std::vector<Placement> placements;
HWND clockLabel{},modeLabel{};
bool uiReady=false,uiTestMode=false;
unsigned long paintCount=0;
void relayout();
float layoutX=1,layoutY=1;
int sx(int v){return static_cast<int>(std::lround(v*layoutX));}
int sy(int v){return static_cast<int>(std::lround(v*layoutY));}
HWND songLabel{},sendLabels[6][3]{},clearButtons[6]{},fxButtons[6]{},fxDials[6]{};
std::thread preparation;
std::atomic<bool> preparing{false},cancelPreparation{false},preparationDone{false};
std::mutex progressMutex;
std::wstring progressText,preparationError,preparationTitle;
struct Song {std::filesystem::path source,cache;std::wstring state=L"En cola";bool fast=true;double duration=-1;};
std::vector<Song> songs;
int activeJob=-1;
HWND libraryWindow{},songList{},libraryStatus{},libraryTotal{},sessionLabel{},scrollRail{};
std::filesystem::path sessionPath,loadedCache,loadCache;
bool sessionDirty=false;
constexpr int idSessionSave=870,idSessionSaveAs=871,idSessionOpen=872,idSessionNew=873,idListUp=905,idListDown=906,idListRail=907;
dubwin::WarmCache warmCache;
std::thread warmer,metadataThread;
std::atomic<bool> warming{false},warmDone{false},cancelWarm{false},metadataBusy{false},metadataDone{false};
dubwin::WarmAudio warmResult;
std::filesystem::path warmFolder,metadataSource,metadataCache;
std::wstring warmError;
std::set<std::filesystem::path> warmFailed;
bool waitingWarm=false;
double metadataDuration=-2;
ULONGLONG selectionChanged=0;
unsigned warmHits=0;
void pumpBackground();
void stopBackground();
void pollWarm();
void updateLibrarySummary();
int meterSegments(float peak);
RECT channelMeterRect(int channel);
bool queuePaused=false,fastMode=true;
int gpuDevice=-1,learnTransport=-1;
int transportNotes[3]={25,20,26}; // Bank Left: Play; SOLO+MUTE 7: pause; Bank Right: restart.
std::thread loader;
std::atomic<bool> loading{false},loadDone{false},cancelLoad{false};
std::wstring loadError,loadTitle;
std::filesystem::path loadedSource,loadSource;
bool loadedFast=true,loadFast=true,resumeAfterLoad=false;
std::vector<float> waveform,preparedWaveform;
bool waveDragging=false;
int selectedOutput=-1;
std::wstring preferredOutput;
std::vector<std::wstring> deviceNames;
constexpr int idOutputBase=1000;
std::filesystem::path jobCache;
std::wstring jobBackend,lastStemError;
std::vector<std::pair<int,std::wstring>> adapters;
constexpr int idLibrary=800,idLoadReady=801,idRemoveSong=802,idRetrySong=803,idQueuePause=804,
    idFast=810,idQuality=811,idCPU=812,idGPU=820,idLearn=850,idAbout=860,idExit=861,idLastError=862;

std::array<std::vector<float>,4> preparedStems;
float instrumentValues[2][2]={{0.5f,0.45f},{0.5f,0.5f}};
dubwin::Pickup instrumentPickup[2][2],fxPickup[6];
float fxValues[6]={0.57f,0.5f,0.7f,0.4f,0.3f,0.7f};
bool instrumentOn[2][2]{},fxOn[6]{};
void syncInstrumentUI(int t);
void resetGestures();
bool ensureAudio();
void instrumentAction(int index,int gesture,bool on);
void effectAction(int action,bool on);
void startPreparation();
void showLibrary();
void refreshLibrary();
void buildMenus();
void saveLibrary();
void loadLibrary();
void pollPreparation();
std::unique_ptr<dubwin::Mixer> mixer;
ma_context context{}; ma_device device{};
bool contextReady=false,deviceReady=false;
std::vector<ma_device_id> deviceIds;
HMIDIIN midiIn{}; HMIDIOUT midiOut{};
bool soloHeld=false;
bool pressed[128]{};
dubwin::Pickup levelPickup[6],sendPickup[6][3],returnPickup[3],masterPickup;

// Native static controls repaint even for identical text. Avoid timer-induced erasing.
void setText(HWND h,const wchar_t* value){
    if(!h)return;int n=GetWindowTextLengthW(h);std::wstring old(size_t(n)+1,L'\0');GetWindowTextW(h,old.data(),n+1);old.resize(n);
    if(old!=value)SetWindowTextW(h,value);
}
void error(const wchar_t* text) {theme::message(window,text,L"DubStemMix — Windows alpha",MB_OK|MB_ICONWARNING);}
std::wstring wide(const char* text) {
    int n=MultiByteToWideChar(CP_UTF8,0,text,-1,nullptr,0);
    if(n<=0)return L"Dispositivo";
    std::wstring s(static_cast<size_t>(n),L'\0'); MultiByteToWideChar(CP_UTF8,0,text,-1,s.data(),n); s.pop_back(); return s;
}
HWND control(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id=0) {
    if(wcscmp(cls,L"BUTTON")==0) style=(style&~0xFu)|BS_OWNERDRAW;
    auto c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,sx(x),sy(y),sx(w),sy(h),window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    placements.push_back({c,x,y,w,h});SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE); return c;
}
HWND dial(int x,int y,int w,int h,int id,float value,bool fader=false) {
    auto c=CreateWindowExW(0,L"DubDial",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP,sx(x),sy(y),sx(w),sy(h),window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),fader?reinterpret_cast<void*>(1):nullptr);
    placements.push_back({c,x,y,w,h});
    SendMessageW(c,TBM_SETPOS,TRUE,static_cast<LPARAM>(std::lround(value*127)));return c;
}
void led(int note,bool on) {if(midiOut)midiOutShortMsg(midiOut,0x90u|(static_cast<DWORD>(note)<<8)|(on?127u<<16:0));}
void syncButtons(int t) {
    if(mixer->isInstrument(t)){syncInstrumentUI(t);return;}
    setText(mutes[t],mixer->strip[t].solo.load()?L"SOLO":(mixer->strip[t].mute.load()?L"MUTED":L"MUTE"));
    setText(solos[t],mixer->strip[t].solo.load()?L"SOLO ON":L"SOLO");
    led(1+3*t,mixer->strip[t].mute.load()); led(2+3*t,mixer->strip[t].solo.load());if(t<4)led(3+3*t,mixer->strip[t].solo.load());
}
void resetPickup() {
    for(auto& p:levelPickup)p.reset(); for(auto& row:sendPickup)for(auto& p:row)p.reset();
    for(auto& p:returnPickup)p.reset(); masterPickup.reset();
    for(auto& row:instrumentPickup)for(auto& p:row)p.reset();for(auto& p:fxPickup)p.reset();
}
void audioCallback(ma_device* d,void* out,const void*,ma_uint32 frames) {
    static_cast<dubwin::Mixer*>(d->pUserData)->render(static_cast<float*>(out),frames);
}
void stopAudio() {if(deviceReady)ma_device_stop(&device);}
void closeAudio() {if(deviceReady){ma_device_uninit(&device);deviceReady=false;}}
bool openAudio() {
    closeAudio();
    const int index=selectedOutput;
    if(index<0 || static_cast<size_t>(index)>=deviceIds.size()){error(L"Selecciona una salida de audio.");return false;}
    auto cfg=ma_device_config_init(ma_device_type_playback);
    cfg.playback.pDeviceID=&deviceIds[static_cast<size_t>(index)];
    cfg.playback.format=ma_format_f32; cfg.playback.channels=2;
    cfg.sampleRate=dubwin::rate; cfg.periodSizeInFrames=512;
    cfg.playback.shareMode=ma_share_mode_shared;
    cfg.dataCallback=audioCallback;cfg.pUserData=mixer.get();
    if(ma_device_init(&context,&cfg,&device)!=MA_SUCCESS){error(L"No se pudo abrir esa salida. Conecta la interfaz y pulsa Actualizar audio.");return false;}
    deviceReady=true; return true;
}
void updateAudioLabel(){
    auto label=selectedOutput>=0 && size_t(selectedOutput)<deviceNames.size()?L"OUT: "+deviceNames[selectedOutput]:L"OUT: sin dispositivo";
    setText(audioLabel,label.c_str());
}
void enumerateAudio() {
    if(mixer->playing.load()){error(L"Pulsa Pausa antes de cambiar la salida.");return;}
    stopAudio();closeAudio();deviceIds.clear();deviceNames.clear();selectedOutput=-1;
    ma_device_info* infos=nullptr;ma_uint32 count=0;
    if(!contextReady || ma_context_get_devices(&context,&infos,&count,nullptr,nullptr)!=MA_SUCCESS){error(L"No se pudieron enumerar las salidas de audio.");return;}
    for(ma_uint32 i=0;i<count;++i){deviceIds.push_back(infos[i].id);deviceNames.push_back(wide(infos[i].name));if(infos[i].isDefault)selectedOutput=int(i);}
    for(size_t i=0;i<deviceNames.size();++i)if(deviceNames[i]==preferredOutput)selectedOutput=int(i);
    if(selectedOutput<0 && count)selectedOutput=0;updateAudioLabel();buildMenus();
}
void CALLBACK midiCallback(HMIDIIN,UINT message,DWORD_PTR user,DWORD_PTR data,DWORD_PTR) {
    if(message==MIM_DATA)PostMessageW(reinterpret_cast<HWND>(user),midiMessage,static_cast<WPARAM>(data),0);
}
bool isMidimix(const wchar_t* text) {
    std::wstring s; for(;*text;++text)if(!iswspace(*text))s.push_back(static_cast<wchar_t>(towlower(*text)));
    return s.find(L"midimix")!=std::wstring::npos;
}
void closeMidi() {
    if(midiIn){midiInStop(midiIn);midiInReset(midiIn);midiInClose(midiIn);midiIn=nullptr;}
    if(midiOut){for(int n=1;n<=27;++n)led(n,false);midiOutClose(midiOut);midiOut=nullptr;}
    if(mixer){mixer->panic();for(auto& row:instrumentOn)for(auto& on:row)on=false;}
    soloHeld=false; std::fill(std::begin(pressed),std::end(pressed),false);
}
void connectMidi() {
    closeMidi(); resetPickup();
    for(UINT i=0;i<midiOutGetNumDevs();++i){MIDIOUTCAPSW caps{};if(midiOutGetDevCapsW(i,&caps,sizeof caps)==MMSYSERR_NOERROR && isMidimix(caps.szPname)){
        if(midiOutOpen(&midiOut,i,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR)midiOut=nullptr;break;}}
    for(UINT i=0;i<midiInGetNumDevs();++i){MIDIINCAPSW caps{};if(midiInGetDevCapsW(i,&caps,sizeof caps)==MMSYSERR_NOERROR && isMidimix(caps.szPname)){
        if(midiInOpen(&midiIn,i,reinterpret_cast<DWORD_PTR>(midiCallback),reinterpret_cast<DWORD_PTR>(window),CALLBACK_FUNCTION)==MMSYSERR_NOERROR){
            if(midiInStart(midiIn)!=MMSYSERR_NOERROR){midiInClose(midiIn);midiIn=nullptr;}
        }else midiIn=nullptr;
        break;}}
    for(int t=0;t<6;++t)syncButtons(t);
    setText(midiLabel,midiIn?L"MIDI: MIDImix conectado":L"MIDI: desconectado");
}
void midiValue(int value,std::atomic<float>& target,dubwin::Pickup& pickup,HWND h) {
    if(pickup.accept(value,target.load())){target.store(value/127.0f);SendMessageW(h,TBM_SETPOS,TRUE,value);}
}
void handleMidi(DWORD message) {
    const int type=message&0xF0, d1=(message>>8)&127,d2=(message>>16)&127;
    if(type==0x90 && d2 && learnTransport>=0){
        if(d1==3||d1==6||d1==9||d1==12){setText(midiLabel,L"REC ARM 1-4 reservado a SOLO");return;}
        for(int i=0;i<3;++i)if(i!=learnTransport && transportNotes[i]==d1)transportNotes[i]=-1;
        transportNotes[learnTransport]=d1;learnTransport=-1;saveLibrary();
        setText(midiLabel,L"Boton aprendido y guardado");return;
    }
    if(type==0xB0){
        if(d1==62)midiValue(d2,mixer->master,masterPickup,masterSlider);
        for(int t=0;t<6;++t){
            if(d1==dubwin::faderCC[t])midiValue(d2,mixer->strip[t].level,levelPickup[t],levels[t]);
            for(int b=0;b<3;++b)if(d1==dubwin::knobCC[t][b]) {
                if(mixer->isInstrument(t) && b<2){
                    if(instrumentPickup[t-4][b].accept(d2,instrumentValues[t-4][b])){
                        instrumentValues[t-4][b]=d2/127.0f;SendMessageW(sends[t][b],TBM_SETPOS,TRUE,d2);
                        SendMessageW(window,WM_HSCROLL,0,reinterpret_cast<LPARAM>(sends[t][b]));
                        instrumentPickup[t-4][b].caught=true;
                    }
                }else {int bus=mixer->isInstrument(t)?0:b;midiValue(d2,mixer->strip[t].send[bus],sendPickup[t][b],sends[t][b]);}
            }
        }
        const int effectCC[]={54,55,56,58,59,60};
        for(int e=0;e<6;++e)if(d1==effectCC[e] && fxPickup[e].accept(d2,fxValues[e])){
            SendMessageW(fxDials[e],TBM_SETPOS,TRUE,d2);SendMessageW(window,WM_HSCROLL,0,reinterpret_cast<LPARAM>(fxDials[e]));fxPickup[e].caught=true;
        }
        if(d1==57)midiValue(d2,mixer->returns[0],returnPickup[0],returns[0]);
        if(d1==61)midiValue(d2,mixer->returns[1],returnPickup[1],returns[1]);
    }else if(type==0x90 || type==0x80){
        const bool down=type==0x90 && d2!=0;
        if(d1==27 && transportNotes[0]!=27 && transportNotes[1]!=27 && transportNotes[2]!=27){soloHeld=down;return;}
        const bool edge=down && !pressed[d1];pressed[d1]=down;
        for(int t=0;t<4;++t)if(d1==3+3*t){if(edge){mixer->strip[t].solo=!mixer->strip[t].solo.load();syncButtons(t);}return;}
        for(int i=0;i<3;++i)if(edge && d1==transportNotes[i]){SendMessageW(window,WM_COMMAND,i==0?idPlay:(i==1?idPause:idStop),0);return;}
        for(int t=4;t<6;++t)if(mixer->isInstrument(t)){
            if(d1==1+3*t){instrumentAction(t-4,0,down);return;}
            if(d1==3+3*t){instrumentAction(t-4,1,down);return;}
        }
        if(d1==19){effectAction(0,down);return;}if(d1==21){effectAction(1,down);return;}
        if(d1==22){if(edge)effectAction(2,true);return;}if(d1==24){effectAction(3,down);return;}
        if(!edge)return;

        for(int t=0;t<6;++t){
            if(d1==1+3*t){auto& state=soloHeld?mixer->strip[t].solo:mixer->strip[t].mute;state.store(!state.load());syncButtons(t);}
            if(d1==2+3*t){auto& state=mixer->strip[t].solo;state.store(!state.load());syncButtons(t);}
        }
    }
}
RECT waveRect(){return RECT{sx(240),sy(68),sx(1252),sy(108)};}
RECT waveInner(){auto r=waveRect();InflateRect(&r,-6,-3);return r;}
void rebuildWaveform(){
    waveform.assign(dubwin::waveBins,0);size_t frames=mixer->totalFrames();if(!frames)return;
    for(auto& strip:mixer->strip)for(size_t f=0;f<strip.samples.size()/2;++f){auto bin=std::min(dubwin::waveBins-1,f*dubwin::waveBins/frames);waveform[bin]=std::max(waveform[bin],std::max(std::abs(strip.samples[f*2]),std::abs(strip.samples[f*2+1])));}
    float top=*std::max_element(waveform.begin(),waveform.end());if(top>0)for(auto& p:waveform)p/=top;
}
void seekWave(int x){
    auto r=waveInner();auto total=mixer->totalFrames();if(!total||r.right<=r.left)return;
    auto frame=uint64_t(std::clamp(double(x-r.left)/double(r.right-r.left-1),0.0,1.0)*double(total-1));
    mixer->seek(frame);if(!ensureAudio())return;auto rect=waveRect();InvalidateRect(window,&rect,FALSE);
}
void loadTrack(int track) {
    if(loading){error(L"Espera a que termine la carga seleccionada.");return;}
    if(mixer->playing.load()){error(L"Pulsa Detener antes de cargar pistas. La carga reinicia la posicion de todas las pistas.");return;}
    wchar_t path[32768]{}; OPENFILENAMEW dialog{};dialog.lStructSize=sizeof dialog;dialog.hwndOwner=window;
    dialog.lpstrFilter=L"Audio (WAV, FLAC, MP3)\0*.wav;*.flac;*.mp3\0\0";
    dialog.lpstrFile=path;dialog.nMaxFile=32768;dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetOpenFileNameW(&dialog))return;
    stopAudio();mixer->reset();resetGestures();
    ma_decoder decoder{};auto cfg=ma_decoder_config_init(ma_format_f32,2,dubwin::rate);
    if(ma_decoder_init_file_w(path,&cfg,&decoder)!=MA_SUCCESS){error(L"No se pudo leer el archivo. Prueba con un WAV PCM.");return;}
    setText(status,L"Cargando y convirtiendo audio... Espera a que termine.");UpdateWindow(window);
    bool ok=true;std::vector<float> data;
    // Bound decoded song data to 1 GiB, leaving ample RAM for Windows on a 16 GiB PC.
    size_t used=0;for(int i=0;i<6;++i)if(i!=track)used+=mixer->strip[i].samples.size();
    const size_t limit=(size_t(1024)*1024*1024)/sizeof(float)-used;
    try {
        float chunk[8192];
        for(;;){ma_uint64 read=0;const auto result=ma_decoder_read_pcm_frames(&decoder,chunk,4096,&read);
            if(result!=MA_SUCCESS && result!=MA_AT_END){ok=false;break;}
            if(data.size()+static_cast<size_t>(read)*2>limit){ok=false;break;}
            for(size_t i=0;i<static_cast<size_t>(read)*2;++i)if(!std::isfinite(chunk[i]))chunk[i]=0;
            data.insert(data.end(),chunk,chunk+static_cast<size_t>(read)*2);
            if(read==0 || result==MA_AT_END)break;
        }
    }catch(const std::bad_alloc&){ok=false;}
    ma_decoder_uninit(&decoder);
    if(!ok || data.empty()){error(L"No se pudo cargar: archivo invalido o limite total de audio (1 GB) excedido. La pista anterior se conserva.");return;}
    mixer->strip[track].samples=std::move(data);mixer->strip[track].mute=false;mixer->strip[track].solo=false;
    syncInstrumentUI(track);rebuildWaveform();loadedSource.clear();loadedCache.clear();
    auto name=std::filesystem::path(path).filename().wstring(); setText(names[track],name.c_str());
    setText(status,L"Pista cargada. Carga las otras pistas y pulsa Reproducir.");InvalidateRect(window,nullptr,FALSE);
}
void updateStatus() {
    pollPreparation();
    theme::active(GetDlgItem(window,idPlay),mixer->playing.load());
    theme::active(GetDlgItem(window,idPause),!mixer->playing.load()&&mixer->position.load()>0);
    for(int t=0;t<6;++t){theme::active(mutes[t],mixer->isInstrument(t)?instrumentOn[t-4][0]:(mixer->strip[t].mute.load()||mixer->strip[t].solo.load()));theme::active(solos[t],mixer->isInstrument(t)?instrumentOn[t-4][1]:mixer->strip[t].solo.load());}
    bool anySolo=false;for(auto& s:mixer->strip)anySolo|=s.solo.load();
    setText(GetDlgItem(window,idClearSolo),anySolo?L"CLEAR SOLO":L"SOLO");theme::active(GetDlgItem(window,idClearSolo),anySolo);
    for(int i=0;i<6;++i)theme::active(fxButtons[i],fxOn[i]);
    auto elapsed=mixer->position.load()/dubwin::rate,total=mixer->totalFrames()/dubwin::rate;
    wchar_t clock[80];swprintf_s(clock,L"%02llu:%02llu / %02llu:%02llu",(unsigned long long)(elapsed/60),(unsigned long long)(elapsed%60),(unsigned long long)(total/60),(unsigned long long)(total%60));setText(clockLabel,clock);
    auto mode=std::wstring(fastMode?L"Rapido":L"Calidad")+(gpuDevice<0?L" / CPU":L" / GPU experimental");setText(modeLabel,mode.c_str());
    if(preparing.load()){std::lock_guard<std::mutex> lock(progressMutex);setText(status,progressText.c_str());}
    if(libraryStatus){std::lock_guard<std::mutex> lock(progressMutex);setText(libraryStatus,loading?L"Cargando audio preparado...":progressText.c_str());}
    static size_t lastPosition=~size_t(0);static float lastPeak=-1;
    auto position=mixer->position.load();auto peak=mixer->peak.load();
    if(position!=lastPosition){RECT wave{sx(240),sy(68),sx(1252),sy(108)};InvalidateRect(window,&wave,FALSE);lastPosition=position;}
    static std::array<int,8> lastMeters{{-1,-1,-1,-1,-1,-1,-1,-1}};
    for(int t=0;t<8;++t){int count=meterSegments(t<6?mixer->channelPeaks[t].load():mixer->returnPeaks[t-6].load());if(count!=lastMeters[t]){auto r=channelMeterRect(t);InvalidateRect(window,&r,FALSE);lastMeters[t]=count;}}
    if(peak!=lastPeak){RECT meter{sx(1230),sy(535),sx(1242),sy(706)};InvalidateRect(window,&meter,FALSE);lastPeak=peak;}
    if(deviceReady && mixer->playing.load() && !ma_device_is_started(&device)){
        mixer->playing=false; error(L"Se interrumpio la salida de audio. Pulsa Detener y Actualizar audio para reconectar.");
    }
    const auto seconds=mixer->position.load()/dubwin::rate;
    wchar_t buffer[256];swprintf_s(buffer,L"%s  |  %llu:%02llu  |  Pico %.0f%%  |  Windows alpha 0.6.0 — instrumentos + preparacion",
        mixer->playing.load()?L"REPRODUCIENDO":L"PAUSA / DETENIDO",
        static_cast<unsigned long long>(seconds/60),static_cast<unsigned long long>(seconds%60),mixer->peak.load()*100.0);
    if(!preparing.load())setText(status,buffer);
}
void resetGestures() {
    for(auto& row:instrumentOn)for(auto& on:row)on=false;
    for(int i=0;i<4;++i)fxOn[i]=false;
    mixer->releaseInstruments();mixer->setEffect(0,DUB_DELAY_HOLD,0);mixer->setEffect(0,DUB_DELAY_DOUBLE,0);mixer->setEffect(0,DUB_DELAY_STOP,0);
    const wchar_t* names[]={L"HOLD",L"x2",L"CRASH",L"TAPE STOP"};for(int i=0;i<4;++i)if(fxButtons[i])setText(fxButtons[i],names[i]);
    for(int t=4;t<6;++t)if(mutes[t])syncInstrumentUI(t);
}
bool ensureAudio() {
    if(!deviceReady && !openAudio())return false;
    if(!ma_device_is_started(&device) && ma_device_start(&device)!=MA_SUCCESS){closeAudio();error(L"No se pudo iniciar audio");return false;}
    return true;
}
void syncInstrumentUI(int t) {
    const bool inst=mixer->isInstrument(t);
    const wchar_t* labels[]={L"DELAY",L"REVERB",L"PHASER"};
    for(int b=0;b<3;++b){
        setText(sendLabels[t][b],inst?(b==0?L"PITCH":(b==1?(t==4?L"RATE":L"DECAY"):L"DELAY")):labels[b]);
        const float v=inst && b<2?instrumentValues[t-4][b]:mixer->strip[t].send[inst?0:b].load();SendMessageW(sends[t][b],TBM_SETPOS,TRUE,static_cast<int>(std::lround(v*127)));
    }
    if(inst){setText(names[t],t==4?L"DUB SIREN":L"SYNDRUM");
        setText(mutes[t],t==4?(instrumentOn[0][0]?L"HOLD ON":L"HOLD"):L"HIT");
        setText(solos[t],t==4?(instrumentOn[0][1]?L"RISE ON":L"RISE"):(instrumentOn[1][1]?L"ROLL ON":L"ROLL"));
        led(1+3*t,instrumentOn[t-4][0]);led(3+3*t,instrumentOn[t-4][1]);
    }else {setText(mutes[t],mixer->strip[t].solo?L"SOLO":(mixer->strip[t].mute?L"MUTED":L"MUTE"));setText(solos[t],mixer->strip[t].solo?L"SOLO ON":L"SOLO");}
}
void instrumentAction(int index,int gesture,bool on) {
    if(!mixer->isInstrument(index+4))return;
    if(on && !ensureAudio())return;
    instrumentOn[index][gesture]=on;
    int parameter=index==0?(gesture==0?DUB_SIREN_GATE:DUB_SIREN_RISE):(gesture==0?DUB_SYNDRUM_HIT:DUB_SYNDRUM_ROLLING);
    // A one-shot hit clears itself; Note Off must not erase an unconsumed hit.
    if(index!=1 || gesture!=0 || on)mixer->setInstrument(index,parameter,on?1.0f:0.0f);
    syncInstrumentUI(index+4);
}
void effectAction(int action,bool on) {
    if(on && !ensureAudio())return;
    fxOn[action]=on;
    if(action==0)mixer->setEffect(0,DUB_DELAY_HOLD,on?1:0);
    if(action==1)mixer->setEffect(0,DUB_DELAY_DOUBLE,on?1:0);
    if(action==2 && on){mixer->setEffect(1,DUB_SWITCH_SELECT,1);mixer->setEffect(1,DUB_SPRING_CRASH,1);fxOn[4]=true;setText(fxButtons[4],L"SPRING");fxOn[2]=false;}
    if(action==3)mixer->setEffect(0,DUB_DELAY_STOP,on?1:0);
    if(action==4){mixer->setEffect(1,DUB_SWITCH_SELECT,on?1:0);setText(fxButtons[4],on?L"SPRING":L"PLATE");}
    if(action==5){mixer->setEffect(2,DUB_SWITCH_SELECT,on?1:0);setText(fxButtons[5],on?L"FLANGER":L"PHASER");}
    const wchar_t* titles[]={L"HOLD",L"x2",L"CRASH",L"TAPE STOP"};
    if(action<4){auto label=std::wstring(titles[action])+(fxOn[action]?L" ON":L"");setText(fxButtons[action],label.c_str());}
}
void recordStemError(const std::wstring& details){
    lastStemError=details;
    try{auto folder=dubwin::configRoot();std::filesystem::create_directories(folder);
        int size=WideCharToMultiByte(CP_UTF8,0,details.c_str(),int(details.size()),nullptr,0,nullptr,nullptr);
        std::string utf8(size,'\0');WideCharToMultiByte(CP_UTF8,0,details.c_str(),int(details.size()),utf8.data(),size,nullptr,nullptr);
        std::ofstream out(folder/L"last-stem-error.txt",std::ios::binary|std::ios::trunc);out<<utf8;
    }catch(...){/* The on-screen diagnostic remains available if disk writing fails. */}
}
void showStemError(){
    if(lastStemError.empty()){try{std::ifstream in(dubwin::configRoot()/L"last-stem-error.txt",std::ios::binary);std::string data((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());if(!data.empty())lastStemError=wide(data.c_str());}catch(...){} }
    auto message=lastStemError.empty()?L"No hay errores de stems registrados en esta version.":lastStemError+L"\n\nPuedes copiar este mensaje con Ctrl+C.";
    theme::message(window,message.c_str(),L"Detalle del error de stems",MB_OK|MB_ICONINFORMATION);
}
void setProgress(const std::wstring& text){std::lock_guard<std::mutex> lock(progressMutex);progressText=text;}
// The queue owns paths only. A preparation worker never touches the live mixer.
std::filesystem::path settingsFile(){auto p=dubwin::configRoot();std::filesystem::create_directories(p);auto f=p/L"library.ini";if(!std::filesystem::exists(f)){std::ofstream out(f,std::ios::binary);out.put(char(0xff));out.put(char(0xfe));}return f;}
void saveLibrary(){
    if(uiTestMode)return;
    try{auto f=settingsFile();auto put=[&](const wchar_t* section,const std::wstring& key,const std::wstring& value){WritePrivateProfileStringW(section,key.c_str(),value.c_str(),f.c_str());};
        put(L"Preferences",L"Output",preferredOutput);put(L"MIDI",L"Layout",L"2");
        put(L"Preferences",L"Fast",fastMode?L"1":L"0");put(L"Preferences",L"GPU",std::to_wstring(gpuDevice));
        for(int i=0;i<3;++i)put(L"MIDI",std::to_wstring(i),std::to_wstring(transportNotes[i]));
        put(L"Session",L"File",sessionPath.wstring());put(L"Session",L"Dirty",sessionDirty?L"1":L"0");
        put(L"Songs",L"Count",std::to_wstring(songs.size()));
        for(size_t i=0;i<songs.size();++i){auto k=std::to_wstring(i);put(L"Paths",k,songs[i].source.wstring());put(L"Caches",k,songs[i].cache.wstring());put(L"Modes",k,songs[i].fast?L"1":L"0");put(L"Durations",k,std::to_wstring(songs[i].duration));}
    }catch(const std::exception& e){error(wide(e.what()).c_str());}
}
void loadLibrary(){
    if(uiTestMode)return;
    auto f=settingsFile();fastMode=GetPrivateProfileIntW(L"Preferences",L"Fast",1,f.c_str())!=0;
    gpuDevice=static_cast<int>(GetPrivateProfileIntW(L"Preferences",L"GPU",-1,f.c_str()));
    for(int i=0;i<3;++i)transportNotes[i]=static_cast<int>(GetPrivateProfileIntW(L"MIDI",std::to_wstring(i).c_str(),transportNotes[i],f.c_str()));
    if(GetPrivateProfileIntW(L"MIDI",L"Layout",0,f.c_str())<2){transportNotes[0]=25;transportNotes[1]=20;transportNotes[2]=26;}
    auto get=[&](const wchar_t* section,const std::wstring& key){wchar_t v[32768]{};GetPrivateProfileStringW(section,key.c_str(),L"",v,32768,f.c_str());return std::wstring(v);};
    preferredOutput=get(L"Preferences",L"Output");sessionPath=get(L"Session",L"File");sessionDirty=get(L"Session",L"Dirty")==L"1";
    int count=std::min(1000,int(GetPrivateProfileIntW(L"Songs",L"Count",0,f.c_str())));
    for(int i=0;i<count;++i){auto key=std::to_wstring(i);Song song;song.source=get(L"Paths",key);song.cache=get(L"Caches",key);song.fast=get(L"Modes",key)!=L"0";
        try{auto value=get(L"Durations",key);if(!value.empty())song.duration=std::stod(value);if(!std::isfinite(song.duration)||song.duration<-2||song.duration>2678400)song.duration=-1;}catch(...){song.duration=-1;}
        if(song.source.empty())continue;
        song.state=std::filesystem::exists(song.cache/L"complete.txt")?L"Lista":L"Pendiente (reintentar)";songs.push_back(std::move(song));}
}
std::wstring durationText(double seconds,bool total=false){
    if(seconds<0)return L"--:--";auto n=static_cast<unsigned long long>(std::llround(seconds));wchar_t text[64];
    if(total||n>=3600)swprintf_s(text,L"%llu:%02llu:%02llu",n/3600,(n/60)%60,n%60);else swprintf_s(text,L"%02llu:%02llu",n/60,n%60);return text;
}
void updateLibrarySummary(){
    double total=0;size_t unknown=0;for(auto& song:songs){if(song.duration>=0)total+=song.duration;else ++unknown;}
    auto text=std::to_wstring(songs.size())+L" pistas / Total "+durationText(total,true)+(unknown?L" + ?":L"");setText(libraryTotal,text.c_str());
    auto name=(sessionPath.empty()?L"Sin guardar":sessionPath.stem().wstring())+(sessionDirty?L" *":L"");setText(sessionLabel,name.c_str());
}
void refreshLibrary(){
    updateLibrarySummary();if(!songList)return;auto selected=SendMessageW(songList,LB_GETCURSEL,0,0);auto top=SendMessageW(songList,LB_GETTOPINDEX,0,0);
    SendMessageW(songList,WM_SETREDRAW,FALSE,0);SendMessageW(songList,LB_RESETCONTENT,0,0);
    for(const auto& song:songs){auto label=L"["+song.state+L"]  "+song.source.filename().wstring()+(song.fast?L"  (Rapido)":L"  (Calidad)");SendMessageW(songList,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
    SendMessageW(songList,LB_SETCURSEL,selected,0);SendMessageW(songList,LB_SETTOPINDEX,top,0);SendMessageW(songList,WM_SETREDRAW,TRUE,0);InvalidateRect(songList,nullptr,TRUE);if(scrollRail)InvalidateRect(scrollRail,nullptr,FALSE);
}
void pollWarm(){
    if(warming && warmDone.load(std::memory_order_acquire)){
        if(warmer.joinable())warmer.join();warming=false;
        if(waitingWarm){waitingWarm=false;loadError=warmError;preparedStems=std::move(warmResult.stems);preparedWaveform=std::move(warmResult.wave);loadDone.store(true,std::memory_order_release);}
        else if(warmError.empty())warmCache.put(std::move(warmResult));
        else warmFailed.insert(warmFolder);
        warmResult={};refreshLibrary();
    }
}
void pumpBackground(){
    if(metadataBusy && metadataDone.load(std::memory_order_acquire)){
        if(metadataThread.joinable())metadataThread.join();metadataBusy=false;
        for(auto& song:songs)if(song.source==metadataSource && song.cache==metadataCache)song.duration=metadataDuration;
        refreshLibrary();
    }
    if(!metadataBusy){for(auto& song:songs)if(song.duration==-1){
        metadataSource=song.source;metadataCache=song.cache;auto source=metadataSource,cache=metadataCache;
        metadataBusy=true;metadataDone=false;
        metadataThread=std::thread([source,cache]{SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
            try{auto stem=cache/L"drums.wav";metadataDuration=dubwin::audioDuration(!cache.empty()&&std::filesystem::exists(stem)?stem:source);}catch(...){metadataDuration=-2;}
            metadataDone.store(true,std::memory_order_release);});break;
    }}
    if(warming || loading || GetTickCount64()-selectionChanged<350)return;
    int selected=int(SendMessageW(songList,LB_GETCURSEL,0,0)),target=-1;
    if(selected>=0 && selected<int(songs.size()) && songs[selected].state==L"Lista" && songs[selected].cache!=loadedCache)target=selected;
    if(target<0){int after=-1;for(size_t i=0;i<songs.size();++i)if(songs[i].source==loadedSource && songs[i].fast==loadedFast){after=int(i);break;}
        for(int i=after+1;i<int(songs.size());++i)if(songs[i].state==L"Lista" && songs[i].cache!=loadedCache){target=i;break;}}
    if(target<0)return;auto& song=songs[target];
    if(song.cache.empty()||warmCache.contains(song.cache)||warmFailed.count(song.cache))return;
    warmCache.reserve(size_t((song.duration>=0?std::min(600.0,std::ceil(song.duration)+1):600.0)*48000*8*4)+256*1024);
    warmFolder=song.cache;warmError.clear();warmResult={};warmResult.key=warmFolder;warming=true;cancelWarm=false;warmDone=false;auto folder=warmFolder;
    warmer=std::thread([folder]{SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        try{warmResult.stems=dubwin::loadPrepared(folder,cancelWarm,&warmResult.wave);}catch(const std::exception& e){warmError=wide(e.what());}
        warmDone.store(true,std::memory_order_release);});refreshLibrary();
}
void stopBackground(){
    cancelWarm=true;if(warmer.joinable())warmer.join();warming=false;warmDone=false;warmResult={};waitingWarm=false;
    if(metadataThread.joinable())metadataThread.join();metadataBusy=false;metadataDone=false;
}
std::filesystem::path previewRoot(){return std::filesystem::temp_directory_path()/(L"DubStemMix-ui-"+std::to_wstring(GetCurrentProcessId()));}
bool saveSessionDialog(bool saveAs){
    auto path=sessionPath;
    if(uiTestMode)path=previewRoot()/L"biblioteca.dubset";
    else if(saveAs||path.empty()){
        wchar_t file[32768]{};auto initial=path.empty()?std::wstring(L"Mi biblioteca.dubset"):path.wstring();wcsncpy_s(file,initial.c_str(),_TRUNCATE);
        OPENFILENAMEW d{};d.lStructSize=sizeof d;d.hwndOwner=window;d.lpstrTitle=L"Guardar biblioteca";d.lpstrFilter=L"Biblioteca DubStemMix\0*.dubset\0\0";d.lpstrDefExt=L"dubset";d.lpstrFile=file;d.nMaxFile=32768;d.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
        if(!GetSaveFileNameW(&d))return false;path=file;
    }
    try{std::vector<dubwin::SessionEntry> entries;for(auto& song:songs)entries.push_back({song.source,song.cache,song.fast,song.duration});dubwin::writeSession(path,entries);
        sessionPath=path;sessionDirty=false;saveLibrary();updateLibrarySummary();return true;
    }catch(const std::exception& e){error(wide(e.what()).c_str());return false;}
}
bool confirmLibraryChange(){
    if(preparing||loading){error(L"Espera o cancela la preparacion/carga antes de cambiar de biblioteca.");return false;}
    if(!sessionDirty)return true;
    int answer=theme::message(window,L"Hay cambios en esta biblioteca. ¿Quieres guardarlos antes de continuar?\n\nSi: guardar. No: continuar sin guardar. Cancelar: conservar la biblioteca abierta.",L"Cambiar biblioteca",MB_YESNOCANCEL);
    if(answer==IDCANCEL)return false;return answer==IDYES?saveSessionDialog(false):true;
}
void clearLibraryPlayback(){
    stopBackground();stopAudio();mixer->reset();resetGestures();warmCache.clear();warmFailed.clear();
    for(int t=0;t<6;++t){std::vector<float>().swap(mixer->strip[t].samples);mixer->strip[t].mute=false;mixer->strip[t].solo=false;syncInstrumentUI(t);if(t<4)setText(names[t],L"Sin archivo");}
    loadedSource.clear();loadedCache.clear();waveform.clear();queuePaused=true;setText(GetDlgItem(window,idQueuePause),L"Seguir cola");
    setText(songLabel,L"TU SESION DUB");resetPickup();InvalidateRect(window,nullptr,FALSE);
}
void newSession(){
    if(!confirmLibraryChange())return;clearLibraryPlayback();songs.clear();sessionPath.clear();sessionDirty=false;saveLibrary();refreshLibrary();setProgress(L"Nueva biblioteca. Agrega canciones o abre una guardada.");
}
void openSessionDialog(){
    if(preparing||loading){error(L"Espera o cancela la preparacion/carga antes de abrir otra biblioteca.");return;}
    std::filesystem::path path;
    if(uiTestMode)path=previewRoot()/L"biblioteca.dubset";
    else {wchar_t file[32768]{};OPENFILENAMEW d{};d.lStructSize=sizeof d;d.hwndOwner=window;d.lpstrTitle=L"Abrir biblioteca";d.lpstrFilter=L"Biblioteca DubStemMix\0*.dubset\0\0";d.lpstrFile=file;d.nMaxFile=32768;d.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;if(!GetOpenFileNameW(&d))return;path=file;}
    try{auto entries=dubwin::readSession(path);std::vector<Song> restored;
        for(auto& entry:entries){Song song;song.source=entry.source;song.cache=entry.cache;song.fast=entry.fast;song.duration=entry.duration;
            song.state=!song.cache.empty()&&std::filesystem::exists(song.cache/L"complete.txt")?L"Lista":(std::filesystem::exists(song.source)?L"Pendiente (reintentar)":L"Archivo no encontrado");restored.push_back(std::move(song));}
        if(!confirmLibraryChange())return;clearLibraryPlayback();songs=std::move(restored);sessionPath=path;sessionDirty=false;saveLibrary();refreshLibrary();setProgress(L"Biblioteca abierta. Selecciona y carga una cancion.");
    }catch(const std::exception& e){error(wide(e.what()).c_str());}
}

void runNext(){
    if(preparing || queuePaused)return;
    for(size_t i=0;i<songs.size();++i)if(songs[i].state==L"En cola"){
        activeJob=int(i);songs[i].state=L"Analizando";refreshLibrary();
        const auto source=songs[i].source;const dubwin::SeparationOptions options{songs[i].fast,gpuDevice};
        jobBackend=L"CPU";if(gpuDevice>=0){jobBackend=L"GPU DirectML #"+std::to_wstring(gpuDevice);for(auto& adapter:adapters)if(adapter.first==gpuDevice)jobBackend+=L" / "+adapter.second;}
        preparing=true;cancelPreparation=false;preparationDone=false;preparationError.clear();jobCache.clear();
        preparation=std::thread([source,options]{
            SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
            try{jobCache=dubwin::prepareSong(source,cancelPreparation,setProgress,options);}
            catch(const std::exception& e){preparationError=wide(e.what());}
            preparationDone.store(true,std::memory_order_release);
        });return;
    }
}
void startPreparation(){
    wchar_t files[65536]{};OPENFILENAMEW d{};d.lStructSize=sizeof d;d.hwndOwner=window;
    d.lpstrFilter=L"Canciones (WAV, FLAC, MP3)\0*.wav;*.flac;*.mp3\0\0";d.lpstrFile=files;d.nMaxFile=65536;
    d.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER|OFN_ALLOWMULTISELECT;
    if(!GetOpenFileNameW(&d))return;
    if(theme::message(window,L"Agregar a la cola y separar localmente, sin detener la musica.\n\nLa primera vez Rapido descarga 166 MB; Calidad, hasta 663 MB. Despues funciona sin Internet. Maximo 10 minutos por cancion.\n\nAl terminar, selecciona la cancion en la lista y pulsa Cargar seleccionada. No se cambia automaticamente la cancion en vivo. ¿Continuar?",L"Preparar stems",MB_YESNO|MB_ICONINFORMATION)!=IDYES)return;
    std::filesystem::path first(files);const wchar_t* next=files+wcslen(files)+1;
    auto add=[&](const std::filesystem::path& path){if(songs.size()>=1000)return;for(const auto& item:songs)if(item.source==path && item.fast==fastMode)return;Song item;item.source=path;item.fast=fastMode;songs.push_back(std::move(item));};
    if(!*next)add(first);else for(;*next;next+=wcslen(next)+1)add(first/next);
    sessionDirty=true;saveLibrary();showLibrary();refreshLibrary();runNext();
}
void requestLoadIndex(int n,bool next=false){
    if(n<0 || size_t(n)>=songs.size())return;
    if(loading){error(L"Ya hay una cancion cargandose.");return;}
    if(songs[n].state!=L"Lista" || songs[n].cache.empty()){error(L"Espera a que la cancion este Lista.");return;}
    if(mixer->playing && !next){error(L"Pulsa PAUSA antes de cargar otra cancion, o utiliza SIGUIENTE.");return;}
    if(songs[n].cache==loadedCache && songs[n].source==loadedSource && songs[n].fast==loadedFast){
        bool resume=next&&mixer->playing.load();stopAudio();mixer->reset();resetGestures();for(int t=0;t<6;++t){mixer->strip[t].mute=false;mixer->strip[t].solo=false;syncButtons(t);}++warmHits;
        auto r=waveRect();InvalidateRect(window,&r,FALSE);if(resume)SendMessageW(window,WM_COMMAND,idPlay,0);return;
    }
    auto folder=songs[n].cache;loadCache=folder;loadSource=songs[n].source;loadFast=songs[n].fast;loadTitle=loadSource.stem().wstring();
    resumeAfterLoad=next && mixer->playing.load();loadError.clear();loading=true;loadDone=false;cancelLoad=false;
    SendMessageW(songList,LB_SETCURSEL,n,0);setText(libraryStatus,L"Cargando audio preparado...");
    dubwin::WarmAudio ready;if(warmCache.take(folder,ready)){
        preparedStems=std::move(ready.stems);preparedWaveform=std::move(ready.wave);++warmHits;loadDone=true;pollPreparation();return;
    }
    if(warming && warmFolder==folder){waitingWarm=true;return;}
    loader=std::thread([folder]{try{preparedStems=dubwin::loadPrepared(folder,cancelLoad,&preparedWaveform);}catch(const std::exception& e){loadError=wide(e.what());}loadDone.store(true,std::memory_order_release);});
}
void requestLoad(){requestLoadIndex(int(SendMessageW(songList,LB_GETCURSEL,0,0)));}
void nextSong(){
    if(loading)return;
    int after=-1;for(size_t i=0;i<songs.size();++i)if(songs[i].source==loadedSource && songs[i].fast==loadedFast){after=int(i);break;}
    if(after<0)after=int(SendMessageW(songList,LB_GETCURSEL,0,0))-1;
    for(int i=std::max(0,after+1);i<int(songs.size());++i)if(songs[i].state==L"Lista" && !songs[i].cache.empty()){requestLoadIndex(i,true);return;}
    error(L"No hay otra cancion preparada despues de la actual. Agrega canciones o espera a que termine el analisis.");
}
void pollPreparation(){
    pollWarm();
    if(loading && loadDone.load(std::memory_order_acquire)){
        if(loader.joinable())loader.join();loading=false;
        if(!loadError.empty()){preparedStems={};preparedWaveform.clear();recordStemError(L"Carga: "+loadTitle+L"\nError: "+loadError);error(loadError.c_str());}
        else {stopAudio();mixer->reset();resetGestures();
            if(!loadedCache.empty()){dubwin::WarmAudio previous;previous.key=loadedCache;for(int t=0;t<4;++t)previous.stems[t]=std::move(mixer->strip[t].samples);previous.wave=std::move(waveform);warmCache.put(std::move(previous));}
            const wchar_t* titles[]={L"BATERIA",L"BAJO",L"OTROS",L"VOZ"};
            for(int t=0;t<6;++t){if(t<4)mixer->strip[t].samples=std::move(preparedStems[t]);else mixer->strip[t].samples.clear();
                mixer->strip[t].mute=false;mixer->strip[t].solo=false;syncInstrumentUI(t);if(t<4)setText(names[t],titles[t]);}
            loadedSource=loadSource;loadedCache=loadCache;loadedFast=loadFast;
            for(auto& song:songs)if(song.source==loadedSource && song.fast==loadedFast)song.duration=double(mixer->totalFrames())/dubwin::rate;
            refreshLibrary();waveform=std::move(preparedWaveform);
            setText(songLabel,loadTitle.c_str());InvalidateRect(window,nullptr,FALSE);resetPickup();setProgress(L"Cancion cargada. Pulsa PLAY.");
            if(resumeAfterLoad)SendMessageW(window,WM_COMMAND,idPlay,0); }
    }
    if(preparing && preparationDone.load(std::memory_order_acquire)){
        if(preparation.joinable())preparation.join();preparing=false;
        auto& song=songs[activeJob];song.state=preparationError.empty()?L"Lista":(cancelPreparation?L"Cancelada":L"Error");
        if(preparationError.empty()){song.cache=jobCache;song.duration=-1;warmCache.erase(jobCache);warmFailed.erase(jobCache);sessionDirty=true;}
        else if(!cancelPreparation){
            SYSTEMTIME t{};GetLocalTime(&t);wchar_t stamp[64];swprintf_s(stamp,L"%04u-%02u-%02u %02u:%02u:%02u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
            recordStemError(std::wstring(L"DubStemMix 0.6.0 / ")+stamp+L"\nArchivo: "+song.source.wstring()+L"\nModo: "+(song.fast?L"Rapido":L"Calidad")+L"\nMotor: "+jobBackend+L"\n\nError: "+preparationError);
            queuePaused=true;setText(GetDlgItem(window,idQueuePause),L"Seguir cola");
        }
        setProgress(preparationError.empty()?L"Stems listos. Selecciona y carga desde la lista.":L"Analisis detenido. Abre Ayuda > Ultimo error de stems para ver el detalle.");
        activeJob=-1;saveLibrary();refreshLibrary();
    }
    runNext();pumpBackground();
}
void showLibrary(){SetFocus(songList);}
void buildMenus(){
    auto bar=CreateMenu(),file=CreatePopupMenu(),prefs=CreatePopupMenu(),transport=CreatePopupMenu(),help=CreatePopupMenu();
    AppendMenuW(file,MF_STRING,idSessionNew,L"Nueva biblioteca");AppendMenuW(file,MF_STRING,idSessionOpen,L"Abrir biblioteca...");
    AppendMenuW(file,MF_STRING,idSessionSave,L"Guardar biblioteca");AppendMenuW(file,MF_STRING,idSessionSaveAs,L"Guardar biblioteca como...");AppendMenuW(file,MF_SEPARATOR,0,nullptr);
    AppendMenuW(file,MF_STRING,idSong,L"Agregar canciones a la cola...");AppendMenuW(file,MF_STRING,idLibrary,L"Lista de canciones...");AppendMenuW(file,MF_STRING,idDataFolder,L"Carpeta de datos...");AppendMenuW(file,MF_STRING,idExit,L"Salir");
    AppendMenuW(prefs,MF_STRING|(fastMode?MF_CHECKED:0),idFast,L"Stems: Rapido (un modelo)");AppendMenuW(prefs,MF_STRING|(!fastMode?MF_CHECKED:0),idQuality,L"Stems: Calidad (cuatro modelos)");
    AppendMenuW(prefs,MF_STRING|(gpuDevice<0?MF_CHECKED:0),idCPU,L"Analisis: CPU");
    for(size_t i=0;i<adapters.size();++i){auto label=L"GPU experimental: "+adapters[i].second;AppendMenuW(prefs,MF_STRING|(gpuDevice==adapters[i].first?MF_CHECKED:0),idGPU+i,label.c_str());}
    auto outputs=CreatePopupMenu();for(size_t i=0;i<deviceNames.size();++i)AppendMenuW(outputs,MF_STRING|(int(i)==selectedOutput?MF_CHECKED:0),idOutputBase+i,deviceNames[i].c_str());
    if(deviceNames.empty())AppendMenuW(outputs,MF_STRING|MF_GRAYED,0,L"Sin dispositivos");
    AppendMenuW(prefs,MF_POPUP,reinterpret_cast<UINT_PTR>(outputs),L"Salida de audio");
    AppendMenuW(prefs,MF_STRING,idAudio,L"Actualizar salidas de audio");AppendMenuW(prefs,MF_STRING,idMidi,L"Reconectar MIDImix");
    AppendMenuW(transport,MF_STRING,idTogglePlay,L"Play / Pausa\tEspacio");AppendMenuW(transport,MF_STRING,idNext,L"Siguiente cancion preparada");AppendMenuW(transport,MF_STRING,idPause,L"Pausa");AppendMenuW(transport,MF_STRING,idStop,L"Reiniciar");
    AppendMenuW(transport,MF_STRING,idLearn,L"Aprender boton Play...");AppendMenuW(transport,MF_STRING,idLearn+1,L"Aprender boton Pausa...");AppendMenuW(transport,MF_STRING,idLearn+2,L"Aprender boton Reiniciar...");
    AppendMenuW(help,MF_STRING,idLastError,L"Ultimo error de stems...");
    AppendMenuW(help,MF_STRING,idAbout,L"Uso y limitaciones");AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(file),L"Archivo");AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(prefs),L"Preferencias");AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(transport),L"Transporte");AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(help),L"Ayuda");
    auto old=GetMenu(window);SetMenu(window,bar);if(old)DestroyMenu(old);DrawMenuBar(window);
}
void relayout(){
    RECT r;GetClientRect(window,&r);if(!r.right||!r.bottom)return;
    layoutX=float(r.right)/1280;layoutY=float(r.bottom)/800;
    auto make=[&](int size,int weight){return CreateFontW(-std::max(9,int(std::lround(size*std::min(layoutX,layoutY)))),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,size==11?L"JetBrains Mono":L"Archivo");};
    auto old=font,oldHeading=headingFont,oldSmall=smallFont;font=make(13,FW_SEMIBOLD);headingFont=make(20,FW_HEAVY);smallFont=make(10,FW_NORMAL);
    SendMessageW(window,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
    auto batch=BeginDeferWindowPos(int(placements.size()));
    for(auto& c:placements){batch=DeferWindowPos(batch,c.h,nullptr,sx(c.x),sy(c.y),sx(c.w),sy(c.height),SWP_NOZORDER|SWP_NOACTIVATE);SendMessageW(c.h,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);}
    if(batch)EndDeferWindowPos(batch);
    SendMessageW(songLabel,WM_SETFONT,reinterpret_cast<WPARAM>(headingFont),FALSE);
    SendMessageW(status,WM_SETFONT,reinterpret_cast<WPARAM>(smallFont),FALSE);SendMessageW(libraryStatus,WM_SETFONT,reinterpret_cast<WPARAM>(smallFont),FALSE);
    SendMessageW(audioLabel,WM_SETFONT,reinterpret_cast<WPARAM>(smallFont),FALSE);SendMessageW(midiLabel,WM_SETFONT,reinterpret_cast<WPARAM>(smallFont),FALSE);
    for(auto& row:sends)for(auto h:row)SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(smallFont),FALSE);
    for(auto& row:sendLabels)for(auto h:row)SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(smallFont),FALSE);
    for(int e=0;e<6;++e){SendMessageW(fxDials[e],WM_SETFONT,reinterpret_cast<WPARAM>(smallFont),FALSE);SendMessageW(GetDlgItem(window,940+e),WM_SETFONT,reinterpret_cast<WPARAM>(smallFont),FALSE);}
    for(auto h:{libraryTotal,sessionLabel})SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(smallFont),FALSE);
    SendMessageW(songList,LB_SETITEMHEIGHT,0,sy(55));
    if(old)DeleteObject(old);if(oldHeading)DeleteObject(oldHeading);if(oldSmall)DeleteObject(oldSmall);
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
int visibleRows(){int height=int(SendMessageW(songList,LB_GETITEMHEIGHT,0,0));RECT r{};GetClientRect(songList,&r);return std::max(1,int(r.bottom)/std::max(1,height));}
void scrollLibrary(int top){int maximum=std::max(0,int(songs.size())-visibleRows());SendMessageW(songList,LB_SETTOPINDEX,std::clamp(top,0,maximum),0);if(scrollRail)InvalidateRect(scrollRail,nullptr,FALSE);}
void moveLibrarySelection(int delta){if(songs.empty())return;int current=int(SendMessageW(songList,LB_GETCURSEL,0,0));int next=current<0?0:std::clamp(current+delta,0,int(songs.size())-1);
    SendMessageW(songList,LB_SETCURSEL,next,0);selectionChanged=GetTickCount64();SetFocus(songList);if(scrollRail)InvalidateRect(scrollRail,nullptr,FALSE);
}
LRESULT CALLBACK listProc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR){
    if(m==WM_MOUSEWHEEL){int delta=GET_WHEEL_DELTA_WPARAM(w);scrollLibrary(int(SendMessageW(h,LB_GETTOPINDEX,0,0))-(delta>0?3:-3));return 0;}
    auto result=DefSubclassProc(h,m,w,l);
    if(m==WM_KEYDOWN||m==WM_LBUTTONUP||m==WM_VSCROLL){if(scrollRail)InvalidateRect(scrollRail,nullptr,FALSE);}
    if(m==WM_NCDESTROY)RemoveWindowSubclass(h,listProc,id);return result;
}
LRESULT CALLBACK scrollProc(HWND h,UINT m,WPARAM w,LPARAM l){
    auto scrollAt=[&](int y){RECT r;GetClientRect(h,&r);int count=int(songs.size()),visible=visibleRows(),maximum=std::max(0,count-visible);
        int thumb=std::min(int(r.bottom),std::max(18,int(r.bottom)*visible/std::max(1,count)));
        scrollLibrary(maximum?int(double(std::clamp(y-thumb/2,0,std::max(1,int(r.bottom)-thumb)))*maximum/std::max(1,int(r.bottom)-thumb)):0);};
    switch(m){
    case WM_ERASEBKGND:return 1;
    case WM_LBUTTONDOWN:SetCapture(h);scrollAt(short(HIWORD(l)));return 0;
    case WM_MOUSEMOVE:if(GetCapture()==h)scrollAt(short(HIWORD(l)));return 0;
    case WM_LBUTTONUP:ReleaseCapture();return 0;
    case WM_MOUSEWHEEL:scrollLibrary(int(SendMessageW(songList,LB_GETTOPINDEX,0,0))-(GET_WHEEL_DELTA_WPARAM(w)>0?3:-3));return 0;
    case WM_PAINT:{PAINTSTRUCT ps;auto dc=BeginPaint(h,&ps);RECT r;GetClientRect(h,&r);theme::PaintBuffer paint(dc,r);FillRect(paint.dc,&r,theme::bg());
        int count=int(songs.size()),visible=visibleRows(),maximum=std::max(0,count-visible),height=int(r.bottom);
        int thumb=std::min(height,std::max(18,height*visible/std::max(1,count))),top=int(SendMessageW(songList,LB_GETTOPINDEX,0,0));
        int y=maximum?top*(height-thumb)/maximum:0;RECT track{r.right/2-1,0,r.right/2+1,r.bottom};FillRect(paint.dc,&track,theme::surface());
        RECT grip{3,y,r.right-3,y+thumb};auto brush=CreateSolidBrush(maximum?theme::muted:RGB(55,49,40));FillRect(paint.dc,&grip,brush);DeleteObject(brush);
        paint.present();EndPaint(h,&ps);return 0;}
    }return DefWindowProcW(h,m,w,l);
}
int meterSegments(float peak){if(!(peak>0.001f))return 0;return std::clamp(int(std::ceil((20*std::log10(peak)+60)*24/60)),0,24);}
RECT channelMeterRect(int channel){int x=240+channel*113+94;return RECT{sx(x),sy(566),sx(x+6),sy(710)};}
void drawChannelMeters(HDC dc){for(int t=0;t<8;++t){float peak=t<6?mixer->channelPeaks[t].load():mixer->returnPeaks[t-6].load();int lit=meterSegments(peak);auto r=channelMeterRect(t);
    for(int segment=0;segment<24;++segment){int bottom=r.bottom-(r.bottom-r.top)*segment/24,top=r.bottom-(r.bottom-r.top)*(segment+1)/24;RECT bar{r.left,top+1,r.right,bottom};
        auto brush=CreateSolidBrush(segment<lit?(segment>=23?theme::red:(segment>=21?theme::gold:theme::green)):RGB(48,43,35));FillRect(dc,&bar,brush);DeleteObject(brush);}}
}

void buildUI(){
    font=CreateFontW(-13,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    control(L"STATIC",L"CONSOLA DUB / 0.6.0",0,16,54,200,18);
    control(L"BUTTON",L"+  AGREGAR CANCION",WS_TABSTOP,16,94,194,36,idSong);
    control(L"BUTTON",L"Pausar cola",WS_TABSTOP,16,138,94,28,idQueuePause);control(L"BUTTON",L"Cancelar",WS_TABSTOP,116,138,94,28,idCancel);
    modeLabel=control(L"STATIC",L"",SS_ENDELLIPSIS,16,180,194,20);
    control(L"STATIC",L"BIBLIOTECA",0,16,216,194,20);
    songList=control(L"LISTBOX",L"",LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_TABSTOP,12,260,184,250,900);
    SetWindowSubclass(songList,listProc,1,0);
    sessionLabel=control(L"STATIC",L"Sin guardar",SS_ENDELLIPSIS,16,238,180,16,904);
    libraryTotal=control(L"STATIC",L"0 pistas / Total 0:00:00",SS_ENDELLIPSIS,16,516,194,20,903);
    control(L"BUTTON",L"▲",WS_TABSTOP,200,260,16,22,idListUp);control(L"BUTTON",L"▼",WS_TABSTOP,200,488,16,22,idListDown);
    scrollRail=control(L"DubScroll",L"",0,200,284,16,200,idListRail);
    control(L"BUTTON",L"CARGAR CANCION",WS_TABSTOP,16,548,194,34,idLoadReady);
    control(L"BUTTON",L"Reintentar",WS_TABSTOP,16,592,94,28,idRetrySong);control(L"BUTTON",L"Quitar",WS_TABSTOP,116,592,94,28,idRemoveSong);
    libraryStatus=control(L"STATIC",L"Agrega canciones para preparar tus stems.",0,16,636,194,64,901);
    audioLabel=control(L"STATIC",L"OUT: sin dispositivo",SS_ENDELLIPSIS,16,730,194,16);
    midiLabel=control(L"STATIC",L"MIDI: desconectado",SS_ENDELLIPSIS,16,752,194,16);
    control(L"BUTTON",L"PLAY",WS_TABSTOP,240,16,66,36,idPlay);control(L"BUTTON",L"PAUSA",WS_TABSTOP,312,16,66,36,idPause);control(L"BUTTON",L"REINICIAR",WS_TABSTOP,384,16,86,36,idStop);
    control(L"BUTTON",L"SIGUIENTE",WS_TABSTOP,478,16,94,36,idNext);
    songLabel=control(L"STATIC",L"TU SESION DUB",SS_ENDELLIPSIS,590,12,540,28);
    clockLabel=control(L"STATIC",L"00:00 / 00:00",0,590,42,450,18);
    control(L"BUTTON",L"PANIC",WS_TABSTOP,1150,16,106,36,idPanic);
    const wchar_t* effects[]={L"TIEMPO",L"FEEDBK",L"DECAY",L"DAMP",L"RATE",L"DEPTH"};
    for(int e=0;e<6;++e){int x=240+(e/2)*340+192+(e%2)*68;fxDials[e]=dial(x,122,46,52,idEffectDial+e,fxValues[e]);control(L"STATIC",effects[e],SS_CENTER,x-8,176,62,15,940+e);}
    const wchar_t* labels[]={L"BATERIA",L"BAJO",L"OTROS",L"VOZ",L"SIREN",L"SYNDRUM"};
    for(int t=0;t<6;++t){int x=240+t*113;
        for(int b=0;b<3;++b){sends[t][b]=dial(x+19,216+b*78,74,62,idSend+t*3+b,0);sendLabels[t][b]=control(L"STATIC",L"",SS_CENTER,x+8,279+b*78,96,15);}
        control(L"STATIC",labels[t],SS_CENTER,x+5,460,102,22,920+t);
        mutes[t]=control(L"BUTTON",L"MUTE",WS_TABSTOP,x+8,490,96,28,idMute+t);solos[t]=control(L"BUTTON",L"SOLO",WS_TABSTOP,x+8,524,96,28,idSolo+t);
        levels[t]=dial(x+23,566,66,144,idLevel+t,0.7f,true);
        names[t]=control(L"STATIC",L"Sin archivo",SS_ENDELLIPSIS|SS_CENTER,x+6,716,100,18);
        control(L"BUTTON",L"Cargar",WS_TABSTOP,x+6,740,49,26,idLoad+t);clearButtons[t]=control(L"BUTTON",L"Vaciar",WS_TABSTOP,x+59,740,49,26,idClear+t);syncInstrumentUI(t);
    }
    control(L"STATIC",L"DELAY",SS_CENTER,922,460,100,22,926);control(L"STATIC",L"SPACE",SS_CENTER,1035,460,100,22,927);
    const wchar_t* fxNames[]={L"HOLD",L"x2",L"CRASH",L"TAPE STOP",L"PLATE",L"PHASER"};
    const int fxX[]={926,926,1039,1039,926,1039},fxY[]={490,524,490,524,242,242};
    for(int i=0;i<6;++i)fxButtons[i]=control(L"BUTTON",fxNames[i],WS_TABSTOP,fxX[i],fxY[i],96,28,idFX+i);
    control(L"STATIC",L"REVERB",SS_CENTER,926,218,96,18);control(L"STATIC",L"MOD FX",SS_CENTER,1039,218,96,18);
    control(L"STATIC",L"PHASER RETURN",SS_CENTER,926,308,209,18);returns[2]=dial(997,337,64,64,idReturn+2,0.5f);
    returns[0]=dial(940,566,66,144,idReturn,0.5f,true);returns[1]=dial(1053,566,66,144,idReturn+1,0.5f,true);
    control(L"STATIC",L"DLY RETURN",SS_CENTER,922,724,104,18);control(L"STATIC",L"REV RETURN",SS_CENTER,1035,724,104,18);
    control(L"STATIC",L"MASTER",SS_CENTER,1148,250,106,24,928);control(L"STATIC",L"SALIDA",SS_CENTER,1148,222,106,18);
    control(L"STATIC",L"LIMITER ON",SS_CENTER,1148,490,106,18);
    control(L"BUTTON",L"SOLO",WS_TABSTOP,1148,524,106,28,idClearSolo);
    masterSlider=dial(1159,566,66,144,idMaster,0.5f,true);
    status=control(L"STATIC",L"",SS_ENDELLIPSIS,240,782,1014,18);
    refreshLibrary();enumerateAudio();connectMidi();
    for(int e=0;e<6;++e)SendMessageW(window,WM_HSCROLL,0,reinterpret_cast<LPARAM>(fxDials[e]));
    for(int t=4;t<6;++t)for(int b=0;b<2;++b)SendMessageW(window,WM_HSCROLL,0,reinterpret_cast<LPARAM>(sends[t][b]));
    uiReady=true;relayout();SetTimer(window,1,100,nullptr);
}
void paintConsole(HDC dc){
    RECT r;GetClientRect(window,&r);FillRect(dc,&r,theme::bg());
    auto rect=[](int x,int y,int w,int h){return RECT{sx(x),sy(y),sx(x+w),sy(y+h)};};
    auto label=[&](const wchar_t* text,RECT r,COLORREF color,HFONT f){auto old=SelectObject(dc,f);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,text,-1,&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);SelectObject(dc,old);};
    label(L"DUB",rect(16,16,52,28),theme::gold,headingFont);label(L"STEM",rect(65,16,70,28),theme::green,headingFont);label(L"MIX",rect(133,16,65,28),theme::red,headingFont);
    auto pen=CreatePen(PS_SOLID,1,RGB(47,42,35));auto old=SelectObject(dc,pen);MoveToEx(dc,sx(226),0,nullptr);LineTo(dc,sx(226),r.bottom);SelectObject(dc,old);DeleteObject(pen);
    for(int i=0;i<9;++i){auto box=rect(240+i*113,208,108,565);theme::card(dc,box,theme::panel,i==6?RGB(100,79,34):(i==7?RGB(67,80,46):RGB(48,42,35)));}
    const wchar_t* titles[]={L"DELAY",L"REVERB",L"PHASER"};const wchar_t* subs[]={L"Dub Delay",L"Plate / Spring",L"Phaser / Flanger"};COLORREF colors[]={theme::gold,theme::green,theme::red};
    for(int i=0;i<3;++i){int x=240+i*340;theme::card(dc,rect(x,118,332,80),theme::panel,colors[i]);label(titles[i],rect(x+16,129,180,24),colors[i],font);label(subs[i],rect(x+16,157,180,23),theme::muted,smallFont);}
    auto wave=waveRect();theme::card(dc,wave,theme::panel,RGB(48,42,35));
    auto inner=waveInner();auto frames=mixer->totalFrames();
    if(frames && !waveform.empty()){
        int width=std::max(1,int(inner.right-inner.left)),mid=(inner.top+inner.bottom)/2,half=std::max(1,int(inner.bottom-inner.top)/2-1);
        int playX=inner.left+int(std::min(1.0,double(mixer->position.load())/double(std::max<size_t>(1,frames-1)))*(width-1));
        auto before=CreatePen(PS_SOLID,1,theme::text),after=CreatePen(PS_SOLID,1,theme::muted);old=SelectObject(dc,after);
        for(int x=0;x<width;++x){size_t first=size_t(x)*waveform.size()/width,last=std::min(waveform.size(),std::max(first+1,size_t(x+1)*waveform.size()/width));float p=0;
            for(size_t j=first;j<last;++j)p=std::max(p,waveform[j]);int a=std::max(1,int(p*half));SelectObject(dc,inner.left+x<=playX?before:after);MoveToEx(dc,inner.left+x,mid-a,nullptr);LineTo(dc,inner.left+x,mid+a+1);}
        SelectObject(dc,old);DeleteObject(before);DeleteObject(after);
        pen=CreatePen(PS_SOLID,2,theme::gold);old=SelectObject(dc,pen);MoveToEx(dc,playX,inner.top,nullptr);LineTo(dc,playX,inner.bottom);SelectObject(dc,old);DeleteObject(pen);
    }else label(L"Carga una cancion. Clic o arrastra en la onda para navegar.",rect(258,72,950,30),theme::muted,smallFont);
    drawChannelMeters(dc);
    float peak=std::min(1.0f,mixer->peak.load());for(int i=0;i<24;++i){auto bar=rect(1232,700-i*7,7,4);auto b=CreateSolidBrush(i<meterSegments(peak)?(i>=23?theme::red:(i>=21?theme::gold:theme::green)):RGB(48,43,35));FillRect(dc,&bar,b);DeleteObject(b);}
    if(songs.empty())label(L"Tu lista esta vacia",rect(24,275,166,25),theme::muted,smallFont);
}
void drawSong(DRAWITEMSTRUCT* item){if(item->itemID==UINT(-1) || item->itemID>=songs.size())return;
    bool selected=(item->itemState&ODS_SELECTED)!=0;auto r=item->rcItem;theme::card(item->hDC,r,selected?theme::text:theme::background,theme::background);
    auto& song=songs[item->itemID];auto name=std::to_wstring(item->itemID+1)+L". "+song.source.stem().wstring();auto old=SelectObject(item->hDC,font);SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,selected?theme::background:theme::text);
    r.left+=sx(10);r.right-=sx(8);r.top+=sy(5);r.bottom=r.top+sy(24);DrawTextW(item->hDC,name.c_str(),-1,&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
    SelectObject(item->hDC,smallFont);SetTextColor(item->hDC,selected?theme::background:(song.state==L"Lista"?theme::green:theme::gold));r.top+=sy(24);r.bottom+=sy(20);auto state=song.state;if(song.source==loadedSource && song.fast==loadedFast)state=L"Cargada";else if(warmCache.contains(song.cache))state=L"En RAM";else if(warming && song.cache==warmFolder)state=L"Precargando";
    auto detail=state+L" / "+durationText(song.duration);DrawTextW(item->hDC,detail.c_str(),-1,&r,DT_SINGLELINE|DT_END_ELLIPSIS);SelectObject(item->hDC,old);
}
LRESULT CALLBACK windowProc(HWND h,UINT message,WPARAM wp,LPARAM lp) {
    switch(message){
    case WM_CREATE:{window=h;loadLibrary();adapters=dubwin::gpuAdapters();buildMenus();RECT r;GetClientRect(h,&r);layoutX=float(r.right)/1280;layoutY=float(r.bottom)/800;buildUI();return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_LBUTTONDOWN:{POINT point{short(LOWORD(lp)),short(HIWORD(lp))};auto r=waveRect();if(PtInRect(&r,point)&&mixer->totalFrames()){waveDragging=true;SetCapture(h);SetFocus(h);seekWave(point.x);}return 0;}
    case WM_MOUSEMOVE:if(waveDragging)seekWave(short(LOWORD(lp)));return 0;
    case WM_LBUTTONUP:if(waveDragging){seekWave(short(LOWORD(lp)));waveDragging=false;ReleaseCapture();}return 0;
    case WM_CAPTURECHANGED:waveDragging=false;return 0;
    case WM_SETCURSOR:{POINT p;GetCursorPos(&p);ScreenToClient(h,&p);auto r=waveRect();if(PtInRect(&r,p)){SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}break;}
    case WM_SIZE:if(uiReady && wp!=SIZE_MINIMIZED)relayout();return 0;
    case WM_GETMINMAXINFO:{auto info=reinterpret_cast<MINMAXINFO*>(lp);info->ptMinTrackSize={1000,640};info->ptMaxTrackSize={8192,8192};return 0;}
    case WM_PAINT:{++paintCount;PAINTSTRUCT ps;auto dc=BeginPaint(h,&ps);RECT r;GetClientRect(h,&r);theme::PaintBuffer paint(dc,r);paintConsole(paint.dc);paint.present();EndPaint(h,&ps);return 0;}
    case WM_MEASUREITEM:{auto item=reinterpret_cast<MEASUREITEMSTRUCT*>(lp);if(item->CtlID==900){item->itemHeight=sy(55);return TRUE;}break;}
    case WM_DRAWITEM:{auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item->CtlID==900)drawSong(item);else theme::button(item);return TRUE;}
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORLISTBOX:{auto dc=reinterpret_cast<HDC>(wp);int id=GetDlgCtrlID(reinterpret_cast<HWND>(lp));SetTextColor(dc,(id==924||id==925)?theme::red:(id==926?theme::gold:(id==927?theme::green:theme::text)));SetBkMode(dc,TRANSPARENT);
        RECT child;GetWindowRect(reinterpret_cast<HWND>(lp),&child);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&child),2);
        bool panel=child.left>=sx(240)&&child.top>=sy(208)&&child.top<sy(773);SetBkColor(dc,panel?theme::panel:theme::background);return reinterpret_cast<LRESULT>(panel?theme::surface():theme::bg());}
    case midiMessage:handleMidi(static_cast<DWORD>(wp));return 0;
    case WM_APP+21:return LRESULT(mixer->position.load());
    case WM_APP+22:return LRESULT(mixer->totalFrames());
    case WM_APP+23:{int mask=0;for(int t=0;t<4;++t)if(mixer->strip[t].solo)mask|=1<<t;return mask;}
    case WM_APP+24:for(size_t i=0;i<songs.size();++i)if(songs[i].source==loadedSource && songs[i].fast==loadedFast)return LRESULT(i+1);return 0;
    case WM_APP+25:return mixer->playing.load();
    case WM_APP+26:return selectedOutput;
    case WM_APP+27:return warmHits;
    case WM_APP+28:{unsigned mask=0;for(size_t i=0;i<songs.size()&&i<31;++i)if(warmCache.contains(songs[i].cache))mask|=1u<<i;return mask;}
    case WM_APP+29:{double total=0;for(auto& song:songs){if(song.duration<0)return -1;total+=song.duration;}return LRESULT(std::llround(total*1000));}
    case WM_APP+30:return wp<6?LRESULT(mixer->channelPeaks[wp].load()*1000000):0;
    case WM_APP+31:return LRESULT(warmCache.bytes());
    case WM_APP+20:return paintCount; // Read-only idle repaint regression probe.
    case WM_TIMER:updateStatus();return 0;
    case WM_HSCROLL:{
        HWND s=reinterpret_cast<HWND>(lp);if(!s)break;int id=GetDlgCtrlID(s);float value=static_cast<float>(SendMessageW(s,TBM_GETPOS,0,0))/127;
        if(id>=idLevel && id<idLevel+6){int t=id-idLevel;mixer->strip[t].level=value;levelPickup[t].reset();}
        else if(id>=idSend && id<idSend+18){int t=(id-idSend)/3,b=(id-idSend)%3;if(mixer->isInstrument(t) && b<2){instrumentValues[t-4][b]=value;instrumentPickup[t-4][b].reset();
            float real=t==4?(b==0?150.0f*std::pow(10.0f,value):0.3f*std::pow(40.0f,value)):(b==0?80.0f*std::pow(15.0f,value):0.08f*std::pow(18.75f,value));
            mixer->setInstrument(t-4,b,real);
        }else{mixer->strip[t].send[mixer->isInstrument(t)?0:b]=value;sendPickup[t][b].reset();}}
        else if(id>=idReturn && id<idReturn+3){int b=id-idReturn;mixer->returns[b]=value;returnPickup[b].reset();}
        else if(id==idMaster){mixer->master=value;masterPickup.reset();}
        else if(id>=idEffectDial && id<idEffectDial+6){int e=id-idEffectDial;fxValues[e]=value;fxPickup[e].reset();
            const int params[]={DUB_DELAY_TIME,DUB_DELAY_FEEDBACK,DUB_PLATE_DECAY,DUB_PLATE_DAMPING,DUB_PHASER_RATE,DUB_PHASER_DEPTH};
            const float real[]={0.02f*std::pow(175.0f,value),value*1.05f,value*0.98f,value*0.95f,0.05f*std::pow(200.0f,value),value};mixer->setEffect(e/2,params[e],real[e]);
        }return 0;}
    case WM_COMMAND:{int id=LOWORD(wp);
        int selected=int(SendMessageW(songList,LB_GETCURSEL,0,0));
        if(id==idSessionSave||id==idSessionSaveAs){saveSessionDialog(id==idSessionSaveAs);return 0;}
        if(id==idSessionOpen){openSessionDialog();return 0;}
        if(id==idSessionNew){newSession();return 0;}
        if(id==idListUp||id==idListDown){moveLibrarySelection(id==idListUp?-1:1);return 0;}
        if(id==900 && HIWORD(wp)==LBN_SELCHANGE){selectionChanged=GetTickCount64();return 0;}
        if(id==idNext){nextSong();return 0;}
        if(id==idTogglePlay){SendMessageW(window,WM_COMMAND,mixer->playing?idPause:idPlay,0);return 0;}
        if(id>=idOutputBase && id<idOutputBase+int(deviceIds.size())){
            if(mixer->playing){error(L"Pulsa Pausa antes de cambiar la salida de audio.");return 0;}
            stopAudio();closeAudio();selectedOutput=id-idOutputBase;preferredOutput=deviceNames[selectedOutput];saveLibrary();updateAudioLabel();buildMenus();return 0;
        }
        if(id==idLoadReady || (id==900 && HIWORD(wp)==LBN_DBLCLK)){requestLoad();return 0;}
        if(id==idQueuePause){queuePaused=!queuePaused;setText(GetDlgItem(window,idQueuePause),queuePaused?L"Seguir cola":L"Pausar cola");return 0;}
        if(id==idRemoveSong && selected>=0 && size_t(selected)<songs.size()){
            if(preparing || loading){error(L"Espera a que termine el trabajo antes de quitar entradas.");return 0;}warmCache.erase(songs[selected].cache);songs.erase(songs.begin()+selected);sessionDirty=true;saveLibrary();refreshLibrary();return 0;}
        if(id==idRetrySong && selected>=0 && size_t(selected)<songs.size() && selected!=activeJob){songs[selected].state=L"En cola";refreshLibrary();runNext();return 0;}

        if(id==idCancel){queuePaused=true;setText(GetDlgItem(window,idQueuePause),L"Seguir cola");cancelPreparation=true;setProgress(L"Cancelando al terminar el bloque actual...");return 0;}
        if(id==idLibrary){showLibrary();return 0;}
        if(id==idExit){SendMessageW(window,WM_CLOSE,0,0);return 0;}
        if(id==idFast || id==idQuality || id==idCPU || (id>=idGPU && id<idGPU+int(adapters.size()))){
            if(id==idFast || id==idQuality)fastMode=id==idFast;else gpuDevice=id==idCPU?-1:adapters[id-idGPU].first;
            saveLibrary();buildMenus();return 0;}
        if(id>=idLearn && id<idLearn+3){learnTransport=id-idLearn;setText(midiLabel,L"Pulsa un boton MIDI (REC ARM 1-4 reservado).");return 0;}
        if(id==idLastError){showStemError();return 0;}
        if(id==idAbout){theme::message(window,L"Agrega canciones en Archivo. El analisis no detiene la musica. En la lista: espera Lista, pulsa Pausa y Cargar seleccionada, despues Play.\n\nBank Left: Play. Bank Right: Reiniciar. SOLO + MUTE 7: Pausa. Espacio: Play/Pausa. REC ARM 1-4: Solo. SEND ALL transmite todos los controles y no se utiliza como transporte. Aprende otros botones en Transporte.\n\nGPU experimental: selecciona NVIDIA en Preferencias. Si falla, usa CPU y Reintentar. Rapido y Calidad tienen caches separados. No requiere Internet tras descargar sus modelos.",L"DubStemMix alpha 0.6.0",MB_OK);return 0;}
        if(id==idDataFolder){if(preparing || loading){error(L"Espera a que termine el trabajo antes de cambiar la carpeta.");return 0;}BROWSEINFOW bi{};bi.hwndOwner=window;bi.lpszTitle=L"Elige una carpeta para modelos y stems (puede estar en D:)";bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
            auto pidl=SHBrowseForFolderW(&bi);if(pidl){wchar_t path[MAX_PATH]{};if(SHGetPathFromIDListW(pidl,path)){try{dubwin::setDataRoot(std::filesystem::path(path)/L"DubStemMix");theme::message(window,L"Carpeta guardada. Los archivos existentes permanecen en su ubicacion anterior.",L"DubStemMix",MB_OK);}catch(const std::exception& e){error(wide(e.what()).c_str());}}CoTaskMemFree(pidl);}return 0;}
        if(id==idSong){startPreparation();return 0;}
        if(id==idClearSolo){for(int t=0;t<6;++t){mixer->strip[t].solo=false;syncButtons(t);}return 0;}
        if(id==idPanic){mixer->panic();resetGestures();for(auto& row:instrumentOn)for(auto& on:row)on=false;for(int t=4;t<6;++t)syncInstrumentUI(t);return 0;}
        if(id>=idFX && id<idFX+6){int a=id-idFX;effectAction(a,!fxOn[a]);return 0;}
        if(id>=idClear && id<idClear+6){if(loading)return 0;if(mixer->playing){error(L"Deten la reproduccion antes de vaciar una pista");return 0;}stopAudio();mixer->reset();resetGestures();int t=id-idClear;mixer->strip[t].samples.clear();mixer->strip[t].mute=false;mixer->strip[t].solo=false;setText(names[t],L"Sin pista");syncInstrumentUI(t);rebuildWaveform();loadedSource.clear();loadedCache.clear();resetPickup();InvalidateRect(window,nullptr,FALSE);return 0;}
        if(id>=idLoad && id<idLoad+6){loadTrack(id-idLoad);return 0;}
        if(id>=idMute && id<idMute+6){int t=id-idMute;if(mixer->isInstrument(t)){instrumentAction(t-4,0,t==4?!instrumentOn[0][0]:true);return 0;}auto& state=((GetKeyState(VK_MENU)&0x8000)||mixer->strip[t].solo.load())?mixer->strip[t].solo:mixer->strip[t].mute;state=!state.load();syncButtons(t);return 0;}
        if(id>=idSolo && id<idSolo+6){int t=id-idSolo;if(mixer->isInstrument(t)){instrumentAction(t-4,1,!instrumentOn[t-4][1]);return 0;}mixer->strip[t].solo=!mixer->strip[t].solo.load();syncButtons(t);return 0;}
        switch(id){
        case idPlay:
            if(loading){error(L"Espera a que termine de cargar la cancion seleccionada.");break;}
            if(!mixer->totalFrames()){error(L"Carga al menos una pista primero.");break;}
            if(mixer->position.load()>=mixer->totalFrames()){stopAudio();mixer->reset();resetGestures();}
            if(!deviceReady && !openAudio())break;
            mixer->playing=true;
            if(!ma_device_is_started(&device) && ma_device_start(&device)!=MA_SUCCESS){mixer->playing=false;closeAudio();error(L"No se pudo iniciar el audio. Actualiza y selecciona la salida de nuevo.");}break;
        case idPause:resumeAfterLoad=false;mixer->playing=false;break;
        case idStop:resumeAfterLoad=false;stopAudio();mixer->reset();resetGestures();for(auto& row:instrumentOn)for(auto& on:row)on=false;for(int t=4;t<6;++t)syncInstrumentUI(t);break;
        case idAudio:enumerateAudio();break;
        case idMidi:connectMidi();break;

        }return 0;}
    case WM_CLOSE:stopBackground();if(loading){cancelLoad=true;if(loader.joinable())loader.join();loading=false;}if(preparing.load()){queuePaused=true;cancelPreparation=true;error(L"Cancelando preparacion. Espera a que termine el bloque actual y vuelve a cerrar.");return 0;}saveLibrary();KillTimer(h,1);closeAudio();closeMidi();DestroyWindow(h);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(h,message,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int show) {
    if(command && wcsstr(command,L"--check-separation"))return dubwin::separationSelfTest();
    try {
        uiTestMode=command && wcsstr(command,L"--ui-test");
        SetProcessDPIAware();
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);auto fontFolder=std::filesystem::path(executable).parent_path()/L"fonts";
        AddFontResourceExW((fontFolder/L"Archivo-Variable.ttf").c_str(),FR_PRIVATE,nullptr);
        AddFontResourceExW((fontFolder/L"JetBrainsMono-Variable.ttf").c_str(),FR_PRIVATE,nullptr);
        CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        Gdiplus::GdiplusStartupInput graphicsInput;ULONG_PTR graphicsToken=0;
        if(Gdiplus::GdiplusStartup(&graphicsToken,&graphicsInput,nullptr)!=Gdiplus::Ok)return 1;
        theme::registerDial(instance);WNDCLASSW scrollClass{};scrollClass.hInstance=instance;scrollClass.lpfnWndProc=scrollProc;scrollClass.lpszClassName=L"DubScroll";scrollClass.hCursor=LoadCursorW(nullptr,IDC_HAND);RegisterClassW(&scrollClass);
        mixer=std::make_unique<dubwin::Mixer>();
        const ma_backend backend=uiTestMode?ma_backend_null:ma_backend_wasapi;
        if(ma_context_init(&backend,1,nullptr,&context)!=MA_SUCCESS){error(L"No se pudo iniciar WASAPI.");return 1;}
        contextReady=true;
        INITCOMMONCONTROLSEX cc{sizeof cc,ICC_BAR_CLASSES};InitCommonControlsEx(&cc);
        WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=windowProc;cls.lpszClassName=L"DubStemMixWinAlpha";
        cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=theme::bg();
        RegisterClassW(&cls);RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);RECT rect{0,0,std::min(1280,int(work.right-work.left)-20),std::min(800,int(work.bottom-work.top)-90)};AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,FALSE);
        HWND h=CreateWindowW(cls.lpszClassName,L"DubStemMix — Windows alpha 0.6.0",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
            CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,nullptr);
        if(!h){ma_context_uninit(&context);return 1;}
        ShowWindow(h,show);UpdateWindow(h);
        if(uiTestMode){for(int i=0;i<8;++i){auto folder=std::filesystem::temp_directory_path()/(L"DubStemMix-ui-"+std::to_wstring(GetCurrentProcessId()))/std::to_wstring(i);dubwin::createPreviewSong(folder,i);Song song;song.source=folder/(i?L"Cancion "+std::to_wstring(i+1)+L".wav":L"Prueba de reproduccion.wav");song.cache=folder;song.state=L"Lista";songs.push_back(song);}refreshLibrary();requestLoadIndex(0);}
        MSG msg{};
        while(GetMessageW(&msg,nullptr,0,0)>0){
            if(IsWindowEnabled(h) && (msg.hwnd==h||IsChild(h,msg.hwnd)) && (msg.message==WM_KEYDOWN||msg.message==WM_KEYUP) && msg.wParam==VK_SPACE){
                if(msg.message==WM_KEYDOWN && !(msg.lParam&(1LL<<30)))SendMessageW(h,WM_COMMAND,idTogglePlay,0);continue;
            }
            if(!(libraryWindow && IsDialogMessageW(libraryWindow,&msg)) && !IsDialogMessageW(h,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
        ma_context_uninit(&context);contextReady=false;mixer.reset();if(font)DeleteObject(font);if(headingFont)DeleteObject(headingFont);if(smallFont)DeleteObject(smallFont);Gdiplus::GdiplusShutdown(graphicsToken);return 0;
    }catch(const std::exception& e){auto text=wide(e.what());error(text.c_str());stopBackground();closeAudio();closeMidi();if(contextReady)ma_context_uninit(&context);return 1;}
}

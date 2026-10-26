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
#include "Separator.h"
#include "Theme.h"
#include "miniaudio.h"
#include "Mixer.h"

namespace {
constexpr UINT midiMessage = WM_APP + 1;
constexpr int idPlay=10,idPause=11,idStop=12,idAudio=13,idMidi=14,idDevices=15;
constexpr int idSong=20,idCancel=21,idPanic=22,idDataFolder=23,idClear=30,idInstrument=40,idFX=60,idEffectDial=700;
constexpr int idLoad=100,idLevel=200,idMute=300,idSolo=400,idSend=500,idReturn=600,idMaster=610;
HWND window{},status{},deviceList{},midiLabel{};
HWND levels[6]{},names[6]{},mutes[6]{},solos[6]{},sends[6][3]{},returns[3]{},masterSlider{};
HFONT font{};
float layoutX=1,layoutY=1;
int sx(int v){return static_cast<int>(std::lround(v*layoutX));}
int sy(int v){return static_cast<int>(std::lround(v*layoutY));}
HWND songLabel{},sendLabels[6][3]{},clearButtons[6]{},fxButtons[6]{},fxDials[6]{};
std::thread preparation;
std::atomic<bool> preparing{false},cancelPreparation{false},preparationDone{false};
std::mutex progressMutex;
std::wstring progressText,preparationError,preparationTitle;
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
void pollPreparation();
std::unique_ptr<dubwin::Mixer> mixer;
ma_context context{}; ma_device device{};
bool contextReady=false,deviceReady=false;
std::vector<ma_device_id> deviceIds;
HMIDIIN midiIn{}; HMIDIOUT midiOut{};
bool soloHeld=false;
bool pressed[128]{};
dubwin::Pickup levelPickup[6],sendPickup[6][3],returnPickup[3],masterPickup;

void error(const wchar_t* text) {MessageBoxW(window,text,L"DubStemMix — Windows alpha",MB_OK|MB_ICONWARNING);}
std::wstring wide(const char* text) {
    int n=MultiByteToWideChar(CP_UTF8,0,text,-1,nullptr,0);
    if(n<=0)return L"Dispositivo";
    std::wstring s(static_cast<size_t>(n),L'\0'); MultiByteToWideChar(CP_UTF8,0,text,-1,s.data(),n); s.pop_back(); return s;
}
HWND control(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id=0) {
    if(wcscmp(cls,L"BUTTON")==0) style=(style&~0xFu)|BS_OWNERDRAW;
    auto c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,sx(x),sy(y),sx(w),sy(h),window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE); return c;
}
HWND dial(int x,int y,int w,int h,int id,float value,bool fader=false) {
    auto c=CreateWindowExW(0,L"DubDial",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP,sx(x),sy(y),sx(w),sy(h),window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),fader?reinterpret_cast<void*>(1):nullptr);
    SendMessageW(c,TBM_SETPOS,TRUE,static_cast<LPARAM>(std::lround(value*127)));return c;
}
void led(int note,bool on) {if(midiOut)midiOutShortMsg(midiOut,0x90u|(static_cast<DWORD>(note)<<8)|(on?127u<<16:0));}
void syncButtons(int t) {
    if(mixer->isInstrument(t)){syncInstrumentUI(t);return;}
    SetWindowTextW(mutes[t],mixer->strip[t].mute.load()?L"MUTED":L"MUTE");
    SetWindowTextW(solos[t],mixer->strip[t].solo.load()?L"SOLO ON":L"SOLO");
    led(1+3*t,mixer->strip[t].mute.load()); led(2+3*t,mixer->strip[t].solo.load());
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
    const LRESULT index=SendMessageW(deviceList,CB_GETCURSEL,0,0);
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
void enumerateAudio() {
    if(preparing.load())return;
    if(mixer->playing.load()){error(L"Pulsa Detener antes de cambiar la salida.");return;}
    closeAudio(); mixer->reset();resetGestures(); deviceIds.clear(); SendMessageW(deviceList,CB_RESETCONTENT,0,0);
    ma_device_info* infos=nullptr; ma_uint32 count=0;
    if(!contextReady || ma_context_get_devices(&context,&infos,&count,nullptr,nullptr)!=MA_SUCCESS){error(L"No se pudieron enumerar las salidas de audio.");return;}
    int selected=0;
    for(ma_uint32 i=0;i<count;++i){
        deviceIds.push_back(infos[i].id); auto name=wide(infos[i].name);
        SendMessageW(deviceList,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));
        if(infos[i].isDefault)selected=static_cast<int>(i);
    }
    if(count)SendMessageW(deviceList,CB_SETCURSEL,selected,0);
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
    SetWindowTextW(midiLabel,midiIn?L"MIDImix conectado (pickup activo)":L"MIDImix no disponible. Conecta y pulsa Reconectar MIDI.");
}
void midiValue(int value,std::atomic<float>& target,dubwin::Pickup& pickup,HWND h) {
    if(pickup.accept(value,target.load())){target.store(value/127.0f);SendMessageW(h,TBM_SETPOS,TRUE,value);}
}
void handleMidi(DWORD message) {
    if(preparing.load())return;
    const int type=message&0xF0, d1=(message>>8)&127,d2=(message>>16)&127;
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
        if(d1==27){soloHeld=down;return;}
        const bool edge=down && !pressed[d1];pressed[d1]=down;
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
void loadTrack(int track) {
    if(preparing.load())return;
    if(mixer->playing.load()){error(L"Pulsa Detener antes de cargar pistas. La carga reinicia la posicion de todas las pistas.");return;}
    wchar_t path[32768]{}; OPENFILENAMEW dialog{};dialog.lStructSize=sizeof dialog;dialog.hwndOwner=window;
    dialog.lpstrFilter=L"Audio (WAV, FLAC, MP3)\0*.wav;*.flac;*.mp3\0\0";
    dialog.lpstrFile=path;dialog.nMaxFile=32768;dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetOpenFileNameW(&dialog))return;
    stopAudio();mixer->reset();resetGestures();
    ma_decoder decoder{};auto cfg=ma_decoder_config_init(ma_format_f32,2,dubwin::rate);
    if(ma_decoder_init_file_w(path,&cfg,&decoder)!=MA_SUCCESS){error(L"No se pudo leer el archivo. Prueba con un WAV PCM.");return;}
    SetWindowTextW(status,L"Cargando y convirtiendo audio... Espera a que termine.");UpdateWindow(window);
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
    syncInstrumentUI(track);
    auto name=std::filesystem::path(path).filename().wstring(); SetWindowTextW(names[track],name.c_str());
    SetWindowTextW(status,L"Pista cargada. Carga las otras pistas y pulsa Reproducir.");
}
void updateStatus() {
    pollPreparation();
    if(preparing.load()){std::lock_guard<std::mutex> lock(progressMutex);SetWindowTextW(status,progressText.c_str());return;}
    InvalidateRect(window,nullptr,FALSE);
    if(deviceReady && mixer->playing.load() && !ma_device_is_started(&device)){
        mixer->playing=false; error(L"Se interrumpio la salida de audio. Pulsa Detener y Actualizar audio para reconectar.");
    }
    const auto seconds=mixer->position.load()/dubwin::rate;
    wchar_t buffer[256];swprintf_s(buffer,L"%s  |  %llu:%02llu  |  Pico %.0f%%  |  Windows alpha 0.2 — instrumentos + preparacion",
        mixer->playing.load()?L"REPRODUCIENDO":L"PAUSA / DETENIDO",
        static_cast<unsigned long long>(seconds/60),static_cast<unsigned long long>(seconds%60),mixer->peak.load()*100.0);
    SetWindowTextW(status,buffer);
}
void resetGestures() {
    for(auto& row:instrumentOn)for(auto& on:row)on=false;
    for(int i=0;i<4;++i)fxOn[i]=false;
    mixer->releaseInstruments();mixer->setEffect(0,DUB_DELAY_HOLD,0);mixer->setEffect(0,DUB_DELAY_DOUBLE,0);mixer->setEffect(0,DUB_DELAY_STOP,0);
    const wchar_t* names[]={L"HOLD",L"x2",L"CRASH",L"TAPE STOP"};for(int i=0;i<4;++i)if(fxButtons[i])SetWindowTextW(fxButtons[i],names[i]);
    for(int t=4;t<6;++t)if(mutes[t])syncInstrumentUI(t);
}
bool ensureAudio() {
    if(preparing.load())return false;
    if(!deviceReady && !openAudio())return false;
    if(!ma_device_is_started(&device) && ma_device_start(&device)!=MA_SUCCESS){closeAudio();error(L"No se pudo iniciar audio");return false;}
    return true;
}
void syncInstrumentUI(int t) {
    const bool inst=mixer->isInstrument(t);
    const wchar_t* labels[]={L"DELAY",L"REVERB",L"PHASER"};
    for(int b=0;b<3;++b){
        SetWindowTextW(sendLabels[t][b],inst?(b==0?L"PITCH":(b==1?(t==4?L"RATE":L"DECAY"):L"DELAY")):labels[b]);
        const float v=inst && b<2?instrumentValues[t-4][b]:mixer->strip[t].send[inst?0:b].load();SendMessageW(sends[t][b],TBM_SETPOS,TRUE,static_cast<int>(std::lround(v*127)));
    }
    if(inst){SetWindowTextW(names[t],t==4?L"DUB SIREN":L"SYNDRUM");
        SetWindowTextW(mutes[t],t==4?(instrumentOn[0][0]?L"HOLD ON":L"HOLD"):L"HIT");
        SetWindowTextW(solos[t],t==4?(instrumentOn[0][1]?L"RISE ON":L"RISE"):(instrumentOn[1][1]?L"ROLL ON":L"ROLL"));
        led(1+3*t,instrumentOn[t-4][0]);led(3+3*t,instrumentOn[t-4][1]);
    }else {SetWindowTextW(mutes[t],mixer->strip[t].mute?L"MUTED":L"MUTE");SetWindowTextW(solos[t],mixer->strip[t].solo?L"SOLO ON":L"SOLO");}
}
void instrumentAction(int index,int gesture,bool on) {
    if(!mixer->isInstrument(index+4) || preparing.load())return;
    if(on && !ensureAudio())return;
    instrumentOn[index][gesture]=on;
    int parameter=index==0?(gesture==0?DUB_SIREN_GATE:DUB_SIREN_RISE):(gesture==0?DUB_SYNDRUM_HIT:DUB_SYNDRUM_ROLLING);
    // A one-shot hit clears itself; Note Off must not erase an unconsumed hit.
    if(index!=1 || gesture!=0 || on)mixer->setInstrument(index,parameter,on?1.0f:0.0f);
    syncInstrumentUI(index+4);
}
void effectAction(int action,bool on) {
    if(preparing.load())return;
    if(on && !ensureAudio())return;
    fxOn[action]=on;
    if(action==0)mixer->setEffect(0,DUB_DELAY_HOLD,on?1:0);
    if(action==1)mixer->setEffect(0,DUB_DELAY_DOUBLE,on?1:0);
    if(action==2 && on){mixer->setEffect(1,DUB_SWITCH_SELECT,1);mixer->setEffect(1,DUB_SPRING_CRASH,1);fxOn[4]=true;SetWindowTextW(fxButtons[4],L"SPRING");fxOn[2]=false;}
    if(action==3)mixer->setEffect(0,DUB_DELAY_STOP,on?1:0);
    if(action==4){mixer->setEffect(1,DUB_SWITCH_SELECT,on?1:0);SetWindowTextW(fxButtons[4],on?L"SPRING":L"PLATE");}
    if(action==5){mixer->setEffect(2,DUB_SWITCH_SELECT,on?1:0);SetWindowTextW(fxButtons[5],on?L"FLANGER":L"PHASER");}
    const wchar_t* titles[]={L"HOLD",L"x2",L"CRASH",L"TAPE STOP"};
    if(action<4){auto label=std::wstring(titles[action])+(fxOn[action]?L" ON":L"");SetWindowTextW(fxButtons[action],label.c_str());}
}
void setProgress(const std::wstring& text){std::lock_guard<std::mutex> lock(progressMutex);progressText=text;}
void startPreparation() {
    if(preparing.load())return;
    if(mixer->playing.load()){error(L"Deten la reproduccion antes de preparar una cancion.");return;}
    wchar_t file[32768]{};OPENFILENAMEW d{};d.lStructSize=sizeof d;d.hwndOwner=window;d.lpstrFilter=L"Cancion (WAV, FLAC, MP3)\0*.wav;*.flac;*.mp3\0\0";d.lpstrFile=file;d.nMaxFile=32768;d.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetOpenFileNameW(&d))return;
    if(MessageBoxW(window,L"Preparar esta cancion en voz, bateria, bajo y otros.\n\nLa primera vez se descargan hasta 663 MB de modelos desde Hugging Face. El audio se procesa en tu computador y no se sube. Puede tardar varios minutos. Maximo 10 minutos por cancion.\n\nLos modelos son htdemucs_ft de Meta, exportados por StemSplitio. Consulta NOTICE en licenses para los avisos del modelo.\n\nAl terminar se reemplazan las pistas actuales. ¿Continuar?",L"Preparar cancion",MB_YESNO|MB_ICONINFORMATION)!=IDYES)return;
    stopAudio();mixer->reset();resetGestures();for(auto& row:instrumentOn)for(auto& on:row)on=false;
    preparing=true;cancelPreparation=false;preparationDone=false;preparationError.clear();setProgress(L"Iniciando preparacion...");
    preparationTitle=std::filesystem::path(file).stem().wstring();
    preparation=std::thread([path=std::filesystem::path(file)]{
        try{preparedStems=dubwin::separateSong(path,cancelPreparation,setProgress);}
        catch(const std::exception& e){preparationError=wide(e.what());}
        preparationDone.store(true,std::memory_order_release);
    });
}
void pollPreparation() {
    if(!preparing.load() || !preparationDone.load(std::memory_order_acquire))return;
    if(preparation.joinable())preparation.join();preparing=false;
    if(!preparationError.empty()){error(preparationError.c_str());return;}
    const wchar_t* titles[]={L"VOZ",L"BATERIA",L"BAJO",L"OTROS"};
    for(int t=0;t<6;++t){if(t<4)mixer->strip[t].samples=std::move(preparedStems[t]);else mixer->strip[t].samples.clear();
        mixer->strip[t].mute=false;mixer->strip[t].solo=false;syncInstrumentUI(t);if(t<4)SetWindowTextW(names[t],titles[t]);}
    SetWindowTextW(songLabel,preparationTitle.c_str());resetPickup();
    MessageBoxW(window,L"Cancion preparada. Las cuatro pistas estan listas; Siren y Syndrum quedan en los canales 5 y 6.",L"DubStemMix",MB_OK);
}
void buildUI() {
    font=CreateFontW(-12,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    control(L"STATIC",L"DUBSTEMMIX",0,12,20,130,26);
    control(L"STATIC",L"WIN / ALPHA 0.2",0,12,48,128,20);
    control(L"BUTTON",L"ABRIR CANCION",WS_TABSTOP,10,88,128,34,idSong);
    control(L"BUTTON",L"Cancelar proceso",WS_TABSTOP,10,130,128,28,idCancel);
    control(L"STATIC",L"SALIDA DE AUDIO",0,12,180,120,20);
    deviceList=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,10,204,128,240,idDevices);
    control(L"BUTTON",L"Actualizar audio",WS_TABSTOP,10,240,128,28,idAudio);
    control(L"BUTTON",L"Reconectar MIDI",WS_TABSTOP,10,286,128,28,idMidi);
    midiLabel=control(L"STATIC",L"",0,12,324,122,70);
    control(L"BUTTON",L"Carpeta de datos",WS_TABSTOP,10,382,128,28,idDataFolder);
    control(L"STATIC",L"STEMS + INSTRUMENTOS\n\n1-4: stems\n5: Siren\n6: Syndrum\n7-8: efectos\n\nArrastra las perillas arriba / abajo.\n\nCargar permite abrir stems existentes.",0,12,420,122,245);
    control(L"BUTTON",L"PLAY",WS_TABSTOP,156,15,70,34,idPlay);control(L"BUTTON",L"PAUSA",WS_TABSTOP,234,15,70,34,idPause);control(L"BUTTON",L"REINICIAR",WS_TABSTOP,312,15,90,34,idStop);
    songLabel=control(L"STATIC",L"NUEVA VERSION",SS_ENDELLIPSIS,420,22,380,24);
    control(L"BUTTON",L"PANIC",WS_TABSTOP,970,15,90,34,idPanic);
    const wchar_t* effects[]={L"TIEMPO",L"FEEDBACK",L"DECAY",L"DAMPING",L"RATE",L"DEPTH"};
    for(int e=0;e<6;++e){int x=160+(e/2)*300+(e%2)*105;fxDials[e]=dial(x,118,56,60,idEffectDial+e,fxValues[e]);control(L"STATIC",effects[e],SS_CENTER,x-8,181,75,18);}
    const wchar_t* labels[]={L"1  VOZ",L"2  BATERIA",L"3  BAJO",L"4  OTROS",L"5  SIREN",L"6  SYNDRUM"};
    for(int t=0;t<6;++t){int x=150+t*100;
        control(L"STATIC",labels[t],SS_CENTER,x+2,218,92,20);
        names[t]=control(L"STATIC",L"Sin pista",SS_ENDELLIPSIS|SS_CENTER,x+3,241,90,20);
        for(int b=0;b<3;++b){sends[t][b]=dial(x+20,265+b*73,58,57,idSend+t*3+b,0);sendLabels[t][b]=control(L"STATIC",L"",SS_CENTER,x+3,322+b*73,90,16);}
        mutes[t]=control(L"BUTTON",L"MUTE",WS_TABSTOP,x+6,493,84,26,idMute+t);
        solos[t]=control(L"BUTTON",L"SOLO",WS_TABSTOP,x+6,524,84,26,idSolo+t);
        levels[t]=dial(x+27,557,42,92,idLevel+t,0.7f,true);
        control(L"BUTTON",L"Cargar",WS_TABSTOP,x+5,657,42,24,idLoad+t);
        clearButtons[t]=control(L"BUTTON",L"Vaciar",WS_TABSTOP,x+50,657,42,24,idClear+t);
        syncInstrumentUI(t);
    }
    control(L"STATIC",L"7  DELAY",SS_CENTER,754,218,90,20);control(L"STATIC",L"8  SPACE",SS_CENTER,854,218,90,20);
    const wchar_t* fxNames[]={L"HOLD",L"x2",L"CRASH",L"TAPE STOP",L"PLATE",L"PHASER"};
    const int fxX[]={756,756,856,856,756,856},fxY[]={385,420,385,420,285,285};
    for(int i=0;i<6;++i)fxButtons[i]=control(L"BUTTON",fxNames[i],WS_TABSTOP,fxX[i],fxY[i],84,28,idFX+i);
    control(L"STATIC",L"RETORNOS",SS_CENTER,758,340,182,20);
    returns[0]=dial(777,470,42,175,idReturn,0.5f,true);returns[1]=dial(877,470,42,175,idReturn+1,0.5f,true);
    control(L"STATIC",L"MASTER",SS_CENTER,964,218,96,20);masterSlider=dial(990,270,42,225,idMaster,0.5f,true);
    control(L"STATIC",L"PHASER RETURN",SS_CENTER,959,530,106,20);returns[2]=dial(982,559,58,62,idReturn+2,0.5f);
    status=control(L"STATIC",L"",0,12,706,1050,28);
    SendMessageW(deviceList,CB_SETDROPPEDWIDTH,450,0);
    enumerateAudio();connectMidi();
    for(int e=0;e<6;++e)SendMessageW(window,WM_HSCROLL,0,reinterpret_cast<LPARAM>(fxDials[e]));
    for(int t=4;t<6;++t)for(int b=0;b<2;++b)SendMessageW(window,WM_HSCROLL,0,reinterpret_cast<LPARAM>(sends[t][b]));
    SetTimer(window,1,150,nullptr);
}
LRESULT CALLBACK windowProc(HWND h,UINT message,WPARAM wp,LPARAM lp) {
    switch(message){
    case WM_CREATE:{window=h;RECT r;GetClientRect(h,&r);layoutX=float(r.right)/1080;layoutY=float(r.bottom)/740;buildUI();return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps;auto dc=BeginPaint(h,&ps);RECT r;GetClientRect(h,&r);FillRect(dc,&r,theme::bg());
        for(int i=0;i<9;++i){RECT strip{150+i*100,210,246+i*100,692};if(i==8)strip.right=1068;strip={sx(strip.left),sy(strip.top),sx(strip.right),sy(strip.bottom)};FillRect(dc,&strip,theme::surface());}
        RECT wave{sx(150),sy(62),sx(1068),sy(107)};FillRect(dc,&wave,theme::surface());
        const std::vector<float>* samples=nullptr;for(auto& strip:mixer->strip)if(!strip.samples.empty()){samples=&strip.samples;break;}
        if(samples){auto pen=CreatePen(PS_SOLID,1,theme::muted);auto old=SelectObject(dc,pen);size_t frames=samples->size()/2;
            for(int x=0;x<300;++x){size_t i=static_cast<size_t>(x)*frames/300;float peak=0;for(size_t j=0;j<128 && i+j<frames;++j)peak=std::max(peak,std::abs((*samples)[(i+j)*2]));int a=static_cast<int>(std::min(peak,1.0f)*20);MoveToEx(dc,sx(155+x*3),sy(84-a),nullptr);LineTo(dc,sx(155+x*3),sy(84+a+1));}SelectObject(dc,old);DeleteObject(pen);
            if(frames){int x=155+static_cast<int>(std::min(1.0,double(mixer->position.load())/frames)*900);pen=CreatePen(PS_SOLID,2,theme::gold);old=SelectObject(dc,pen);MoveToEx(dc,sx(x),sy(64),nullptr);LineTo(dc,sx(x),sy(105));SelectObject(dc,old);DeleteObject(pen);}}
        EndPaint(h,&ps);return 0;}
    case WM_DRAWITEM:theme::button(reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORLISTBOX:{auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,theme::text);SetBkMode(dc,TRANSPARENT);SetBkColor(dc,theme::background);
        RECT child;GetWindowRect(reinterpret_cast<HWND>(lp),&child);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&child),2);
        if(child.left>=sx(150) && child.top>=sy(210) && child.top<sy(692))return reinterpret_cast<LRESULT>(theme::surface());
        return reinterpret_cast<LRESULT>(theme::bg());}
    case midiMessage:handleMidi(static_cast<DWORD>(wp));return 0;
    case WM_TIMER:updateStatus();return 0;
    case WM_HSCROLL:{
        if(preparing.load())return 0;
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
        if(id==idCancel){cancelPreparation=true;setProgress(L"Cancelando al terminar el bloque actual...");return 0;}
        if(preparing.load())return 0;
        if(id==idDataFolder){BROWSEINFOW bi{};bi.hwndOwner=window;bi.lpszTitle=L"Elige una carpeta para modelos y stems (puede estar en D:)";bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
            auto pidl=SHBrowseForFolderW(&bi);if(pidl){wchar_t path[MAX_PATH]{};if(SHGetPathFromIDListW(pidl,path)){try{dubwin::setDataRoot(std::filesystem::path(path)/L"DubStemMix");MessageBoxW(window,L"Carpeta guardada. Los archivos existentes permanecen en su ubicacion anterior.",L"DubStemMix",MB_OK);}catch(const std::exception& e){error(wide(e.what()).c_str());}}CoTaskMemFree(pidl);}return 0;}
        if(id==idSong){startPreparation();return 0;}
        if(id==idPanic){mixer->panic();resetGestures();for(auto& row:instrumentOn)for(auto& on:row)on=false;for(int t=4;t<6;++t)syncInstrumentUI(t);return 0;}
        if(id>=idFX && id<idFX+6){int a=id-idFX;effectAction(a,!fxOn[a]);return 0;}
        if(id>=idClear && id<idClear+6){if(mixer->playing){error(L"Deten la reproduccion antes de vaciar una pista");return 0;}stopAudio();mixer->reset();resetGestures();int t=id-idClear;mixer->strip[t].samples.clear();mixer->strip[t].mute=false;mixer->strip[t].solo=false;SetWindowTextW(names[t],L"Sin pista");syncInstrumentUI(t);resetPickup();return 0;}
        if(id>=idLoad && id<idLoad+6){loadTrack(id-idLoad);return 0;}
        if(id>=idMute && id<idMute+6){int t=id-idMute;if(mixer->isInstrument(t)){instrumentAction(t-4,0,t==4?!instrumentOn[0][0]:true);return 0;}mixer->strip[t].mute=!mixer->strip[t].mute.load();syncButtons(t);return 0;}
        if(id>=idSolo && id<idSolo+6){int t=id-idSolo;if(mixer->isInstrument(t)){instrumentAction(t-4,1,!instrumentOn[t-4][1]);return 0;}mixer->strip[t].solo=!mixer->strip[t].solo.load();syncButtons(t);return 0;}
        switch(id){
        case idPlay:
            if(!mixer->totalFrames()){error(L"Carga al menos una pista primero.");break;}
            if(mixer->position.load()>=mixer->totalFrames()){stopAudio();mixer->reset();resetGestures();}
            if(!deviceReady && !openAudio())break;
            mixer->playing=true;
            if(ma_device_start(&device)!=MA_SUCCESS){mixer->playing=false;closeAudio();error(L"No se pudo iniciar el audio. Actualiza y selecciona la salida de nuevo.");}break;
        case idPause:mixer->playing=false;break;
        case idStop:stopAudio();mixer->reset();resetGestures();for(auto& row:instrumentOn)for(auto& on:row)on=false;for(int t=4;t<6;++t)syncInstrumentUI(t);break;
        case idAudio:enumerateAudio();break;
        case idMidi:connectMidi();break;
        case idDevices:if(HIWORD(wp)==CBN_SELCHANGE){stopAudio();closeAudio();mixer->reset();resetGestures();}break;
        }return 0;}
    case WM_CLOSE:if(preparing.load()){cancelPreparation=true;error(L"Cancelando preparacion. Espera a que termine el bloque actual y vuelve a cerrar.");return 0;}KillTimer(h,1);closeAudio();closeMidi();DestroyWindow(h);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(h,message,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int show) {
    if(command && wcsstr(command,L"--check-separation"))return dubwin::separationSelfTest();
    try {
        CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        theme::registerDial(instance);
        mixer=std::make_unique<dubwin::Mixer>();
        const ma_backend backend=ma_backend_wasapi;
        if(ma_context_init(&backend,1,nullptr,&context)!=MA_SUCCESS){error(L"No se pudo iniciar WASAPI.");return 1;}
        contextReady=true;
        INITCOMMONCONTROLSEX cc{sizeof cc,ICC_BAR_CLASSES};InitCommonControlsEx(&cc);
        WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=windowProc;cls.lpszClassName=L"DubStemMixWinAlpha";
        cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=theme::bg();
        RegisterClassW(&cls);RECT rect{0,0,1080,740};AdjustWindowRect(&rect,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN,FALSE);
        HWND h=CreateWindowW(cls.lpszClassName,L"DubStemMix — Windows alpha 0.2",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN,
            CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,nullptr);
        if(!h){ma_context_uninit(&context);return 1;}
        ShowWindow(h,show);UpdateWindow(h);MSG msg{};
        while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(h,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
        ma_context_uninit(&context);contextReady=false;mixer.reset();return 0;
    }catch(const std::exception& e){auto text=wide(e.what());error(text.c_str());closeAudio();closeMidi();if(contextReady)ma_context_uninit(&context);return 1;}
}

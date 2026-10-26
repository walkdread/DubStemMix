#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <filesystem>
#include <memory>
#include <string>
#include <cwctype>
#include "miniaudio.h"
#include "Mixer.h"

namespace {
constexpr UINT midiMessage = WM_APP + 1;
constexpr int idPlay=10,idPause=11,idStop=12,idAudio=13,idMidi=14,idDevices=15;
constexpr int idLoad=100,idLevel=200,idMute=300,idSolo=400,idSend=500,idReturn=600,idMaster=610;
HWND window{},status{},deviceList{},midiLabel{};
HWND levels[6]{},names[6]{},mutes[6]{},solos[6]{},sends[6][3]{},returns[3]{},masterSlider{};
HFONT font{};
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
    auto c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE); return c;
}
HWND slider(int x,int y,int w,int id,float value) {
    auto h=control(TRACKBAR_CLASSW,L"",TBS_HORZ|TBS_NOTICKS|WS_TABSTOP,x,y,w,28,id);
    SendMessageW(h,TBM_SETRANGE,TRUE,MAKELPARAM(0,127));
    SendMessageW(h,TBM_SETPOS,TRUE,static_cast<LPARAM>(std::lround(value*127))); return h;
}
void led(int note,bool on) {if(midiOut)midiOutShortMsg(midiOut,0x90u|(static_cast<DWORD>(note)<<8)|(on?127u<<16:0));}
void syncButtons(int t) {
    SendMessageW(mutes[t],BM_SETCHECK,mixer->strip[t].mute.load()?BST_CHECKED:BST_UNCHECKED,0);
    SendMessageW(solos[t],BM_SETCHECK,mixer->strip[t].solo.load()?BST_CHECKED:BST_UNCHECKED,0);
    led(1+3*t,mixer->strip[t].mute.load()); led(2+3*t,mixer->strip[t].solo.load());
}
void resetPickup() {
    for(auto& p:levelPickup)p.reset(); for(auto& row:sendPickup)for(auto& p:row)p.reset();
    for(auto& p:returnPickup)p.reset(); masterPickup.reset();
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
    if(mixer->playing.load()){error(L"Pulsa Detener antes de cambiar la salida.");return;}
    closeAudio(); mixer->reset(); deviceIds.clear(); SendMessageW(deviceList,CB_RESETCONTENT,0,0);
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
    const int type=message&0xF0, d1=(message>>8)&127,d2=(message>>16)&127;
    if(type==0xB0){
        if(d1==62)midiValue(d2,mixer->master,masterPickup,masterSlider);
        for(int t=0;t<6;++t){
            if(d1==dubwin::faderCC[t])midiValue(d2,mixer->strip[t].level,levelPickup[t],levels[t]);
            for(int b=0;b<3;++b)if(d1==dubwin::knobCC[t][b])midiValue(d2,mixer->strip[t].send[b],sendPickup[t][b],sends[t][b]);
        }
        if(d1==57)midiValue(d2,mixer->returns[0],returnPickup[0],returns[0]);
        if(d1==61)midiValue(d2,mixer->returns[1],returnPickup[1],returns[1]);
    }else if(type==0x90 || type==0x80){
        const bool down=type==0x90 && d2!=0;
        if(d1==27){soloHeld=down;return;}
        const bool edge=down && !pressed[d1];pressed[d1]=down;if(!edge)return;
        for(int t=0;t<6;++t){
            if(d1==1+3*t){auto& state=soloHeld?mixer->strip[t].solo:mixer->strip[t].mute;state.store(!state.load());syncButtons(t);}
            if(d1==2+3*t){auto& state=mixer->strip[t].solo;state.store(!state.load());syncButtons(t);}
        }
    }
}
void loadTrack(int track) {
    if(mixer->playing.load()){error(L"Pulsa Detener antes de cargar pistas. La carga reinicia la posicion de todas las pistas.");return;}
    wchar_t path[32768]{}; OPENFILENAMEW dialog{};dialog.lStructSize=sizeof dialog;dialog.hwndOwner=window;
    dialog.lpstrFilter=L"Audio (WAV, FLAC, MP3)\0*.wav;*.flac;*.mp3\0\0";
    dialog.lpstrFile=path;dialog.nMaxFile=32768;dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetOpenFileNameW(&dialog))return;
    stopAudio();mixer->reset();
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
    mixer->strip[track].samples=std::move(data);
    auto name=std::filesystem::path(path).filename().wstring(); SetWindowTextW(names[track],name.c_str());
    SetWindowTextW(status,L"Pista cargada. Carga las otras pistas y pulsa Reproducir.");
}
void updateStatus() {
    if(deviceReady && mixer->playing.load() && !ma_device_is_started(&device)){
        mixer->playing=false; error(L"Se interrumpio la salida de audio. Pulsa Detener y Actualizar audio para reconectar.");
    }
    const auto seconds=mixer->position.load()/dubwin::rate;
    wchar_t buffer[256];swprintf_s(buffer,L"%s  |  %llu:%02llu  |  Pico %.0f%%  |  Windows alpha 0.1 — prueba, sin grabacion",
        mixer->playing.load()?L"REPRODUCIENDO":L"PAUSA / DETENIDO",
        static_cast<unsigned long long>(seconds/60),static_cast<unsigned long long>(seconds%60),mixer->peak.load()*100.0);
    SetWindowTextW(status,buffer);
}
void buildUI() {
    font=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    control(L"STATIC",L"DubStemMix / Windows — primera prueba",0,20,16,650,24);
    control(L"STATIC",L"1. Elige salida   2. Carga tus stems   3. Reproduce   4. Sube los faders con suavidad",0,20,44,1020,24);
    deviceList=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,20,78,520,240,idDevices);
    control(L"BUTTON",L"Actualizar audio",WS_TABSTOP,552,76,150,28,idAudio);
    control(L"BUTTON",L"Reconectar MIDI",WS_TABSTOP,714,76,155,28,idMidi);
    midiLabel=control(L"STATIC",L"",0,20,116,1020,24);
    const wchar_t* labels[]={L"1 / Voz",L"2 / Bateria",L"3 / Bajo",L"4 / Otros",L"5 / Extra",L"6 / Extra"};
    for(int t=0;t<6;++t){const int x=20+t*174;
        control(L"BUTTON",labels[t],BS_GROUPBOX,x,150,164,300);
        control(L"BUTTON",L"Cargar audio",WS_TABSTOP,x+10,178,144,27,idLoad+t);
        names[t]=control(L"STATIC",L"Sin pista",SS_PATHELLIPSIS,x+10,214,144,36);
        control(L"STATIC",L"Volumen",0,x+10,256,140,18); levels[t]=slider(x+6,276,150,idLevel+t,0.7f);
        mutes[t]=control(L"BUTTON",L"Mute",BS_AUTOCHECKBOX|WS_TABSTOP,x+10,310,70,22,idMute+t);
        solos[t]=control(L"BUTTON",L"Solo",BS_AUTOCHECKBOX|WS_TABSTOP,x+84,310,70,22,idSolo+t);
        const wchar_t* busNames[]={L"Delay",L"Reverb",L"Phaser"};
        for(int b=0;b<3;++b){control(L"STATIC",busNames[b],0,x+10,342+b*32,48,20);sends[t][b]=slider(x+58,337+b*32,98,idSend+t*3+b,0);}
    }
    control(L"BUTTON",L"Reproducir",WS_TABSTOP,20,470,150,36,idPlay);
    control(L"BUTTON",L"Pausa",WS_TABSTOP,182,470,120,36,idPause);
    control(L"BUTTON",L"Detener / reiniciar",WS_TABSTOP,314,470,180,36,idStop);
    control(L"STATIC",L"Master",0,535,480,60,20);masterSlider=slider(600,473,250,idMaster,0.5f);
    const wchar_t* busNames[]={L"Retorno delay (fader 7)",L"Retorno reverb (fader 8)",L"Retorno phaser (raton)"};
    for(int b=0;b<3;++b){control(L"STATIC",busNames[b],0,20+b*350,527,240,20);returns[b]=slider(20+b*350,551,300,idReturn+b,0.5f);}
    control(L"STATIC",L"MIDImix: faders 1–6 = pistas; perillas superior / media / inferior = delay / reverb / phaser.",0,20,590,1040,22);
    control(L"STATIC",L"Pickup: mueve cada control hasta alcanzar el valor en pantalla. Bancos y REC ARM no estan activos en esta prueba.",0,20,616,1040,22);
    status=control(L"STATIC",L"",0,20,650,1040,30);
    enumerateAudio();connectMidi();SetTimer(window,1,150,nullptr);
}
LRESULT CALLBACK windowProc(HWND h,UINT message,WPARAM wp,LPARAM lp) {
    switch(message){
    case WM_CREATE:window=h;buildUI();return 0;
    case midiMessage:handleMidi(static_cast<DWORD>(wp));return 0;
    case WM_TIMER:updateStatus();return 0;
    case WM_HSCROLL:{
        HWND s=reinterpret_cast<HWND>(lp);if(!s)break;int id=GetDlgCtrlID(s);float value=static_cast<float>(SendMessageW(s,TBM_GETPOS,0,0))/127;
        if(id>=idLevel && id<idLevel+6){int t=id-idLevel;mixer->strip[t].level=value;levelPickup[t].reset();}
        else if(id>=idSend && id<idSend+18){int t=(id-idSend)/3,b=(id-idSend)%3;mixer->strip[t].send[b]=value;sendPickup[t][b].reset();}
        else if(id>=idReturn && id<idReturn+3){int b=id-idReturn;mixer->returns[b]=value;returnPickup[b].reset();}
        else if(id==idMaster){mixer->master=value;masterPickup.reset();}return 0;}
    case WM_COMMAND:{int id=LOWORD(wp);
        if(id>=idLoad && id<idLoad+6){loadTrack(id-idLoad);return 0;}
        if(id>=idMute && id<idMute+6){int t=id-idMute;mixer->strip[t].mute=!mixer->strip[t].mute.load();syncButtons(t);return 0;}
        if(id>=idSolo && id<idSolo+6){int t=id-idSolo;mixer->strip[t].solo=!mixer->strip[t].solo.load();syncButtons(t);return 0;}
        switch(id){
        case idPlay:
            if(!mixer->totalFrames()){error(L"Carga al menos una pista primero.");break;}
            if(mixer->position.load()>=mixer->totalFrames()){stopAudio();mixer->reset();}
            if(!deviceReady && !openAudio())break;
            mixer->playing=true;
            if(ma_device_start(&device)!=MA_SUCCESS){mixer->playing=false;closeAudio();error(L"No se pudo iniciar el audio. Actualiza y selecciona la salida de nuevo.");}break;
        case idPause:mixer->playing=false;break;
        case idStop:stopAudio();mixer->reset();break;
        case idAudio:enumerateAudio();break;
        case idMidi:connectMidi();break;
        case idDevices:if(HIWORD(wp)==CBN_SELCHANGE){stopAudio();closeAudio();mixer->reset();}break;
        }return 0;}
    case WM_CLOSE:KillTimer(h,1);closeAudio();closeMidi();DestroyWindow(h);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(h,message,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    try {
        mixer=std::make_unique<dubwin::Mixer>();
        const ma_backend backend=ma_backend_wasapi;
        if(ma_context_init(&backend,1,nullptr,&context)!=MA_SUCCESS){error(L"No se pudo iniciar WASAPI.");return 1;}
        contextReady=true;
        INITCOMMONCONTROLSEX cc{sizeof cc,ICC_BAR_CLASSES};InitCommonControlsEx(&cc);
        WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=windowProc;cls.lpszClassName=L"DubStemMixWinAlpha";
        cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);
        RegisterClassW(&cls);RECT rect{0,0,1080,700};AdjustWindowRect(&rect,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE);
        HWND h=CreateWindowW(cls.lpszClassName,L"DubStemMix — Windows alpha 0.1",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
            CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,nullptr);
        if(!h){ma_context_uninit(&context);return 1;}
        ShowWindow(h,show);UpdateWindow(h);MSG msg{};
        while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(h,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
        ma_context_uninit(&context);contextReady=false;mixer.reset();return 0;
    }catch(const std::exception& e){auto text=wide(e.what());error(text.c_str());closeAudio();closeMidi();if(contextReady)ma_context_uninit(&context);return 1;}
}

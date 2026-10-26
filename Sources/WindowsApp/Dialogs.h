#pragma once
#include "Theme.h"
#include <string>
#include <cstring>
namespace theme {
struct Notice {const wchar_t* text;const wchar_t* title;UINT flags;int result=0;HFONT font=nullptr;};
inline LRESULT CALLBACK noticeProc(HWND h,UINT m,WPARAM w,LPARAM l){
    auto* n=reinterpret_cast<Notice*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(m==WM_NCCREATE){n=static_cast<Notice*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(n));return TRUE;}
    if(!n)return DefWindowProcW(h,m,w,l);
    switch(m){
    case WM_CREATE:{RECT r;GetClientRect(h,&r);n->font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Archivo");
        auto edit=CreateWindowExW(0,L"EDIT",n->text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,22,60,r.right-44,r.bottom-128,h,reinterpret_cast<HMENU>(100),nullptr,nullptr);
        SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(n->font),TRUE);
        bool question=(n->flags&MB_YESNO)==MB_YESNO;
        auto add=[&](int id,const wchar_t* label,int x){auto b=CreateWindowW(L"BUTTON",label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,x,r.bottom-52,120,32,h,reinterpret_cast<HMENU>(INT_PTR(id)),nullptr,nullptr);SendMessageW(b,WM_SETFONT,reinterpret_cast<WPARAM>(n->font),TRUE);return b;};
        if(question)add(IDNO,L"Cancelar",r.right-276);
        auto accept=add(question?IDYES:IDOK,question?L"Continuar":L"Aceptar",r.right-142);SetFocus(accept);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT p;auto dc=BeginPaint(h,&p);RECT r;GetClientRect(h,&r);PaintBuffer buffer(dc,r);FillRect(buffer.dc,&r,bg());card(buffer.dc,r,background,gold);r={22,18,r.right-22,47};SetBkMode(buffer.dc,TRANSPARENT);SetTextColor(buffer.dc,gold);auto old=SelectObject(buffer.dc,n->font);DrawTextW(buffer.dc,n->title,-1,&r,DT_SINGLELINE|DT_END_ELLIPSIS);SelectObject(buffer.dc,old);buffer.present();EndPaint(h,&p);return 0;}
    case WM_DRAWITEM:button(reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:{auto dc=reinterpret_cast<HDC>(w);SetTextColor(dc,text);SetBkColor(dc,panel);return reinterpret_cast<LRESULT>(surface());}
    case WM_COMMAND:if(LOWORD(w)==IDOK||LOWORD(w)==IDYES||LOWORD(w)==IDNO||LOWORD(w)==IDCANCEL){n->result=LOWORD(w)==IDCANCEL?IDNO:LOWORD(w);DestroyWindow(h);return 0;}break;
    case WM_CLOSE:n->result=(n->flags&MB_YESNO)==MB_YESNO?IDNO:IDOK;DestroyWindow(h);return 0;
    case WM_NCDESTROY:if(n->font)DeleteObject(n->font);SetWindowLongPtrW(h,GWLP_USERDATA,0);return 0;
    }return DefWindowProcW(h,m,w,l);
}
inline int message(HWND owner,const wchar_t* body,const wchar_t* title,UINT flags){
    WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpfnWndProc=noticeProc;c.lpszClassName=L"DubNotice";c.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&c);
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);RECT bounds=work;if(owner)GetWindowRect(owner,&bounds);
    int width=std::min(680,int(work.right-work.left)-24),height=std::min(420,int(work.bottom-work.top)-24);
    int x=std::clamp(int((bounds.left+bounds.right-width)/2),int(work.left),int(work.right-width));int y=std::clamp(int((bounds.top+bounds.bottom-height)/2),int(work.top),int(work.bottom-height));
    Notice n{body,title,flags};bool enabled=owner && IsWindowEnabled(owner);if(enabled)EnableWindow(owner,FALSE);
    auto h=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,c.lpszClassName,title,WS_POPUP|WS_BORDER,x,y,width,height,owner,nullptr,c.hInstance,&n);
    if(!h){if(enabled)EnableWindow(owner,TRUE);return IDNO;}
    ShowWindow(h,SW_SHOW);UpdateWindow(h);MSG msg{};
    while(IsWindow(h)){int result=GetMessageW(&msg,nullptr,0,0);if(result<=0){if(result==0)PostQuitMessage(int(msg.wParam));break;}
        if(msg.message==WM_KEYDOWN && (msg.wParam==VK_ESCAPE||msg.wParam==VK_RETURN)){SendMessageW(h,WM_COMMAND,msg.wParam==VK_ESCAPE?IDCANCEL:((flags&MB_YESNO)==MB_YESNO?IDYES:IDOK),0);continue;}
        if(msg.message==WM_KEYDOWN && msg.wParam=='C' && (GetKeyState(VK_CONTROL)&0x8000)){
            size_t bytes=(wcslen(body)+1)*sizeof(wchar_t);if(OpenClipboard(h)){auto memory=GlobalAlloc(GMEM_MOVEABLE,bytes);if(memory){auto dest=GlobalLock(memory);if(dest){memcpy(dest,body,bytes);GlobalUnlock(memory);EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,memory))GlobalFree(memory);}else GlobalFree(memory);}CloseClipboard();}continue;
        }
        if(!IsDialogMessageW(h,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    if(IsWindow(h))DestroyWindow(h);if(enabled){EnableWindow(owner,TRUE);SetActiveWindow(owner);}return n.result;
}
}

#pragma once
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <gdiplus.h>
namespace theme {
constexpr COLORREF background=RGB(20,18,16),panel=RGB(32,29,25),text=RGB(235,225,203),muted=RGB(155,144,124);
constexpr COLORREF gold=RGB(219,166,52),green=RGB(125,155,83),red=RGB(203,83,52);
inline HBRUSH bg(){static HBRUSH b=CreateSolidBrush(background);return b;}
inline HBRUSH surface(){static HBRUSH b=CreateSolidBrush(panel);return b;}
inline Gdiplus::Color color(COLORREF c){return Gdiplus::Color(255,GetRValue(c),GetGValue(c),GetBValue(c));}
inline void card(HDC dc,RECT r,COLORREF fill,COLORREF stroke){
    Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath path;float x=float(r.left)+0.5f,y=float(r.top)+0.5f,w=float(r.right-r.left)-1,h=float(r.bottom-r.top)-1,d=12;
    path.AddArc(x,y,d,d,180,90);path.AddArc(x+w-d,y,d,d,270,90);path.AddArc(x+w-d,y+h-d,d,d,0,90);path.AddArc(x,y+h-d,d,d,90,90);path.CloseFigure();
    Gdiplus::SolidBrush brush(color(fill));Gdiplus::Pen pen(color(stroke));g.FillPath(&brush,&path);g.DrawPath(&pen,&path);
}
inline void active(HWND h,bool on){if(bool(GetPropW(h,L"active"))!=on){SetPropW(h,L"active",reinterpret_cast<HANDLE>(INT_PTR(on)));InvalidateRect(h,nullptr,FALSE);}}
struct Dial {HFONT font{};int value=0;bool fader=false,drag=false;int startY=0,startValue=0;};
inline void changed(HWND h,Dial& d,int value){d.value=std::clamp(value,0,127);InvalidateRect(h,nullptr,FALSE);SendMessageW(GetParent(h),WM_HSCROLL,TB_THUMBTRACK,reinterpret_cast<LPARAM>(h));}
inline LRESULT CALLBACK dialProc(HWND h,UINT m,WPARAM w,LPARAM l){
    auto* d=reinterpret_cast<Dial*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(m==WM_NCCREATE){auto* cs=reinterpret_cast<CREATESTRUCTW*>(l);d=new Dial;d->fader=cs->lpCreateParams!=nullptr;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(d));return TRUE;}
    if(!d)return DefWindowProcW(h,m,w,l);
    switch(m){
    case WM_SETFONT:d->font=reinterpret_cast<HFONT>(w);if(l)InvalidateRect(h,nullptr,FALSE);return 0;
    case WM_GETFONT:return reinterpret_cast<LRESULT>(d->font);
    case TBM_SETRANGE:return 0;
    case TBM_SETPOS:d->value=std::clamp(static_cast<int>(l),0,127);InvalidateRect(h,nullptr,FALSE);return 0;
    case TBM_GETPOS:return d->value;
    case WM_GETDLGCODE:return DLGC_WANTARROWS;
    case WM_LBUTTONDOWN:d->drag=true;d->startY=static_cast<short>(HIWORD(l));d->startValue=d->value;SetFocus(h);SetCapture(h);return 0;
    case WM_MOUSEMOVE:if(d->drag){int y=static_cast<short>(HIWORD(l));RECT r;GetClientRect(h,&r);int delta=d->fader?(d->startY-y)*127/std::max(1,int(r.bottom)-20):(d->startY-y);changed(h,*d,d->startValue+delta);}return 0;
    case WM_LBUTTONUP:d->drag=false;ReleaseCapture();return 0;
    case WM_CAPTURECHANGED:d->drag=false;return 0;
    case WM_MOUSEWHEEL:changed(h,*d,d->value+(GET_WHEEL_DELTA_WPARAM(w)>0?2:-2));return 0;
    case WM_KEYDOWN:if(w==VK_UP||w==VK_RIGHT)changed(h,*d,d->value+1);if(w==VK_DOWN||w==VK_LEFT)changed(h,*d,d->value-1);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps;auto dc=BeginPaint(h,&ps);RECT r;GetClientRect(h,&r);FillRect(dc,&r,surface());
        int width=r.right,height=r.bottom;const int id=GetDlgCtrlID(h);COLORREF accent=gold;
        if(id>=500 && id<518){int bus=(id-500)%3;accent=bus==1?green:(bus==2?red:gold);}
        if(id==514 || id==517)accent=gold;
        if(id==512 || id==513 || id==515 || id==516)accent=text;
        if(id>=700 && id<706)accent=(id<702?gold:(id<704?green:red));
        auto f=d->font;auto previous=SelectObject(dc,f?f:GetStockObject(DEFAULT_GUI_FONT));
        if(d->fader){
            int center=width/2;RECT groove{center-2,12,center+2,height-12};FillRect(dc,&groove,bg());
            auto pen=CreatePen(PS_SOLID,1,RGB(64,57,47));auto old=SelectObject(dc,pen);
            for(int j=0;j<=10;++j){int y=12+(height-24)*j/10;MoveToEx(dc,center+21,y,nullptr);LineTo(dc,center+(j%5==0?30:26),y);}SelectObject(dc,old);DeleteObject(pen);
            int y=height-14-(height-28)*d->value/127;RECT thumb{center-17,y-9,center+17,y+9};card(dc,thumb,text,text);
            pen=CreatePen(PS_SOLID,1,background);old=SelectObject(dc,pen);MoveToEx(dc,center-16,y,nullptr);LineTo(dc,center+16,y);SelectObject(dc,old);DeleteObject(pen);
        }else{
            Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            float side=float(std::min(width,height-17))-12, cx=width/2.0f,cy=(height-17)/2.0f;
            Gdiplus::Pen track(color(RGB(48,43,36)),6),valuePen(color(accent),6);valuePen.SetStartCap(Gdiplus::LineCapRound);valuePen.SetEndCap(Gdiplus::LineCapRound);
            g.DrawArc(&track,cx-side/2,cy-side/2,side,side,135,270);
            if(d->value)g.DrawArc(&valuePen,cx-side/2,cy-side/2,side,side,135,270.0f*d->value/127);
            Gdiplus::Pen inner(color(RGB(55,49,40)),1);g.DrawEllipse(&inner,cx-side*0.32f,cy-side*0.32f,side*0.64f,side*0.64f);
            double a=(135.0+270.0*d->value/127.0)*3.141592653589793/180.0;
            Gdiplus::Pen needle(color(d->value?accent:muted),3);needle.SetEndCap(Gdiplus::LineCapRound);
            g.DrawLine(&needle,cx+float(std::cos(a))*side*0.16f,cy+float(std::sin(a))*side*0.16f,cx+float(std::cos(a))*side*0.29f,cy+float(std::sin(a))*side*0.29f);
            wchar_t v[16];swprintf_s(v,L"%d",d->value);RECT label{0,height-16,width,height};SetBkMode(dc,TRANSPARENT);SetTextColor(dc,muted);DrawTextW(dc,v,-1,&label,DT_CENTER|DT_SINGLELINE);
        }
        SelectObject(dc,previous);
        if(GetFocus()==h){RECT focus=r;InflateRect(&focus,-1,-1);DrawFocusRect(dc,&focus);}EndPaint(h,&ps);return 0;}
    case WM_NCDESTROY:delete d;SetWindowLongPtrW(h,GWLP_USERDATA,0);break;
    }return DefWindowProcW(h,m,w,l);
}
inline void registerDial(HINSTANCE instance){WNDCLASSW c{};c.hInstance=instance;c.lpfnWndProc=dialProc;c.lpszClassName=L"DubDial";c.hCursor=LoadCursorW(nullptr,IDC_HAND);RegisterClassW(&c);}
inline void button(DRAWITEMSTRUCT* item){auto dc=item->hDC;auto r=item->rcItem;
    bool down=(item->itemState&ODS_SELECTED)!=0,on=GetPropW(item->hwndItem,L"active")!=nullptr;int id=GetDlgCtrlID(item->hwndItem);
    COLORREF highlight=(id==304||id==305||id==22)?red:gold;
    bool primary=id==10||id==801;COLORREF fill=on?highlight:(primary?text:panel);
    FillRect(dc,&r,surface());card(dc,r,fill,down?gold:(on?highlight:RGB(55,49,40)));
    auto f=reinterpret_cast<HFONT>(SendMessageW(item->hwndItem,WM_GETFONT,0,0));auto old=SelectObject(dc,f?f:GetStockObject(DEFAULT_GUI_FONT));
    wchar_t label[128];GetWindowTextW(item->hwndItem,label,128);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,(on||primary)?background:text);InflateRect(&r,-2,0);DrawTextW(dc,label,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);SelectObject(dc,old);
    if(item->itemState&ODS_FOCUS){InflateRect(&r,-3,-3);DrawFocusRect(dc,&r);}
}
}

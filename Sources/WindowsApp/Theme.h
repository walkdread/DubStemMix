#pragma once
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <cmath>
namespace theme {
constexpr COLORREF background=RGB(20,18,16),panel=RGB(32,29,25),text=RGB(235,225,203),muted=RGB(155,144,124);
constexpr COLORREF gold=RGB(219,166,52),green=RGB(125,155,83),red=RGB(203,83,52);
inline HBRUSH bg(){static HBRUSH b=CreateSolidBrush(background);return b;}
inline HBRUSH surface(){static HBRUSH b=CreateSolidBrush(panel);return b;}
struct Dial {int value=0;bool fader=false,drag=false;int startY=0,startValue=0;};
inline void changed(HWND h,Dial& d,int value){d.value=std::clamp(value,0,127);InvalidateRect(h,nullptr,FALSE);SendMessageW(GetParent(h),WM_HSCROLL,TB_THUMBTRACK,reinterpret_cast<LPARAM>(h));}
inline LRESULT CALLBACK dialProc(HWND h,UINT m,WPARAM w,LPARAM l){
    auto* d=reinterpret_cast<Dial*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(m==WM_NCCREATE){auto* cs=reinterpret_cast<CREATESTRUCTW*>(l);d=new Dial;d->fader=cs->lpCreateParams!=nullptr;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(d));return TRUE;}
    if(!d)return DefWindowProcW(h,m,w,l);
    switch(m){
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
        if(d->fader){RECT groove{width/2-2,9,width/2+2,height-9};FillRect(dc,&groove,bg());
            int y=height-10-(height-20)*d->value/127;RECT thumb{width/2-14,y-6,width/2+14,y+6};HBRUSH b=CreateSolidBrush(text);FillRect(dc,&thumb,b);DeleteObject(b);
        }else{int cx=width/2,cy=(height-14)/2,rad=std::min(width,height-14)/2-4;
            auto pen=CreatePen(PS_SOLID,5,RGB(55,49,40));auto old=SelectObject(dc,pen);auto oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));Ellipse(dc,cx-rad,cy-rad,cx+rad,cy+rad);SelectObject(dc,old);DeleteObject(pen);
            double a=(135.0+270.0*d->value/127.0)*3.141592653589793/180.0;
            pen=CreatePen(PS_SOLID,4,accent);old=SelectObject(dc,pen);MoveToEx(dc,cx+static_cast<int>(std::cos(a)*rad*0.35),cy+static_cast<int>(std::sin(a)*rad*0.35),nullptr);LineTo(dc,cx+static_cast<int>(std::cos(a)*rad*0.8),cy+static_cast<int>(std::sin(a)*rad*0.8));SelectObject(dc,old);DeleteObject(pen);SelectObject(dc,oldBrush);
            wchar_t v[16];swprintf_s(v,L"%d",d->value);RECT label{0,height-15,width,height};SetBkMode(dc,TRANSPARENT);SetTextColor(dc,muted);DrawTextW(dc,v,-1,&label,DT_CENTER|DT_SINGLELINE);
        }
        if(GetFocus()==h){RECT focus=r;InflateRect(&focus,-1,-1);DrawFocusRect(dc,&focus);}EndPaint(h,&ps);return 0;}
    case WM_NCDESTROY:delete d;SetWindowLongPtrW(h,GWLP_USERDATA,0);break;
    }return DefWindowProcW(h,m,w,l);
}
inline void registerDial(HINSTANCE instance){WNDCLASSW c{};c.hInstance=instance;c.lpfnWndProc=dialProc;c.lpszClassName=L"DubDial";c.hCursor=LoadCursorW(nullptr,IDC_HAND);RegisterClassW(&c);}
inline void button(DRAWITEMSTRUCT* item){auto dc=item->hDC;auto r=item->rcItem;FillRect(dc,&r,surface());
    bool down=(item->itemState&ODS_SELECTED)!=0;auto p=CreatePen(PS_SOLID,1,down?gold:RGB(74,65,49));auto old=SelectObject(dc,p);auto b=SelectObject(dc,GetStockObject(NULL_BRUSH));RoundRect(dc,r.left,r.top,r.right,r.bottom,6,6);SelectObject(dc,old);SelectObject(dc,b);DeleteObject(p);
    auto font=reinterpret_cast<HFONT>(SendMessageW(item->hwndItem,WM_GETFONT,0,0));auto oldFont=SelectObject(dc,font?font:GetStockObject(DEFAULT_GUI_FONT));
    wchar_t label[128];GetWindowTextW(item->hwndItem,label,128);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,down?gold:text);DrawTextW(dc,label,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(dc,oldFont);
    if(item->itemState&ODS_FOCUS){InflateRect(&r,-3,-3);DrawFocusRect(dc,&r);}
}
}

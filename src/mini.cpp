#include "mini.hpp"
#include "aviso.hpp"

#include <objidl.h>
#include <gdiplus.h>
#include <windowsx.h>

#include <algorithm>
#include <memory>
#include <thread>

namespace Mini {

namespace {

constexpr wchar_t kClass[] = L"RangaYTM.mini";
constexpr UINT kArtReady = WM_APP + 30;

HWND g_window = nullptr;
std::wstring g_options;
std::function<void(const char*)> g_action;
State g_state;
std::wstring g_artUrl;
std::shared_ptr<Gdiplus::Bitmap> g_art;
bool g_visible = false;
float g_scale = 1.0f;
int g_hover = -1; // botão sob o mouse: 0 anterior, 1 tocar/pausar, 2 próxima, 3 fechar, 4 abrir o app

HDC g_dc = nullptr;
HBITMAP g_bitmap = nullptr;

// Tudo em pixels de 1080p, vezes a escala do monitor.
constexpr float kWidth = 340, kHeight = 96;

struct Box {
    float x, y, w, h;
    bool Has(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

Box ButtonBox(int index) {
    const float s = g_scale;
    if (index == 3) return {(kWidth - 26) * s, 4 * s, 22 * s, 22 * s};
    if (index == 4) return {(kWidth - 50) * s, 4 * s, 22 * s, 22 * s};
    return {(94 + index * 40) * s, 52 * s, 32 * s, 32 * s};
}

void RoundRect(Gdiplus::GraphicsPath& path, float x, float y, float w, float h, float r) {
    path.AddArc(x, y, r * 2, r * 2, 180, 90);
    path.AddArc(x + w - r * 2, y, r * 2, r * 2, 270, 90);
    path.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0, 90);
    path.AddArc(x, y + h - r * 2, r * 2, r * 2, 90, 90);
    path.CloseFigure();
}

std::wstring Clock(int seconds) {
    wchar_t text[16];
    swprintf(text, 16, L"%d:%02d", seconds / 60, seconds % 60);
    return text;
}

void Redraw() {
    if (!g_visible || g_window == nullptr) return;
    const float s = g_scale;
    const int width = static_cast<int>(kWidth * s), height = static_cast<int>(kHeight * s);
    if (g_dc == nullptr) g_dc = CreateCompatibleDC(nullptr);
    if (g_bitmap != nullptr) DeleteObject(g_bitmap);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    g_bitmap = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    SelectObject(g_dc, g_bitmap);
    ZeroMemory(bits, static_cast<size_t>(width) * height * 4);
    {
        Gdiplus::Bitmap canvas(width, height, width * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
        Gdiplus::Graphics g(&canvas);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

        // O mesmo cartão do aviso, um pouco mais firme (aqui ele fica na tela).
        Gdiplus::GraphicsPath box;
        RoundRect(box, 0.5f, 0.5f, width - 1.0f, height - 1.0f, 14 * s);
        Gdiplus::SolidBrush back(Gdiplus::Color(225, 14, 14, 16));
        g.FillPath(&back, &box);
        Gdiplus::Pen edge(Gdiplus::Color(45, 255, 255, 255), 1.0f);
        g.DrawPath(&edge, &box);

        const float pad = 12 * s, side = 72 * s;
        Gdiplus::GraphicsPath cover;
        RoundRect(cover, pad, pad, side, side, 8 * s);
        if (g_art) {
            g.SetClip(&cover);
            g.DrawImage(g_art.get(), Gdiplus::RectF(pad, pad, side, side));
            g.ResetClip();
        } else {
            Gdiplus::SolidBrush grey(Gdiplus::Color(255, 45, 45, 48));
            g.FillPath(&grey, &cover);
        }

        Gdiplus::FontFamily family(L"Segoe UI");
        Gdiplus::Font big(&family, 14 * s, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font normal(&family, 12 * s, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Font tiny(&family, 10.5f * s, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::StringFormat line;
        line.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        line.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        Gdiplus::SolidBrush white(Gdiplus::Color(255, 255, 255, 255)), dim(Gdiplus::Color(255, 170, 170, 175)),
            faint(Gdiplus::Color(255, 120, 120, 125)), green(Gdiplus::Color(255, 61, 220, 132));
        const float textX = pad * 2 + side, textW = width - textX - 30 * s;
        const std::wstring title = g_state.title.empty() ? L"Nada tocando" : g_state.title;
        g.DrawString(title.c_str(), -1, &big, Gdiplus::RectF(textX, 10 * s, textW, 20 * s), &line, &white);
        g.DrawString(g_state.artist.c_str(), -1, &normal, Gdiplus::RectF(textX, 30 * s, textW, 18 * s), &line, &dim);

        // Os botões: anterior, tocar/pausar, próxima, desenhados (sem depender de fonte de ícones).
        for (int i = 0; i < 3; ++i) {
            const Box b = ButtonBox(i);
            if (g_hover == i) {
                Gdiplus::SolidBrush lit(Gdiplus::Color(50, 255, 255, 255));
                g.FillEllipse(&lit, b.x, b.y, b.w, b.h);
            }
            const float cx = b.x + b.w / 2, cy = b.y + b.h / 2, r = 7 * s;
            if (i == 1 && g_state.playing) {
                g.FillRectangle(&white, cx - r * 0.75f, cy - r, r * 0.5f, r * 2);
                g.FillRectangle(&white, cx + r * 0.25f, cy - r, r * 0.5f, r * 2);
            } else if (i == 1) {
                Gdiplus::PointF play[3] = {{cx - r * 0.6f, cy - r}, {cx - r * 0.6f, cy + r}, {cx + r, cy}};
                g.FillPolygon(&white, play, 3);
            } else {
                const float dir = i == 0 ? -1.0f : 1.0f, small = r * 0.8f;
                Gdiplus::PointF tri[3] = {{cx - dir * small * 0.7f, cy - small}, {cx - dir * small * 0.7f, cy + small}, {cx + dir * small * 0.6f, cy}};
                g.FillPolygon(&white, tri, 3);
                g.FillRectangle(&white, cx + dir * small * 0.6f - (dir > 0 ? 0 : 2.2f * s), cy - small, 2.2f * s, small * 2);
            }
        }
        // Abrir o app (um quadrado com seta), ao lado do fechar.
        {
            const Box b = ButtonBox(4);
            Gdiplus::Pen pen(g_hover == 4 ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 120, 120, 125), 1.5f * s);
            // Um quadrado aberto no canto de cima à direita, com a seta saindo por ele (o "abrir em janela").
            const float m = 6 * s, x0 = b.x + m, y0 = b.y + m, x1 = b.x + b.w - m, y1 = b.y + b.h - m;
            const float midX = (x0 + x1) / 2, midY = (y0 + y1) / 2, head = 4 * s;
            g.DrawLine(&pen, midX, y0, x0, y0);
            g.DrawLine(&pen, x0, y0, x0, y1);
            g.DrawLine(&pen, x0, y1, x1, y1);
            g.DrawLine(&pen, x1, y1, x1, midY);
            g.DrawLine(&pen, midX, midY, x1, y0);
            g.DrawLine(&pen, x1, y0, x1 - head, y0);
            g.DrawLine(&pen, x1, y0, x1, y0 + head);
        }
        // Fechar (×) no canto.
        {
            const Box b = ButtonBox(3);
            Gdiplus::Pen x(g_hover == 3 ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 120, 120, 125), 1.6f * s);
            const float m = 7 * s;
            g.DrawLine(&x, b.x + m, b.y + m, b.x + b.w - m, b.y + b.h - m);
            g.DrawLine(&x, b.x + b.w - m, b.y + m, b.x + m, b.y + b.h - m);
        }

        // O tempo e a linha de progresso.
        if (g_state.length > 0) {
            const std::wstring times = Clock(g_state.position) + L" / " + Clock(g_state.length);
            Gdiplus::StringFormat right;
            right.SetAlignment(Gdiplus::StringAlignmentFar);
            g.DrawString(times.c_str(), -1, &tiny, Gdiplus::RectF(textX, 60 * s, width - textX - pad, 16 * s), &right, &faint);
            const float barX = textX, barY = height - 7 * s, barW = width - textX - pad;
            Gdiplus::SolidBrush track(Gdiplus::Color(60, 255, 255, 255));
            g.FillRectangle(&track, barX, barY, barW, 2.5f * s);
            const float done = std::clamp(static_cast<float>(g_state.position) / g_state.length, 0.0f, 1.0f);
            g.FillRectangle(&green, barX, barY, barW * done, 2.5f * s);
        }

        if (const wchar_t* file = _wgetenv(L"RANGA_YTM_TESTE_MINI"); file != nullptr) {
            UINT count = 0, bytes = 0;
            Gdiplus::GetImageEncodersSize(&count, &bytes);
            std::unique_ptr<BYTE[]> buffer(new BYTE[bytes]);
            auto* list = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.get());
            Gdiplus::GetImageEncoders(count, bytes, list);
            for (UINT i = 0; i < count; ++i)
                if (wcscmp(list[i].MimeType, L"image/png") == 0) canvas.Save(file, &list[i].Clsid, nullptr);
        }
    }
    RECT at;
    GetWindowRect(g_window, &at);
    POINT origin{0, 0}, place{at.left, at.top};
    SIZE size{width, height};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(g_window, nullptr, &place, &size, g_dc, &origin, 0, &blend, ULW_ALPHA);
}

int HitButton(int x, int y) {
    for (int i = 0; i < 5; ++i) if (ButtonBox(i).Has(static_cast<float>(x), static_cast<float>(y))) return i;
    return -1;
}

void SavePlace() {
    RECT at;
    GetWindowRect(g_window, &at);
    WritePrivateProfileStringW(L"mini", L"x", std::to_wstring(at.left).c_str(), g_options.c_str());
    WritePrivateProfileStringW(L"mini", L"y", std::to_wstring(at.top).c_str(), g_options.c_str());
}

LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCHITTEST: {
        // Fora dos botões é a "barra de título": arrasta a janela.
        POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(window, &p);
        return HitButton(p.x, p.y) >= 0 ? HTCLIENT : HTCAPTION;
    }
    case WM_MOUSEMOVE: {
        const int hover = HitButton(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (hover != g_hover) {
            g_hover = hover;
            Redraw();
        }
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
        TrackMouseEvent(&track);
        return 0;
    }
    case WM_MOUSELEAVE:
        g_hover = -1;
        Redraw();
        return 0;
    case WM_LBUTTONUP: {
        const int hit = HitButton(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (hit == 3) {
            SetVisible(false);
            if (g_action) g_action("closed");
        } else if (hit == 4) {
            if (g_action) g_action("show");
        }
        else if (hit >= 0 && hit <= 2 && g_action) {
            g_action(hit == 0 ? "previous" : hit == 1 ? "playPause" : "next");
            // A resposta da página chega em até 1 s; o botão já troca na hora.
            if (hit == 1) {
                g_state.playing = !g_state.playing;
                Redraw();
            }
        }
        return 0;
    }
    case WM_EXITSIZEMOVE:
        SavePlace();
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case kArtReady: {
        std::unique_ptr<std::pair<std::wstring, std::shared_ptr<Gdiplus::Bitmap>>> art(
            reinterpret_cast<std::pair<std::wstring, std::shared_ptr<Gdiplus::Bitmap>>*>(lParam));
        if (art->first == g_artUrl) {
            g_art = art->second;
            Redraw();
        }
        return 0;
    }
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

}

void Start(HINSTANCE instance, const std::wstring& options, std::function<void(const char*)> action) {
    g_options = options;
    g_action = std::move(action);
    WNDCLASSEXW type{sizeof(type)};
    type.lpfnWndProc = Proc;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.lpszClassName = kClass;
    RegisterClassExW(&type);
    // Onde ficou da última vez; na primeira, embaixo à direita do monitor principal.
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &info);
    g_scale = std::clamp((info.rcMonitor.bottom - info.rcMonitor.top) / 1080.0f, 1.0f, 2.0f);
    const int width = static_cast<int>(kWidth * g_scale), height = static_cast<int>(kHeight * g_scale);
    int x = GetPrivateProfileIntW(L"mini", L"x", info.rcWork.right - width - 24, g_options.c_str());
    int y = GetPrivateProfileIntW(L"mini", L"y", info.rcWork.bottom - height - 24, g_options.c_str());
    // Se o monitor de antes sumiu, volta para o principal.
    const RECT wanted{x, y, x + width, y + height};
    if (MonitorFromRect(&wanted, MONITOR_DEFAULTTONULL) == nullptr) {
        x = info.rcWork.right - width - 24;
        y = info.rcWork.bottom - height - 24;
    }
    g_window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, L"Ranga YTM - mini", WS_POPUP, x, y, width,
                               height, nullptr, nullptr, instance, nullptr);
    if (GetPrivateProfileIntW(L"mini", L"aberto", 0, g_options.c_str()) != 0) SetVisible(true);
}

void Stop() {
    if (g_window != nullptr) DestroyWindow(g_window);
    if (g_bitmap != nullptr) DeleteObject(g_bitmap);
    if (g_dc != nullptr) DeleteDC(g_dc);
    g_art.reset();
}

bool Visible() { return g_visible; }

void SetVisible(bool on) {
    g_visible = on;
    WritePrivateProfileStringW(L"mini", L"aberto", on ? L"1" : L"0", g_options.c_str());
    if (on) {
        Redraw();
        ShowWindow(g_window, SW_SHOWNOACTIVATE);
        SetWindowPos(g_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        ShowWindow(g_window, SW_HIDE);
    }
}

void Update(const State& state) {
    const bool newArt = state.art != g_artUrl;
    g_state = state;
    if (newArt) {
        g_artUrl = state.art;
        g_art.reset();
        // A capa pela internet, fora da thread da janela.
        std::thread([url = state.art] {
            auto* art = new std::pair<std::wstring, std::shared_ptr<Gdiplus::Bitmap>>(url, Aviso::LoadArt(url));
            if (!PostMessageW(g_window, kArtReady, 0, reinterpret_cast<LPARAM>(art))) delete art;
        }).detach();
    }
    Redraw();
}

}

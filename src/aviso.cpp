#include "aviso.hpp"

#include <objidl.h>
#include <gdiplus.h>
#include <urlmon.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>

namespace Aviso {

namespace {

constexpr wchar_t kClass[] = L"RangaYTM.aviso";
constexpr UINT kReady = WM_APP + 20;
constexpr UINT_PTR kTimer = 1;
// Entra em 180 ms, fica 4 s, sai em 450 ms.
constexpr ULONGLONG kIn = 180, kHold = 4000, kOut = 450;

HWND g_window = nullptr;
ULONG_PTR g_gdiplus = 0;
std::wstring g_options;
bool g_enabled = true;
std::atomic<unsigned> g_generation{0};

HBITMAP g_bitmap = nullptr;
HDC g_dc = nullptr;
SIZE g_size{};
POINT g_at{};
ULONGLONG g_started = 0;

struct Ready {
    unsigned generation;
    std::wstring title, artist;
    std::unique_ptr<Gdiplus::Bitmap> art;
    HMONITOR monitor;
};

// A capa pela internet (thread própria): o que vier, ou nada.
std::unique_ptr<Gdiplus::Bitmap> Download(const std::wstring& url) {
    if (url.rfind(L"https://", 0) != 0) return nullptr;
    IStream* stream = nullptr;
    if (FAILED(URLOpenBlockingStreamW(nullptr, url.c_str(), &stream, 0, nullptr)) || stream == nullptr) return nullptr;
    std::unique_ptr<Gdiplus::Bitmap> image(Gdiplus::Bitmap::FromStream(stream));
    std::unique_ptr<Gdiplus::Bitmap> copy;
    // Uma cópia em memória: o GDI+ lê a imagem do stream aos poucos, e o stream é solto aqui.
    if (image && image->GetLastStatus() == Gdiplus::Ok && image->GetWidth() > 0) {
        copy.reset(new Gdiplus::Bitmap(image->GetWidth(), image->GetHeight(), PixelFormat32bppPARGB));
        Gdiplus::Graphics g(copy.get());
        g.DrawImage(image.get(), 0, 0, image->GetWidth(), image->GetHeight());
    }
    image.reset();
    stream->Release();
    return copy;
}

void RoundRect(Gdiplus::GraphicsPath& path, float x, float y, float w, float h, float r) {
    path.AddArc(x, y, r * 2, r * 2, 180, 90);
    path.AddArc(x + w - r * 2, y, r * 2, r * 2, 270, 90);
    path.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0, 90);
    path.AddArc(x, y + h - r * 2, r * 2, r * 2, 90, 90);
    path.CloseFigure();
}

bool PngEncoder(CLSID& clsid) {
    UINT count = 0, bytes = 0;
    Gdiplus::GetImageEncodersSize(&count, &bytes);
    if (bytes == 0) return false;
    std::unique_ptr<BYTE[]> buffer(new BYTE[bytes]);
    auto* list = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.get());
    Gdiplus::GetImageEncoders(count, bytes, list);
    for (UINT i = 0; i < count; ++i) {
        if (wcscmp(list[i].MimeType, L"image/png") == 0) {
            clsid = list[i].Clsid;
            return true;
        }
    }
    return false;
}

void Present(BYTE alpha) {
    BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
    POINT origin{0, 0};
    UpdateLayeredWindow(g_window, nullptr, &g_at, &g_size, g_dc, &origin, 0, &blend, ULW_ALPHA);
}

// Desenha a barrinha num bitmap com transparência, do tamanho certo para o monitor.
void Draw(const Ready& ready) {
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(ready.monitor, &info);
    const RECT& m = info.rcMonitor;
    // Tamanho pela altura do monitor: o mesmo jeito numa TV 4K e num monitor 1080p.
    const float s = std::clamp((m.bottom - m.top) / 1080.0f, 1.0f, 2.0f);
    const int width = static_cast<int>(360 * s), height = static_cast<int>(76 * s);
    g_size = SIZE{width, height};
    g_at = POINT{m.left + static_cast<int>(18 * s), m.top + static_cast<int>(18 * s)};

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

    Gdiplus::Bitmap canvas(width, height, width * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
    Gdiplus::Graphics g(&canvas);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

    // Fundo escuro meio transparente: discreto por cima do jogo.
    Gdiplus::GraphicsPath box;
    RoundRect(box, 0.5f, 0.5f, width - 1.0f, height - 1.0f, 14 * s);
    Gdiplus::SolidBrush back(Gdiplus::Color(185, 14, 14, 16));
    g.FillPath(&back, &box);
    Gdiplus::Pen edge(Gdiplus::Color(40, 255, 255, 255), 1.0f);
    g.DrawPath(&edge, &box);

    const float pad = 10 * s, side = height - pad * 2;
    if (ready.art) {
        Gdiplus::GraphicsPath clip;
        RoundRect(clip, pad, pad, side, side, 8 * s);
        g.SetClip(&clip);
        g.DrawImage(ready.art.get(), Gdiplus::RectF(pad, pad, side, side));
        g.ResetClip();
    } else {
        Gdiplus::GraphicsPath empty;
        RoundRect(empty, pad, pad, side, side, 8 * s);
        Gdiplus::SolidBrush grey(Gdiplus::Color(255, 45, 45, 48));
        g.FillPath(&grey, &empty);
    }

    const float textX = pad * 2 + side, textW = width - textX - pad;
    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font small(&family, 10.5f * s, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font big(&family, 15.5f * s, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font normal(&family, 13 * s, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::StringFormat line;
    line.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    line.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    Gdiplus::SolidBrush green(Gdiplus::Color(255, 61, 220, 132)), white(Gdiplus::Color(255, 255, 255, 255)), dim(Gdiplus::Color(255, 170, 170, 175));
    g.DrawString(L"TOCANDO AGORA", -1, &small, Gdiplus::RectF(textX, pad - 1 * s, textW, 16 * s), &line, &green);
    g.DrawString(ready.title.c_str(), -1, &big, Gdiplus::RectF(textX, pad + 14 * s, textW, 22 * s), &line, &white);
    g.DrawString(ready.artist.c_str(), -1, &normal, Gdiplus::RectF(textX, pad + 36 * s, textW, 20 * s), &line, &dim);

    // Teste: RANGA_YTM_TESTE_AVISO=<arquivo.png> guarda o desenho (o próprio bitmap, nunca a tela).
    if (const wchar_t* file = _wgetenv(L"RANGA_YTM_TESTE_AVISO"); file != nullptr) {
        CLSID png;
        if (PngEncoder(png)) canvas.Save(file, &png, nullptr);
    }
}

LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == kReady) {
        std::unique_ptr<Ready> ready(reinterpret_cast<Ready*>(lParam));
        // Uma música mais nova já passou na frente desta (troca rápida): fica a mais nova.
        if (ready->generation != g_generation) return 0;
        Draw(*ready);
        g_started = GetTickCount64();
        Present(0);
        SetWindowPos(window, HWND_TOPMOST, g_at.x, g_at.y, g_size.cx, g_size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetTimer(window, kTimer, 16, nullptr);
        return 0;
    }
    if (message == WM_TIMER && wParam == kTimer) {
        const ULONGLONG t = GetTickCount64() - g_started;
        double alpha;
        if (t < kIn) alpha = static_cast<double>(t) / kIn;
        else if (t < kIn + kHold) alpha = 1.0;
        else if (t < kIn + kHold + kOut) alpha = 1.0 - static_cast<double>(t - kIn - kHold) / kOut;
        else {
            KillTimer(window, kTimer);
            ShowWindow(window, SW_HIDE);
            return 0;
        }
        Present(static_cast<BYTE>(alpha * 255));
        // Continua por cima de jogo que reabriu a janela dele.
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return 0;
    }
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(window, message, wParam, lParam);
}

}

void Start(HINSTANCE instance, const std::wstring& options) {
    g_options = options;
    g_enabled = GetPrivateProfileIntW(L"aviso", L"ligado", 1, g_options.c_str()) != 0;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&g_gdiplus, &input, nullptr);
    WNDCLASSEXW type{sizeof(type)};
    type.lpfnWndProc = Proc;
    type.hInstance = instance;
    type.lpszClassName = kClass;
    RegisterClassExW(&type);
    // Camadas, por cima, fora da barra de tarefas, sem foco e sem clique (o clique passa para o jogo).
    g_window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE, kClass, L"", WS_POPUP, 0, 0,
                               1, 1, nullptr, nullptr, instance, nullptr);
}

void Stop() {
    if (g_window != nullptr) DestroyWindow(g_window);
    if (g_bitmap != nullptr) DeleteObject(g_bitmap);
    if (g_dc != nullptr) DeleteDC(g_dc);
    if (g_gdiplus != 0) Gdiplus::GdiplusShutdown(g_gdiplus);
}

bool Enabled() { return g_enabled; }

void SetEnabled(bool on) {
    g_enabled = on;
    WritePrivateProfileStringW(L"aviso", L"ligado", on ? L"1" : L"0", g_options.c_str());
}

void Show(HWND app, const std::wstring& title, const std::wstring& artist, const std::wstring& artUrl) {
    if (!g_enabled || g_window == nullptr) return;
    const HWND front = GetForegroundWindow();
    if (front == app && _wgetenv(L"RANGA_YTM_TESTE_AVISO") == nullptr) return;
    const unsigned generation = ++g_generation;
    // O monitor de quem está na frente: o do jogo.
    const HMONITOR monitor = MonitorFromWindow(front != nullptr ? front : app, MONITOR_DEFAULTTONEAREST);
    std::thread([generation, title, artist, artUrl, monitor] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        auto* ready = new Ready{generation, title, artist, Download(artUrl), monitor};
        CoUninitialize();
        if (!PostMessageW(g_window, kReady, 0, reinterpret_cast<LPARAM>(ready))) delete ready;
    }).detach();
}

}

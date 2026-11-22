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

// Desenha a barrinha num bitmap com transparência: uma linha só, capa pequena e "Música — Artista", do tamanho do
// texto (sem caixa sobrando) e na escala do monitor.
void Draw(const Ready& ready) {
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(ready.monitor, &info);
    const RECT& m = info.rcMonitor;
    // Tamanho pela altura do monitor: o mesmo jeito numa TV 4K e num monitor 1080p.
    const float s = std::clamp((m.bottom - m.top) / 1080.0f, 1.0f, 2.0f);
    const float height = 40 * s, pad = 6 * s, side = height - pad * 2, gap = 9 * s;

    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font bold(&family, 14 * s, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font normal(&family, 13 * s, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::StringFormat line(Gdiplus::StringFormat::GenericTypographic());
    line.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    line.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsMeasureTrailingSpaces);
    line.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    const std::wstring artist = ready.artist.empty() ? L"" : L"  —  " + ready.artist;

    // Quanto o texto ocupa, para a barrinha ter o tamanho dele (até um máximo; o resto vira "...").
    float titleW = 0, artistW = 0;
    {
        Gdiplus::Bitmap probe(1, 1, PixelFormat32bppPARGB);
        Gdiplus::Graphics measure(&probe);
        measure.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        Gdiplus::RectF box;
        measure.MeasureString(ready.title.c_str(), -1, &bold, Gdiplus::PointF(0, 0), &line, &box);
        titleW = box.Width + 2 * s;
        if (!artist.empty()) {
            measure.MeasureString(artist.c_str(), -1, &normal, Gdiplus::PointF(0, 0), &line, &box);
            artistW = box.Width + 2 * s;
        }
    }
    const float textMax = 400 * s;
    if (titleW + artistW > textMax) {
        // Corta primeiro o artista (até 35% do espaço), depois o título.
        artistW = std::min(artistW, textMax * 0.35f);
        titleW = std::min(titleW, textMax - artistW);
    }
    const float textX = pad + side + gap;
    const int width = static_cast<int>(textX + titleW + artistW + height / 2), heightPx = static_cast<int>(height);
    g_size = SIZE{width, heightPx};
    g_at = POINT{m.left + static_cast<int>(16 * s), m.top + static_cast<int>(16 * s)};

    if (g_dc == nullptr) g_dc = CreateCompatibleDC(nullptr);
    if (g_bitmap != nullptr) DeleteObject(g_bitmap);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -heightPx;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    g_bitmap = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    SelectObject(g_dc, g_bitmap);
    ZeroMemory(bits, static_cast<size_t>(width) * heightPx * 4);

    Gdiplus::Bitmap canvas(width, heightPx, width * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
    Gdiplus::Graphics g(&canvas);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

    // Uma pílula escura e bem transparente: aparece sem tampar o jogo.
    Gdiplus::GraphicsPath box;
    RoundRect(box, 0.5f, 0.5f, width - 1.0f, height - 1.0f, height / 2 - 0.5f);
    Gdiplus::SolidBrush back(Gdiplus::Color(115, 10, 10, 12));
    g.FillPath(&back, &box);
    Gdiplus::Pen edge(Gdiplus::Color(22, 255, 255, 255), 1.0f);
    g.DrawPath(&edge, &box);

    Gdiplus::GraphicsPath cover;
    RoundRect(cover, pad, pad, side, side, side / 2);
    if (ready.art) {
        g.SetClip(&cover);
        g.DrawImage(ready.art.get(), Gdiplus::RectF(pad, pad, side, side));
        g.ResetClip();
    } else {
        Gdiplus::SolidBrush grey(Gdiplus::Color(200, 45, 45, 48));
        g.FillPath(&grey, &cover);
    }

    Gdiplus::SolidBrush white(Gdiplus::Color(245, 255, 255, 255)), dim(Gdiplus::Color(215, 185, 185, 190));
    g.DrawString(ready.title.c_str(), -1, &bold, Gdiplus::RectF(textX, 0, titleW, height), &line, &white);
    if (!artist.empty()) g.DrawString(artist.c_str(), -1, &normal, Gdiplus::RectF(textX + titleW, 0, artistW, height), &line, &dim);

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

// Ranga YTM: o YouTube Music numa janela própria, leve, com o controle e a bandeja.
//
// Não é um navegador embutido: quem desenha a página é o WebView2, o motor do Edge que já vem no Windows, então o
// programa tem poucos MB. O login é o do próprio site (a pessoa entra na conta do Google na página; o programa não vê
// senha) e fica guardado em %APPDATA%\RangaYTM. App não oficial: não pula propaganda nem baixa música.
//
// Feito para abrir pela loja Apps do Ranga EMU (Store.hpp lá):
//   --ranga-emu           B sem ter para onde voltar fecha e devolve ao EMU
//   --tela-cheia          já abre em tela cheia
//   --escala=N            a página N vezes maior (1 a 2), para ler de longe
//   --monitor=x,y,l,a     o monitor do EMU, em pixels do Windows
//
// O controle vem da ranga_pad.dll (ranga_pad.c, o mesmo SDL do Ranga EMU e do Ranga TV) e só conta com a janela na
// frente. Direcional anda, A (Cross) aperta, B (Círculo) volta, Y (Triângulo) toca/pausa, LB/RB música anterior e
// próxima, X (Quadrado) busca, Select abre a tela do que está tocando, Start tela cheia.

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00 // Windows 10: SetProcessDpiAwarenessContext
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include "WebView2.h"

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace {

// ---- COM: um handler do WebView2 a partir de uma função ----
template <typename Interface, typename... Args>
class Handler final : public Interface {
public:
    explicit Handler(std::function<HRESULT(Args...)> fn) : fn_(std::move(fn)) {}
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --refs_;
        if (left == 0) delete this;
        return left;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** out) override {
        // O WebView2 só pergunta pela interface do próprio handler.
        *out = this;
        AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override { return fn_(args...); }

private:
    std::atomic<ULONG> refs_{1};
    std::function<HRESULT(Args...)> fn_;
};

template <typename Interface, typename... Args> Interface* Make(std::type_identity_t<std::function<HRESULT(Args...)>> fn) {
    return new Handler<Interface, Args...>(std::move(fn));
}

// O IID que o WebView2.h só declara (o MSVC o tira do __uuidof; aqui vai escrito, do MIDL_INTERFACE de lá).
constexpr IID kController2 = {0xc979903e, 0xd4ca, 0x4228, {0x92, 0xeb, 0x47, 0xee, 0x3f, 0xa9, 0x6e, 0xab}};

constexpr UINT kTrayMessage = WM_APP + 1, kPadMessage = WM_APP + 2;
constexpr UINT kTrayShow = 1, kTrayPlay = 2, kTrayNext = 3, kTrayPrevious = 4, kTrayQuit = 5;
constexpr wchar_t kClass[] = L"RangaYTM";
constexpr wchar_t kHome[] = L"https://music.youtube.com/";

HWND g_window = nullptr;
HMENU g_trayMenu = nullptr;
NOTIFYICONDATAW g_tray{};
ICoreWebView2Controller* g_controller = nullptr;
ICoreWebView2* g_web = nullptr;
std::wstring g_script;

bool g_fromEmu = false, g_fullscreen = false;
double g_scale = 1.0;
RECT g_monitor{};
bool g_hasMonitor = false;
WINDOWPLACEMENT g_beforeFullscreen{sizeof(WINDOWPLACEMENT)};
std::atomic<bool> g_running{true};

std::wstring Folder() {
    // Teste: RANGA_YTM_DADOS troca a pasta (a conta de verdade fica fora).
    if (const wchar_t* chosen = _wgetenv(L"RANGA_YTM_DADOS"); chosen != nullptr && *chosen != 0) {
        CreateDirectoryW(chosen, nullptr);
        return chosen;
    }
    wchar_t* appData = nullptr;
    std::wstring folder;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) folder = std::wstring(appData) + L"\\RangaYTM";
    CoTaskMemFree(appData);
    CreateDirectoryW(folder.c_str(), nullptr);
    return folder;
}

std::wstring ExeFolder() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring folder = path;
    return folder.substr(0, folder.find_last_of(L'\\'));
}

// O script do controle (controle.js), guardado no próprio .exe.
std::wstring LoadScript() {
    HRSRC found = FindResourceW(nullptr, L"CONTROLE", MAKEINTRESOURCEW(10));
    if (found == nullptr) return L"";
    HGLOBAL data = LoadResource(nullptr, found);
    const char* bytes = static_cast<const char*>(LockResource(data));
    const int size = static_cast<int>(SizeofResource(nullptr, found));
    std::wstring text(MultiByteToWideChar(CP_UTF8, 0, bytes, size, nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes, size, text.data(), static_cast<int>(text.size()));
    return text;
}

void Run(const wchar_t* js) {
    if (g_web != nullptr) g_web->ExecuteScript(js, nullptr);
}

// Só o YouTube Music e o login do Google ficam na janela; o resto abre no navegador.
bool Allowed(const std::wstring& url) {
    const size_t start = url.find(L"://");
    if (start == std::wstring::npos) return url.rfind(L"about:", 0) == 0 || url.rfind(L"data:", 0) == 0;
    std::wstring host = url.substr(start + 3);
    host = host.substr(0, host.find_first_of(L"/:?#"));
    for (wchar_t& c : host) c = static_cast<wchar_t>(towlower(c));
    const auto ends = [&](const std::wstring& tail) { return host == tail || (host.size() > tail.size() && host.compare(host.size() - tail.size(), tail.size(), tail) == 0 && host[host.size() - tail.size() - 1] == L'.'); };
    if (ends(L"youtube.com") || ends(L"google.com") || ends(L"gstatic.com") || ends(L"googleusercontent.com") || ends(L"youtube-nocookie.com")) return true;
    // O login do Google às vezes passa pelo domínio do país (accounts.google.com.br).
    return host.rfind(L"accounts.google.", 0) == 0;
}

void Resize() {
    if (g_controller == nullptr) return;
    RECT area;
    GetClientRect(g_window, &area);
    g_controller->put_Bounds(area);
}

void SetFullscreen(bool on) {
    const LONG style = GetWindowLongW(g_window, GWL_STYLE);
    if (on && (style & WS_OVERLAPPEDWINDOW)) {
        MONITORINFO info{sizeof(info)};
        GetWindowPlacement(g_window, &g_beforeFullscreen);
        GetMonitorInfoW(MonitorFromWindow(g_window, MONITOR_DEFAULTTONEAREST), &info);
        SetWindowLongW(g_window, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
        const RECT& r = info.rcMonitor;
        SetWindowPos(g_window, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else if (!on && !(style & WS_OVERLAPPEDWINDOW)) {
        SetWindowLongW(g_window, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(g_window, &g_beforeFullscreen);
        SetWindowPos(g_window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    g_fullscreen = on;
}

void BringToFront() {
    ShowWindow(g_window, IsIconic(g_window) ? SW_RESTORE : SW_SHOW);
    // O Windows só deixa vir para a frente quem foi aberto pelo programa da frente (o Ranga EMU libera isso); o
    // segundo caminho, juntar-se por um instante à thread da janela da frente, cobre o resto.
    const HWND front = GetForegroundWindow();
    const DWORD frontThread = GetWindowThreadProcessId(front, nullptr), me = GetCurrentThreadId();
    const bool attached = frontThread != me && AttachThreadInput(me, frontThread, TRUE);
    SetForegroundWindow(g_window);
    BringWindowToTop(g_window);
    if (attached) AttachThreadInput(me, frontThread, FALSE);
}

// ---- O controle ----
// Botões da ranga_pad.dll (bits de ranga_pad_poll): 0 cima, 1 baixo, 2 esquerda, 3 direita, 4 A, 5 B, 6 X, 7 Y,
// 8 Start, 9 Select, 10 LB, 11 RB; 30 há controle.
void PadLoop() {
    HMODULE dll = LoadLibraryW((ExeFolder() + L"\\ranga_pad.dll").c_str());
    if (dll == nullptr) return;
    using Poll = int (*)();
    const auto poll = reinterpret_cast<Poll>(reinterpret_cast<void*>(GetProcAddress(dll, "ranga_pad_poll")));
    if (poll == nullptr) return;
    int held = 0;
    ULONGLONG repeatAt[4] = {};
    while (g_running) {
        const int bits = poll();
        if (bits < 0) break;
        const ULONGLONG now = GetTickCount64();
        for (int button = 0; button < 12; ++button) {
            const int mask = 1 << button;
            const bool down = bits & mask, wasDown = held & mask;
            bool fire = down && !wasDown;
            // O direcional segurado repete.
            if (button < 4) {
                if (fire) repeatAt[button] = now + 380;
                else if (down && now >= repeatAt[button]) {
                    fire = true;
                    repeatAt[button] = now + 110;
                }
            }
            // Só com a janela na frente: atrás de outra, o controle é dos outros programas.
            if (fire && GetForegroundWindow() == g_window) PostMessageW(g_window, kPadMessage, button, 0);
        }
        held = bits;
        Sleep(16);
    }
}

void OnPad(int button) {
    switch (button) {
    case 0: Run(L"window.__ranga&&__ranga.move('cima')"); break;
    case 1: Run(L"window.__ranga&&__ranga.move('baixo')"); break;
    case 2: Run(L"window.__ranga&&__ranga.move('esquerda')"); break;
    case 3: Run(L"window.__ranga&&__ranga.move('direita')"); break;
    case 4: Run(L"window.__ranga&&__ranga.accept()"); break;
    case 5: {
        if (g_web == nullptr) break;
        // Primeiro a página: com o teclado na tela aberto, B só o fecha.
        g_web->ExecuteScript(L"!!(window.__ranga&&__ranga.back())",
                             Make<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>([](HRESULT, LPCWSTR result) -> HRESULT {
                                 if (result != nullptr && std::wstring(result) == L"true") return S_OK;
                                 BOOL back = FALSE;
                                 g_web->get_CanGoBack(&back);
                                 if (back) g_web->GoBack();
                                 else if (g_fullscreen && !g_fromEmu) SetFullscreen(false);
                                 else if (g_fromEmu) PostMessageW(g_window, WM_CLOSE, 0, 0);
                                 return S_OK;
                             }));
        break;
    }
    case 6: Run(L"window.__ranga&&__ranga.search()"); break;
    case 7: Run(L"window.__ranga&&__ranga.playPause()"); break;
    case 8: SetFullscreen(!g_fullscreen); break;
    case 9: Run(L"window.__ranga&&__ranga.togglePlayer()"); break;
    case 10: Run(L"window.__ranga&&__ranga.previous()"); break;
    case 11: Run(L"window.__ranga&&__ranga.next()"); break;
    default: break;
    }
}

// ---- Teste sem controle na mão ----
// RANGA_YTM_TESTE="espera,baixo,a,foto,..." aperta os botões (um a cada 1,5 s depois de 10 s) pelo mesmo caminho dos de
// verdade, sem precisar da janela na frente; "foto" guarda a página (CapturePreview do WebView2: só o que ela
// desenha, nunca a tela) em RANGA_YTM_TESTE_FOTO-N.png.
constexpr UINT kTestPhoto = 100;

void TestPhoto(int number) {
    const wchar_t* base = _wgetenv(L"RANGA_YTM_TESTE_FOTO");
    if (base == nullptr || g_web == nullptr) return;
    const std::wstring path = std::wstring(base) + L"-" + std::to_wstring(number) + L".png";
    IStream* stream = nullptr;
    if (FAILED(SHCreateStreamOnFileEx(path.c_str(), STGM_CREATE | STGM_WRITE, FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, &stream))) return;
    g_web->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream,
                          Make<ICoreWebView2CapturePreviewCompletedHandler, HRESULT>([stream](HRESULT) -> HRESULT {
                              stream->Release();
                              return S_OK;
                          }));
}

void TestScript() {
    const wchar_t* script = _wgetenv(L"RANGA_YTM_TESTE");
    if (script == nullptr) return;
    std::thread([text = std::wstring(script)] {
        Sleep(10000);
        const std::wstring names[] = {L"cima", L"baixo", L"esquerda", L"direita", L"a", L"b", L"x", L"y", L"start", L"select", L"lb", L"rb"};
        int photos = 0;
        size_t start = 0;
        while (start <= text.size() && g_running) {
            size_t end = text.find(L',', start);
            if (end == std::wstring::npos) end = text.size();
            const std::wstring step = text.substr(start, end - start);
            start = end + 1;
            Sleep(1500);
            if (step == L"foto") PostMessageW(g_window, kPadMessage, kTestPhoto, ++photos);
            for (int b = 0; b < 12; ++b) if (step == names[b]) PostMessageW(g_window, kPadMessage, b, 0);
        }
    }).detach();
}

// ---- A bandeja ----
void AddTray(HICON icon) {
    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = g_window;
    g_tray.uID = 1;
    g_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_tray.uCallbackMessage = kTrayMessage;
    g_tray.hIcon = icon;
    lstrcpynW(g_tray.szTip, L"Ranga YTM", ARRAYSIZE(g_tray.szTip));
    Shell_NotifyIconW(NIM_ADD, &g_tray);
    g_trayMenu = CreatePopupMenu();
    AppendMenuW(g_trayMenu, MF_STRING, kTrayShow, L"Mostrar");
    AppendMenuW(g_trayMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(g_trayMenu, MF_STRING, kTrayPlay, L"Tocar / pausar");
    AppendMenuW(g_trayMenu, MF_STRING, kTrayNext, L"Próxima");
    AppendMenuW(g_trayMenu, MF_STRING, kTrayPrevious, L"Anterior");
    AppendMenuW(g_trayMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(g_trayMenu, MF_STRING, kTrayQuit, L"Sair");
}

void SetTitle(const std::wstring& title) {
    // A página já diz o que está tocando no título ("Música • Artista - YouTube Music").
    std::wstring shown = title;
    const std::wstring tail = L" - YouTube Music";
    if (shown.size() > tail.size() && shown.compare(shown.size() - tail.size(), tail.size(), tail) == 0) shown.resize(shown.size() - tail.size());
    if (shown.empty() || shown == L"YouTube Music") shown = L"Ranga YTM";
    else shown += L" — Ranga YTM";
    SetWindowTextW(g_window, shown.c_str());
    lstrcpynW(g_tray.szTip, shown.c_str(), ARRAYSIZE(g_tray.szTip));
    g_tray.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
}

// ---- O WebView2 ----
void StartWebView() {
    // Teclas de mídia do teclado e o painel de mídia do Windows (tocar, pausar, próxima), que o WebView2 deixa
    // desligados se ninguém pedir.
    SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", L"--enable-features=HardwareMediaKeyHandling,MediaSessionService");
    HMODULE loader = LoadLibraryW((ExeFolder() + L"\\WebView2Loader.dll").c_str());
    using Create = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    const auto create = loader == nullptr ? nullptr : reinterpret_cast<Create>(reinterpret_cast<void*>(GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions")));
    if (create == nullptr) {
        MessageBoxW(g_window, L"Falta o WebView2Loader.dll ao lado do Ranga YTM. Baixe o app de novo pela loja do Ranga.", L"Ranga YTM", MB_ICONERROR);
        PostQuitMessage(1);
        return;
    }
    const std::wstring data = Folder() + L"\\perfil";
    const HRESULT started = create(nullptr, data.c_str(), nullptr, Make<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>([](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
        if (FAILED(result) || environment == nullptr) {
            MessageBoxW(g_window, L"O WebView2 do Windows não abriu. Ele vem com o Windows 10 e 11; se faltar, instale o \"Microsoft Edge WebView2 Runtime\".", L"Ranga YTM", MB_ICONERROR);
            PostQuitMessage(1);
            return S_OK;
        }
        environment->CreateCoreWebView2Controller(g_window, Make<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>([](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
            if (FAILED(result) || controller == nullptr) return S_OK;
            g_controller = controller;
            g_controller->AddRef();
            g_controller->get_CoreWebView2(&g_web);
            Resize();
            if (g_scale > 1.0) g_controller->put_ZoomFactor(g_scale);
            // Fundo escuro enquanto a página não pinta (sem o clarão branco ao abrir).
            ICoreWebView2Controller2* controller2 = nullptr;
            if (SUCCEEDED(g_controller->QueryInterface(kController2, reinterpret_cast<void**>(&controller2))) && controller2 != nullptr) {
                controller2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{255, 3, 3, 3});
                controller2->Release();
            }
            ICoreWebView2Settings* settings = nullptr;
            if (SUCCEEDED(g_web->get_Settings(&settings)) && settings != nullptr) {
                settings->put_AreDevToolsEnabled(FALSE);
                settings->put_IsStatusBarEnabled(FALSE);
                settings->Release();
            }
            if (!g_script.empty()) g_web->AddScriptToExecuteOnDocumentCreated(g_script.c_str(), nullptr);
            EventRegistrationToken token;
            g_web->add_DocumentTitleChanged(Make<ICoreWebView2DocumentTitleChangedEventHandler, ICoreWebView2*, IUnknown*>([](ICoreWebView2* web, IUnknown*) -> HRESULT {
                LPWSTR title = nullptr;
                if (SUCCEEDED(web->get_DocumentTitle(&title)) && title != nullptr) SetTitle(title);
                CoTaskMemFree(title);
                return S_OK;
            }), &token);
            g_web->add_NavigationStarting(Make<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>([](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                LPWSTR uri = nullptr;
                args->get_Uri(&uri);
                const std::wstring url = uri != nullptr ? uri : L"";
                CoTaskMemFree(uri);
                if (!Allowed(url)) {
                    args->put_Cancel(TRUE);
                    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
                return S_OK;
            }), &token);
            // Janela nova (link com target=_blank): o YouTube Music na mesma janela, o resto no navegador.
            g_web->add_NewWindowRequested(Make<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>([](ICoreWebView2* web, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                LPWSTR uri = nullptr;
                args->get_Uri(&uri);
                const std::wstring url = uri != nullptr ? uri : L"";
                CoTaskMemFree(uri);
                args->put_Handled(TRUE);
                if (Allowed(url)) web->Navigate(url.c_str());
                else ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                return S_OK;
            }), &token);
            g_web->Navigate(kHome);
            return S_OK;
        }));
        return S_OK;
    }));
    if (FAILED(started)) {
        MessageBoxW(g_window, L"O WebView2 do Windows não abriu.", L"Ranga YTM", MB_ICONERROR);
        PostQuitMessage(1);
    }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_SIZE:
        Resize();
        return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = POINT{480, 360};
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_F11) SetFullscreen(!g_fullscreen);
        return 0;
    case kPadMessage:
        if (wParam == kTestPhoto) TestPhoto(static_cast<int>(lParam));
        else OnPad(static_cast<int>(wParam));
        return 0;
    case kTrayMessage:
        if (LOWORD(lParam) == WM_LBUTTONUP) BringToFront();
        else if (LOWORD(lParam) == WM_RBUTTONUP) {
            POINT at;
            GetCursorPos(&at);
            SetForegroundWindow(window);
            const UINT chosen = TrackPopupMenu(g_trayMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, at.x, at.y, 0, window, nullptr);
            if (chosen == kTrayShow) BringToFront();
            else if (chosen == kTrayPlay) Run(L"window.__ranga&&__ranga.playPause()");
            else if (chosen == kTrayNext) Run(L"window.__ranga&&__ranga.next()");
            else if (chosen == kTrayPrevious) Run(L"window.__ranga&&__ranga.previous()");
            else if (chosen == kTrayQuit) DestroyWindow(window);
        }
        return 0;
    case WM_DESTROY:
        g_running = false;
        Shell_NotifyIconW(NIM_DELETE, &g_tray);
        if (g_controller != nullptr) g_controller->Close();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

void ReadArgs() {
    int count = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; i < count; ++i) {
        const std::wstring arg = args[i];
        if (arg == L"--ranga-emu") g_fromEmu = true;
        else if (arg == L"--tela-cheia") g_fullscreen = true;
        else if (arg.rfind(L"--escala=", 0) == 0) {
            std::wstring value = arg.substr(9);
            for (wchar_t& c : value) if (c == L',') c = L'.';
            const double scale = _wtof(value.c_str());
            if (scale >= 1.0 && scale <= 2.0) g_scale = scale;
        } else if (arg.rfind(L"--monitor=", 0) == 0) {
            int v[4] = {};
            if (swscanf(arg.c_str() + 10, L"%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3]) == 4 && v[2] > 0 && v[3] > 0) {
                g_monitor = RECT{v[0], v[1], v[0] + v[2], v[1] + v[3]};
                g_hasMonitor = true;
            }
        }
    }
    LocalFree(args);
}

}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    // Uma janela só: abrir de novo traz a que já existe.
    // (Com a pasta de teste, RANGA_YTM_DADOS, a trava é outra: o teste roda ao lado do app de verdade.)
    HANDLE single = CreateMutexW(nullptr, TRUE, _wgetenv(L"RANGA_YTM_DADOS") != nullptr ? L"RangaYTM.teste" : L"RangaYTM.unica");
    if (single != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kClass, nullptr)) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        return 0;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ReadArgs();
    g_script = LoadScript();

    HICON icon = LoadIconW(instance, L"IDI_APP");
    WNDCLASSEXW type{sizeof(type)};
    type.lpfnWndProc = WindowProc;
    type.hInstance = instance;
    type.hIcon = icon;
    type.hIconSm = icon;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = CreateSolidBrush(RGB(3, 3, 3));
    type.lpszClassName = kClass;
    RegisterClassExW(&type);

    // 1280x800 no meio do monitor do EMU (ou do principal).
    RECT area = g_monitor;
    if (!g_hasMonitor) {
        MONITORINFO info{sizeof(info)};
        GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &info);
        area = info.rcWork;
    }
    const int width = std::min<int>(1280, area.right - area.left), height = std::min<int>(800, area.bottom - area.top);
    g_window = CreateWindowExW(0, kClass, L"Ranga YTM", WS_OVERLAPPEDWINDOW, area.left + (area.right - area.left - width) / 2,
                               area.top + (area.bottom - area.top - height) / 2, width, height, nullptr, nullptr, instance, nullptr);
    if (g_fullscreen) {
        g_fullscreen = false;
        SetFullscreen(true);
    }
    ShowWindow(g_window, show);
    UpdateWindow(g_window);
    if (g_fromEmu) BringToFront();
    AddTray(icon);
    StartWebView();
    std::thread pad(PadLoop);
    TestScript();

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    g_running = false;
    pad.join();
    if (g_web != nullptr) g_web->Release();
    if (g_controller != nullptr) g_controller->Release();
    CoUninitialize();
    return 0;
}

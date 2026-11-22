#include "idioma.hpp"

#include <windows.h>

#include <cwchar>

namespace Idioma {
namespace {

struct Entry {
    const wchar_t* text[Count]; // pt, en, ru, es
};

// Português primeiro: é a chave.
constexpr Entry kTable[] = {
    {{L"Mostrar", L"Show", L"Показать", L"Mostrar"}},
    {{L"Mini player", L"Mini player", L"Мини-плеер", L"Minirreproductor"}},
    {{L"Só o mini player (esconde a janela)\tCtrl+M", L"Mini player only (hides the window)\tCtrl+M", L"Только мини-плеер (скрыть окно)\tCtrl+M",
      L"Solo el minirreproductor (oculta la ventana)\tCtrl+M"}},
    {{L"Tocar / pausar", L"Play / pause", L"Играть / пауза", L"Reproducir / pausar"}},
    {{L"Próxima", L"Next", L"Следующий трек", L"Siguiente"}},
    {{L"Anterior", L"Previous", L"Предыдущий трек", L"Anterior"}},
    {{L"Mostrar a música no canto (jogos)", L"Show the song in the corner (games)", L"Показывать трек в углу экрана (игры)",
      L"Mostrar la canción en la esquina (juegos)"}},
    {{L"Aviso compacto (uma linha)", L"Compact notice (one line)", L"Компактное уведомление (одна строка)", L"Aviso compacto (una línea)"}},
    {{L"Idioma", L"Language", L"Язык", L"Idioma"}},
    {{L"Sair", L"Quit", L"Выход", L"Salir"}},
    {{L"Nada tocando", L"Nothing playing", L"Ничего не играет", L"No suena nada"}},
    {{L"Falta o WebView2Loader.dll ao lado do %ls. Baixe o app de novo pela loja do Ranga.",
      L"WebView2Loader.dll is missing next to %ls. Download the app again from the Ranga store.",
      L"Рядом с %ls нет файла WebView2Loader.dll. Скачайте приложение заново в магазине Ranga.",
      L"Falta WebView2Loader.dll junto a %ls. Vuelve a descargar la app desde la tienda de Ranga."}},
    {{L"O WebView2 do Windows não abriu. Ele vem com o Windows 10 e 11; se faltar, instale o \"Microsoft Edge WebView2 Runtime\".",
      L"Windows WebView2 did not start. It comes with Windows 10 and 11; if it is missing, install the \"Microsoft Edge WebView2 Runtime\".",
      L"Не удалось запустить WebView2. Он входит в Windows 10 и 11; если его нет, установите \"Microsoft Edge WebView2 Runtime\".",
      L"El WebView2 de Windows no se abrió. Viene con Windows 10 y 11; si falta, instala \"Microsoft Edge WebView2 Runtime\"."}},
    {{L"O WebView2 do Windows não abriu.", L"Windows WebView2 did not start.", L"Не удалось запустить WebView2.", L"El WebView2 de Windows no se abrió."}},
};

constexpr const wchar_t* kCodes[Count] = {L"pt", L"en", L"ru", L"es"};
constexpr const wchar_t* kNames[Count] = {L"Português", L"English", L"Русский", L"Español"};
constexpr const wchar_t* kTags[Count] = {L"pt-BR", L"en-US", L"ru-RU", L"es-ES"};

Lang g_lang = Pt;
std::wstring g_options;

bool FromCode(const std::wstring& code, Lang& out) {
    for (int i = 0; i < Count; ++i) {
        if (_wcsnicmp(code.c_str(), kCodes[i], 2) == 0) {
            out = static_cast<Lang>(i);
            return true;
        }
    }
    return false;
}

Lang FromWindows() {
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
    case LANG_PORTUGUESE: return Pt;
    case LANG_RUSSIAN: return Ru;
    case LANG_SPANISH: return Es;
    default: return En;
    }
}

}

void Init(const std::wstring& options, const std::wstring& forced) {
    g_options = options;
    Lang chosen;
    if (!forced.empty() && FromCode(forced, chosen)) {
        g_lang = chosen;
        return;
    }
    wchar_t saved[16] = {};
    GetPrivateProfileStringW(L"geral", L"idioma", L"", saved, 16, options.c_str());
    g_lang = FromCode(saved, chosen) ? chosen : FromWindows();
}

Lang Current() { return g_lang; }

void Set(Lang lang) {
    if (lang < 0 || lang >= Count) return;
    g_lang = lang;
    if (!g_options.empty()) WritePrivateProfileStringW(L"geral", L"idioma", kCodes[lang], g_options.c_str());
}

const wchar_t* T(const wchar_t* pt) {
    if (g_lang == Pt || pt == nullptr) return pt;
    for (const Entry& entry : kTable)
        if (wcscmp(entry.text[Pt], pt) == 0) return entry.text[g_lang];
    return pt;
}

const wchar_t* Code(Lang lang) { return kCodes[lang]; }
const wchar_t* Name(Lang lang) { return kNames[lang]; }
const wchar_t* BrowserTag(Lang lang) { return kTags[lang]; }

}

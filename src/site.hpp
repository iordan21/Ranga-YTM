#pragma once

// Um código, dois apps: o que muda entre o Ranga YTM (YouTube Music) e o Ranga Spot (Spotify). O compilar.ps1 gera os
// dois; o Ranga Spot sai com -DRANGA_SPOT. Tudo o mais (controle, teclado na tela, aviso no canto, mini player,
// bandeja) é o mesmo programa.

#include <string>

namespace Site {

#ifdef RANGA_SPOT
// O Spotify não deixa a marca dele no nome de app de outros; "Spot" lembra sem usar a marca.
inline constexpr wchar_t kName[] = L"Ranga Spot";
inline constexpr wchar_t kId[] = L"RangaSpot"; // classe da janela, pasta em %APPDATA%, travas
inline constexpr wchar_t kHome[] = L"https://open.spotify.com/";
inline constexpr wchar_t kOrigin[] = L"https://open.spotify.com/"; // só ela fala com o app
inline constexpr wchar_t kTitleTail[] = L" | Spotify"; // o título da página ("Música • Artista | Spotify")
inline constexpr wchar_t kPageName[] = L"Spotify";
inline constexpr char kScriptSite[] = "spot";
#else
inline constexpr wchar_t kName[] = L"Ranga YTM";
inline constexpr wchar_t kId[] = L"RangaYTM";
inline constexpr wchar_t kHome[] = L"https://music.youtube.com/";
inline constexpr wchar_t kOrigin[] = L"https://music.youtube.com/";
inline constexpr wchar_t kTitleTail[] = L" - YouTube Music";
inline constexpr wchar_t kPageName[] = L"YouTube Music";
inline constexpr char kScriptSite[] = "ytm";
#endif

// O que fica na janela: o site e o login; o resto abre no navegador.
inline bool Allowed(const std::wstring& host) {
    const auto ends = [&](const std::wstring& tail) {
        return host == tail || (host.size() > tail.size() && host.compare(host.size() - tail.size(), tail.size(), tail) == 0 && host[host.size() - tail.size() - 1] == L'.');
    };
    // O login pelo Google serve aos dois (no Spotify é "Continuar com o Google"); o domínio do país também.
    if (ends(L"google.com") || ends(L"gstatic.com") || ends(L"googleusercontent.com") || host.rfind(L"accounts.google.", 0) == 0) return true;
#ifdef RANGA_SPOT
    return ends(L"spotify.com") || ends(L"scdn.co") || ends(L"spotifycdn.com") || ends(L"spotifycdn.net") || ends(L"spotify.net") ||
           ends(L"facebook.com") || ends(L"appleid.apple.com") || ends(L"apple.com");
#else
    return ends(L"youtube.com") || ends(L"youtube-nocookie.com");
#endif
}

}

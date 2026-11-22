// Idiomas do Ranga YTM / Ranga Spot: português, inglês, russo e espanhol.
//
// Só o que é do app (bandeja, mini player, avisos, teclado na tela); a página do YouTube Music / Spotify vem no idioma
// que o próprio site escolhe (o da conta ou o pedido ao abrir).
//
// Quem escolhe, nesta ordem: --idioma=pt|en|ru|es (o Ranga EMU passa o dele), o salvo em opcoes.ini [geral] idioma
// (o menu da bandeja) e, sem nenhum dos dois, o idioma do Windows (fora desses quatro, inglês).
#pragma once

#include <string>

namespace Idioma {

enum Lang { Pt = 0, En, Ru, Es, Count };

void Init(const std::wstring& options, const std::wstring& forced);
Lang Current();
void Set(Lang lang); // salva em opcoes.ini

// O texto em português (a chave) no idioma atual; sem tradução, o próprio português.
const wchar_t* T(const wchar_t* pt);

const wchar_t* Code(Lang lang);       // "pt", "en", "ru", "es"
const wchar_t* Name(Lang lang);       // "Português", "English", "Русский", "Español"
const wchar_t* BrowserTag(Lang lang); // para o --lang do WebView2: "pt-BR", "en-US", "ru-RU", "es-ES"

}

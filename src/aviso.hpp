#pragma once

// O aviso "tocando agora": quando a música muda, uma barrinha no canto superior esquerdo do monitor em que se está
// (o do jogo), com a capa, o nome e o artista, que entra e some sozinha em uns 4 segundos. Por cima de jogo em janela
// ou em tela cheia sem borda (o modo do Ranga EMU e da maioria dos emuladores); em tela cheia exclusiva o Windows não
// deixa janela nenhuma aparecer por cima. Não pega clique nem foco: o controle e o teclado continuam no jogo.

#include <windows.h>

#include <string>

namespace Aviso {

// Uma vez, no começo. options: o .ini onde fica ligado/desligado.
void Start(HINSTANCE instance, const std::wstring& options);
void Stop();

bool Enabled();
void SetEnabled(bool on);
// Compacto: uma linha só ("pilula"); senão o cartão com capa grande (o padrão).
bool Compact();
void SetCompact(bool on);

// A música mudou (thread da janela). app: a janela do Ranga YTM; com ela na frente o aviso não aparece (a pessoa
// já está vendo a música).
void Show(HWND app, const std::wstring& title, const std::wstring& artist, const std::wstring& artUrl);

}

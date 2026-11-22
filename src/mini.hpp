#pragma once

// O mini player: uma janelinha sempre por cima com a capa, o nome, o artista, os botões (anterior, tocar/pausar,
// próxima) e uma linha de progresso. Arrasta-se pelo fundo, lembra onde ficou (opcoes.ini [mini]) e não toma o foco
// ao clicar (o jogo continua com o teclado e o controle). Abre e fecha pela bandeja.

#include <windows.h>

#include <functional>
#include <string>

namespace Mini {

struct State {
    std::wstring title, artist, art;
    bool playing = false;
    int position = 0, length = 0; // segundos
};

// Uma vez, no começo. action: o que o botão apertado pede ("previous", "playPause", "next").
void Start(HINSTANCE instance, const std::wstring& options, std::function<void(const char*)> action);
void Stop();

bool Visible();
void SetVisible(bool on);

// O estado novo da página (thread da janela).
void Update(const State& state);
// Desenha de novo (o idioma mudou).
void Refresh();

}

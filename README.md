# Ranga YTM

O YouTube Music numa janela própria, leve, com controle (DualSense, Xbox) e ícone na bandeja. Abre pela loja **Apps** do [Ranga EMU](https://github.com/iordan21/Ranga-EMU).

> App **não oficial**. YouTube Music é marca do Google; este app não é do Google. Ele mostra o próprio site (music.youtube.com) e não pula propaganda nem baixa música.

## Como é feito

- C++ com o **WebView2** (o motor do Edge que já vem no Windows 10 e 11): o programa tem uns 500 KB e não leva navegador junto.
- O login é o do próprio site: você entra na sua conta do Google na página, e ele fica guardado em `%APPDATA%\RangaYTM`. O programa não vê senha.
- Só o YouTube Music e o login do Google ficam na janela; qualquer outro link abre no seu navegador.

## Controle

| Botão | Faz |
|---|---|
| Direcional / analógico | anda pelos cartões, músicas e botões |
| A (Cross) | abre / toca o que está marcado |
| B (Círculo) | volta (aberto pelo Ranga EMU, sem ter para onde voltar, fecha e devolve ao EMU) |
| Y (Triângulo) | toca / pausa |
| LB / RB | música anterior / próxima |
| X (Quadrado) | busca |
| Select | abre a tela do que está tocando |
| Start | tela cheia |

Teclado: F11 tela cheia. Teclas de mídia do teclado e o painel de mídia do Windows também funcionam.

## Compilar

`powershell -ExecutionPolicy Bypass -File compilar.ps1` gera `build\` (RangaYTM.exe, ranga_pad.dll, WebView2Loader.dll). Precisa:

- o MinGW e o SDL 2 estático do Ranga EMU (caminhos no começo do `compilar.ps1`);
- o kit do WebView2 em `deps\webview2-pkg`: o pacote `Microsoft.Web.WebView2` do nuget.org, descompactado (confira a assinatura com `dotnet nuget verify`).

## Argumentos (os que o Ranga EMU passa)

`--ranga-emu`, `--tela-cheia`, `--escala=N` (1 a 2), `--monitor=x,y,largura,altura`.

# Ranga YTM e Ranga Spot

Dois apps do mesmo código: o **Ranga YTM** (YouTube Music) e o **Ranga Spot** (para ouvir o Spotify). O que muda entre eles está em `src/site.hpp`; o `compilar.ps1` gera os dois (`RangaYTM.exe` e `RangaSpot.exe`).

> Apps **não oficiais**. YouTube Music é marca do Google e Spotify é marca da Spotify AB; nenhum dos dois apps é deles. Eles mostram o próprio site, não pulam propaganda nem baixam música.

## Ranga YTM

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

## Aviso "tocando agora"

Quando a música muda e a janela do Ranga YTM não está na frente (você está num jogo), aparece por uns 4 segundos uma barrinha discreta no canto superior esquerdo do monitor do jogo, com a capa, o nome e o artista. Não pega clique nem foco. Aparece por cima de jogo em janela ou tela cheia sem borda (o modo do Ranga EMU); em tela cheia exclusiva o Windows não deixa. Liga e desliga no ícone da bandeja.

## Mini player

Uma janelinha sempre por cima com a capa, o nome, o artista, anterior / tocar-pausar / próxima e o tempo. Abre e fecha
pelo ícone da bandeja (Mini player), arrasta-se pelo fundo e lembra onde ficou. Clicar nela não tira o foco do jogo.
Com ela aberta, o aviso do canto não aparece (a música já está à vista).

**Só o mini player:** Ctrl+M (ou "Só o mini player" na bandeja) esconde a janela principal e deixa só o mini player.
O X da janela faz o mesmo: a música continua no mini player. O botão ao lado do × no mini player traz a janela de volta;
o × do mini player (com a janela escondida) ou "Sair" na bandeja fecham o app de vez.

## Compilar

`powershell -ExecutionPolicy Bypass -File compilar.ps1` gera `build\` (RangaYTM.exe, ranga_pad.dll, WebView2Loader.dll). Precisa:

- o MinGW e o SDL 2 estático do Ranga EMU (caminhos no começo do `compilar.ps1`);
- o kit do WebView2 em `deps\webview2-pkg`: o pacote `Microsoft.Web.WebView2` do nuget.org, descompactado (confira a assinatura com `dotnet nuget verify`).

## Argumentos (os que o Ranga EMU passa)

`--ranga-emu`, `--tela-cheia`, `--escala=N` (1 a 2), `--monitor=x,y,largura,altura`.

## Licença

MIT (veja [LICENSE](LICENSE)). Vale para o código daqui: o SDL 2 e o WebView2 têm as suas próprias licenças.

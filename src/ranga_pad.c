/*
 * ranga_pad.dll — o controle (DualSense, Xbox e afins) para o Ranga TV no PC.
 *
 * O Compose Desktop não lê controle nenhum. Em vez de uma biblioteca Java de
 * joystick (as que existem ou só falam XInput, que não enxerga o DualSense, ou
 * estão paradas há anos), o Ranga TV usa o mesmo SDL 2 que o Ranga EMU usa para
 * navegar com o controle: mesma lista de controles conhecidos, mesmo DualSense
 * por HIDAPI. Esta DLL é só a ponte: uma função que devolve os botões
 * apertados agora, num número, para o Kotlin ler pela JNA (player/Gamepad.kt).
 *
 * Compilar: native/compilar.ps1 (usa o SDL estático já compilado pelo Ranga EMU).
 */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdlib.h>
#include <windows.h>

#define RANGA_PAD_MAX 8

static int started = 0;
static SDL_GameController* pads[RANGA_PAD_MAX];
static int count = 0;

static void close_all(void) {
    for (int i = 0; i < count; ++i) SDL_GameControllerClose(pads[i]);
    count = 0;
}

static void open_all(void) {
    close_all();
    for (int i = 0; i < SDL_NumJoysticks() && count < RANGA_PAD_MAX; ++i) {
        if (!SDL_IsGameController(i)) continue;
        SDL_GameController* pad = SDL_GameControllerOpen(i);
        if (pad != NULL) pads[count++] = pad;
    }
}

static int start(void) {
    SDL_SetMainReady();
    // A janela do Ranga TV não é do SDL: sem esta dica ele só lê o controle
    // quando uma janela dele está em primeiro plano, isto é, nunca.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    // Sem janela do SDL ninguém processa as mensagens do Windows nesta thread,
    // e é por elas que chegam os botões da entrada bruta (RawInput): o
    // controle aparecia ligado e nenhum botão respondia. Com esta dica o SDL
    // cuida delas numa thread própria; ranga_pad_poll ainda esvazia a fila.
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
    // Como no Ranga EMU: procurar controle pelo DirectInput custa uns 300 ms e
    // só controle genérico antigo precisa dele; entra só se a primeira busca
    // não achar nada.
    const int directInputChosen = getenv("SDL_DIRECTINPUT_ENABLED") != NULL;
    if (!directInputChosen) SDL_SetHint("SDL_DIRECTINPUT_ENABLED", "0");
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) return 0;
    if (!directInputChosen && SDL_NumJoysticks() == 0) {
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
        SDL_SetHint("SDL_DIRECTINPUT_ENABLED", "1");
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) return 0;
    }
    open_all();
    return 1;
}

/*
 * Os botões apertados agora, um bit cada (o mesmo número em Gamepad.kt):
 * 0 cima, 1 baixo, 2 esquerda, 3 direita (direcional ou analógico esquerdo),
 * 4 A/Cruz, 5 B/Círculo, 6 X/Quadrado, 7 Y/Triângulo, 8 Start/Options,
 * 9 Back/Create, 10 LB/L1, 11 RB/R1; 30: há algum controle ligado.
 * -1: o SDL não abriu.
 */
__declspec(dllexport) int ranga_pad_poll(void) {
    if (!started) {
        if (!start()) return -1;
        started = 1;
    }
    // As mensagens do Windows desta thread (a janela escondida que o SDL cria
    // para saber de controle ligado e desligado).
    MSG message;
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    SDL_Event event;
    int changed = 0;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_CONTROLLERDEVICEADDED || event.type == SDL_CONTROLLERDEVICEREMOVED) changed = 1;
    }
    if (changed) open_all();
    SDL_GameControllerUpdate();

    int bits = 0;
    for (int i = 0; i < count; ++i) {
        SDL_GameController* pad = pads[i];
        if (!SDL_GameControllerGetAttached(pad)) continue;
        bits |= 1 << 30;
        const int x = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        const int y = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP) || y < -20000) bits |= 1 << 0;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || y > 20000) bits |= 1 << 1;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || x < -20000) bits |= 1 << 2;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || x > 20000) bits |= 1 << 3;
        // Pela posição, como no Ranga EMU: Cruz no DualSense, A no Xbox.
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A)) bits |= 1 << 4;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B)) bits |= 1 << 5;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X)) bits |= 1 << 6;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_Y)) bits |= 1 << 7;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START)) bits |= 1 << 8;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK)) bits |= 1 << 9;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) bits |= 1 << 10;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) bits |= 1 << 11;
    }
    return bits;
}

/*
 * Para diagnóstico: o que o SDL está vendo (aparelhos, nomes, caminho e quantos
 * botões cada um tem apertados agora, contados direto no joystick).
 */
__declspec(dllexport) int ranga_pad_info(char* out, int size) {
    if (!started || out == NULL || size <= 0) return 0;
    int used = SDL_snprintf(out, size, "joysticks=%d abertos=%d", SDL_NumJoysticks(), count);
    for (int i = 0; i < count && used < size; ++i) {
        SDL_Joystick* stick = SDL_GameControllerGetJoystick(pads[i]);
        int down = 0;
        for (int b = 0; b < SDL_JoystickNumButtons(stick); ++b) down += SDL_JoystickGetButton(stick, b);
        used += SDL_snprintf(out + used, size - used, " | %s (%s) tipo=%d botoes_apertados=%d",
                             SDL_GameControllerName(pads[i]) ? SDL_GameControllerName(pads[i]) : "?",
                             SDL_GameControllerPath(pads[i]) ? SDL_GameControllerPath(pads[i]) : "?",
                             (int)SDL_GameControllerGetType(pads[i]), down);
    }
    return used;
}

/* Solta os controles (o Ranga TV fechando). */
__declspec(dllexport) void ranga_pad_close(void) {
    if (!started) return;
    close_all();
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    started = 0;
}

# Compila o Ranga YTM em build\ (RangaYTM.exe, ranga_pad.dll e o WebView2Loader.dll ao lado).
# Usa o MinGW e o SDL 2 estatico que o Ranga EMU ja compila (C:\projetos\sos-ps5) e o kit do WebView2 em
# deps\webview2-pkg (pacote Microsoft.Web.WebView2 do nuget.org, assinatura da Microsoft conferida).
# -Saida <pasta>: compila em outro lugar (com o RangaYTM.exe de build\ aberto, ele nao pode ser trocado).
param([string]$Saida = '')
$ErrorActionPreference = 'Stop'
$aqui = $PSScriptRoot
$env:PATH = "C:\projetos\sos-ps5\toolchain\mingw64\bin;$env:PATH"
$sdl = 'C:\projetos\sos-ps5\SoS-PS5\build\3rdparty\SDL2'
$wv = "$aqui\deps\webview2-pkg\build\native"
$out = if ($Saida) { $Saida } else { "$aqui\build" }
New-Item -ItemType Directory -Force $out | Out-Null

# Dois programas do mesmo codigo (src\site.hpp): RangaYTM.exe e, com -DRANGA_SPOT, RangaSpot.exe.
foreach ($app in @(@{ Exe = 'RangaYTM'; Def = @() }, @{ Exe = 'RangaSpot'; Def = @('-DRANGA_SPOT') })) {
    & windres @($app.Def) "$aqui\src\app.rc" -O coff -o "$out\$($app.Exe).res" --include-dir "$aqui\src"
    if ($LASTEXITCODE -ne 0) { throw "windres falhou ($($app.Exe))" }
    & g++ -std=c++20 -O2 -municode -mwindows -DUNICODE -D_UNICODE @($app.Def) "-I$wv\include" "$aqui\src\main.cpp" "$aqui\src\aviso.cpp" "$aqui\src\mini.cpp" "$out\$($app.Exe).res" `
        -o "$out\$($app.Exe).exe" -lole32 -lshell32 -luser32 -lgdi32 -luuid -lshlwapi -lgdiplus -lurlmon -static -static-libgcc -static-libstdc++ -s
    if ($LASTEXITCODE -ne 0) { throw "g++ falhou ($($app.Exe))" }
}

& gcc -O2 -shared -o "$out\ranga_pad.dll" "$aqui\src\ranga_pad.c" "-I$sdl\include\SDL2" "-I$sdl\include-config-release\SDL2" "-L$sdl" -lSDL2 `
    -lm -lkernel32 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lversion -luuid -ladvapi32 -lsetupapi -lshell32 -ldinput8 -lcfgmgr32 `
    -static -static-libgcc -s
if ($LASTEXITCODE -ne 0) { throw "gcc (ranga_pad) falhou" }

Copy-Item "$wv\x64\WebView2Loader.dll" "$out\WebView2Loader.dll" -Force
Get-ChildItem $out -File | Where-Object { $_.Extension -ne '.res' } | ForEach-Object { '{0,-22} {1,8:N0} KB' -f $_.Name, ($_.Length / 1KB) }

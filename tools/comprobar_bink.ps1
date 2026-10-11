# Compiles and runs the native Bink self-check (app/src/cod2_video_bink_prueba.cpp) on the host.
#
#   powershell -ExecutionPolicy Bypass -File tools\comprobar_bink.ps1
#   powershell -ExecutionPolicy Bypass -File tools\comprobar_bink.ps1 -Solo legal.bik
#
# It compiles app/src/cod2_video_bink.cpp with COD2_BINK_SIN_SDK, so only the container parser and the FFmpeg decoder
# are built - no runtime, no game image, no NRO - and then runs it over every `.bik` the game ships, parsing each
# file's header and frame index and decoding its first two frames with FFmpeg.
#
# This is the check that has to pass before any of it is hooked into the game. Everything it covers is silent when it
# is wrong: a wrong header offset yields a plausible number, a frame index read from the wrong place yields plausible
# frames, and a decoder opened without its version or its extradata opens without complaint and then returns nothing.
#
# The table below is what the files actually are, read from their own headers, and the point of writing it down is
# that a change to the parser has to keep agreeing with it. It also covers both branches of the audio table: nine
# files with one or two tracks and one with none.
#
# The .bik files are game data and are not in the repository; they come from the disc, under -Juegos.
param(
    [string]$Juegos = 'assets/game_root',
    [string]$Solo = '',
    [string]$Clang = 'C:\Program Files\LLVM\bin\clang++.exe',
    [string]$Salida = 'app/out/bink/prueba_bink.exe'
)
$ErrorActionPreference = 'Stop'
$raiz = (Split-Path -Parent $PSScriptRoot)
$ffmpeg = "$raiz/sdk/thirdparty/FFmpeg"
$libs = "$raiz/sdk/out/win-amd64"

# fichero, ancho, alto, fotogramas, pistas de audio
$tabla = @(
    @('IW_logo.bik', 640, 480, 174, 1),
    @('atvi.bik', 1280, 720, 451, 1),
    @('legal.bik', 1280, 720, 150, 0),
    @('cod2_intro.bik', 1280, 720, 3937, 1),
    @('cod2_attract.bik', 1280, 720, 3937, 2),
    @('movie_africa.bik', 1280, 720, 1971, 1),
    @('movie_eastern.bik', 1280, 720, 1588, 1),
    @('movie_europe.bik', 1280, 720, 3136, 1),
    @('movie_victory.bik', 1280, 720, 1564, 1)
)
if ($Solo) { $tabla = $tabla | Where-Object { $_[0] -eq $Solo } }
if (-not $tabla) { throw "no hay ningun fichero que probar en $Juegos" }

foreach ($p in @($Clang, "$ffmpeg/libavcodec/avcodec.h", "$libs/libavcodec.lib", "$libs/libavutil.lib")) {
    if (-not (Test-Path $p)) { throw "falta $p" }
}
New-Item -ItemType Directory -Force (Split-Path -Parent "$raiz/$Salida") | Out-Null

# bcrypt comes from the Windows SDK, which is not on the path unless vcvars has run; rather than pull the whole
# BuildTools environment in, its directory is named directly.
$sdkWindows = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\Lib' -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $sdkWindows) { throw 'no encuentro el SDK de Windows' }
$bcrypt = "$($sdkWindows.FullName)\um\x64\bcrypt.lib"
if (-not (Test-Path $bcrypt)) { throw "falta $bcrypt" }

# -I has to be absolute: the vendored FFmpeg is built with HAVE_AV_CONFIG_H and its headers include each other by
# path from the tree root, so a relative include breaks as soon as the working directory is not the tree root.
#
# -o is given a native path on purpose: with forward slashes clang names the debug file after the first path
# component and drops an `a.pdb` in the working directory.
$exe = Join-Path $raiz $Salida
Write-Output 'compilando...'
& $Clang '-std=c++23' '-O1' '-g' '-DCOD2_BINK_SIN_SDK' `
    "-I$ffmpeg" `
    "$raiz/app/src/cod2_video_bink.cpp" `
    "$raiz/app/src/cod2_video_bink_prueba.cpp" `
    "$libs/libavcodec.lib" "$libs/libavutil.lib" $bcrypt `
    '-o' $exe
if ($LASTEXITCODE) { throw "clang++ fallo con $LASTEXITCODE" }

$fallos = 0
foreach ($f in $tabla) {
    $ruta = "$raiz/$Juegos/$($f[0])"
    if (-not (Test-Path $ruta)) { Write-Output "SALTADO  $($f[0]): no esta en $Juegos"; ++$fallos; continue }
    & $exe $ruta $f[1] $f[2] $f[3] $f[4]
    if ($LASTEXITCODE) { Write-Output "FALLO    $($f[0]) (exit $LASTEXITCODE)"; ++$fallos }
}
if ($fallos) { throw "$fallos fichero(s) no pasaron la comprobacion" }
Write-Output 'OK'

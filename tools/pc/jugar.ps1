# cod2 - runs the PC build from outside the repository, which is the only way it gets a normal token.
#
# WHY IT EXISTS
# A process started from a binary that lives inside this repository runs with a restricted,
# low-integrity token. Measured with tools/pc/quien_soy.cpp:
#
#   ...\cod2-nx\tools\pc\quien_soy.exe        integridad S-1-16-4096 (baja)  crear -> ec=5
#   C:\Users\igor2\juego_cod2\quien_soy.exe   integridad S-1-16-8192 (media) crear -> ec=0
#
# The same bytes, the same shell, the same command line: only the location of the executable
# changes. Two symptoms follow from the low token, and both look like game bugs:
#
#   * Saves fail. XamContentCreate cannot create its folder and the runtime used to die with
#     "[FATAL] Dispatch thread: deferred completion threw 'create_directories: Access is denied.'"
#   * The gamepad is not detected at all. SDL enumerates 0 joysticks, so the port never receives
#     SDL_EVENT_GAMEPAD_ADDED. Logged as "SDL enumerates 0 joystick(s), 0 gamepad(s) at startup."
#
# Running the same binaries from a folder outside the repository fixes both. This script does that
# copy and launches from there, pointing --game_data_root back at the repository's extracted game.
#
# Usage: powershell -ExecutionPolicy Bypass -File tools\pc\jugar.ps1
#        powershell -ExecutionPolicy Bypass -File tools\pc\jugar.ps1 -Destino D:\cod2
#        powershell -ExecutionPolicy Bypass -File tools\pc\jugar.ps1 -Esperar     # waits, then says how it ended
#        powershell -ExecutionPolicy Bypass -File tools\pc\jugar.ps1 -Extra --log-level=debug
#        powershell -ExecutionPolicy Bypass -File tools\pc\jugar.ps1 -Partida     # drives the menus into a mission
#        powershell -ExecutionPolicy Bypass -File tools\pc\jugar.ps1 -Ventana 1280x720
#
# The window is never fullscreen: see the --fullscreen=false note below.
#
# -Partida. The three-dimensional scene is only on screen inside a mission, and getting there by hand means
# sitting through 150 seconds of intro movies and then pressing the right buttons. This does it: waits for the
# intro, then Start, then A twice (main menu -> Single Player -> the first mission). It needs --mnk_mode=true,
# which is added for you: this game reads the Xbox 360 controller, not the keyboard, so without the
# keyboard-to-controller emulation nothing the script sends is seen at all - the game looks frozen on the menu.
# The keys are the SDK's own bindings (keybind_start = "X,Return", keybind_a = "Semicolon,Space").
param(
  [string]$Destino = (Join-Path $env:LOCALAPPDATA 'cod2-nx\pc'),
  [string]$GameDataRoot = '',
  [switch]$SinCopiar,
  [switch]$Esperar,
  [switch]$Partida,
  [string]$Ventana = '',
  [string[]]$Extra = @()
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$origen = Join-Path $repo 'app\out\pc'

if (-not $GameDataRoot) { $GameDataRoot = Join-Path $repo 'assets\game_root' }
if (-not (Test-Path -LiteralPath (Join-Path $GameDataRoot 'default.xex'))) {
  throw "no hay juego extraido en '$GameDataRoot' (falta default.xex); usa -GameDataRoot"
}
if (-not (Test-Path -LiteralPath (Join-Path $origen 'cod2.exe'))) {
  throw "no hay build en '$origen'; construye antes con cmake --build app/out/pc --target cod2"
}

New-Item -ItemType Directory -Path $Destino -Force | Out-Null
$copiados = 0
foreach ($f in 'cod2.exe', 'rexruntime.dll', 'rexgpu-xenos.dll') {
  $de = Join-Path $origen $f
  $a = Join-Path $Destino $f
  if ($SinCopiar -and (Test-Path -LiteralPath $a)) { continue }
  $igual = (Test-Path -LiteralPath $a) -and
           ((Get-Item -LiteralPath $a).Length -eq (Get-Item -LiteralPath $de).Length) -and
           ((Get-Item -LiteralPath $a).LastWriteTimeUtc -ge (Get-Item -LiteralPath $de).LastWriteTimeUtc)
  if (-not $igual) {
    Copy-Item -LiteralPath $de -Destination $a -Force
    $copiados++
  }
}
Write-Output "runtime en $Destino ($copiados fichero(s) copiado(s))"

# The saves go next to the runtime, outside the repository, so they are readable and writable and
# easy to find. --store_shaders=false keeps the shader cache from growing a second copy of the data.
#
# --fullscreen=false. The SDK's window starts fullscreen (cvar `fullscreen`, UI/Window), which takes over the
# desktop: the game covers everything, the mouse is captured and nothing else can be looked at while a 4-minute
# test runs. In a window it is just another window - the desktop stays usable and the window can be photographed
# or left alone. The guest video mode is unchanged (1280x720); -Ventana only resizes the frame around it.
#
# An extra argument is quoted when it contains a space. Start-Process joins the list with spaces and
# adds no quoting of its own, so '--cl=+set ui_nextMission 2' would arrive at the runtime as two
# arguments and the cvar would be left with '+set'. The guest command line (--cl) is exactly the kind
# of argument that needs one.
$args = @(
  "--game_data_root=$GameDataRoot",
  "--user_data_root=$(Join-Path $Destino 'datos')",
  '--store_shaders=false',
  '--fullscreen=false'
)
if ($Ventana) {
  $args += "--window_width=$($Ventana.Split('x')[0])", "--window_height=$($Ventana.Split('x')[1])"
}
if ($Partida) { $args += '--mnk_mode=true' }
# `powershell -File script.ps1 -Extra a b c` binds only `a` to an array parameter and silently drops the rest:
# a run asked for three extra cvars arrived with one, and the missing `--cod2_captura_cada_s` was only noticed
# because no PNGs appeared. A single comma-separated value survives -File, so both spellings are accepted.
if ($Extra.Count -eq 1 -and $Extra[0] -match ',') { $Extra = $Extra[0] -split ',' }
$args += ($Extra | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } })
$p = Start-Process -FilePath (Join-Path $Destino 'cod2.exe') -ArgumentList $args `
  -WorkingDirectory $Destino -PassThru
Write-Output "cod2.exe pid $($p.Id)"
Write-Output "registro: $(Join-Path $Destino 'logs')"

if ($Partida) {
  $pulsar = Join-Path $PSScriptRoot 'pulsar.ps1'
  $logs = Join-Path $Destino 'logs'
  # Whether the three-dimensional scene is being drawn, read from the renderer's own per-frame report.
  # In the intro movies, the menu and the loading screens the scene target has a handful of draws or none;
  # in a mission it has hundreds. The report is printed every ten seconds (the interval of the C reports),
  # so it is only read after waiting longer than that.
  function EnEscena {
    $f = Get-ChildItem $logs -Filter 'cod2_*.log' -ErrorAction SilentlyContinue |
         Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $f) { return $false }
    $l = Select-String -Path $f.FullName -Pattern 'C6 area abierta por fotograma' | Select-Object -Last 1
    if (-not $l) { return $false }
    return ($l.Line -match 'escena [\d.]+ Mtexels en [\d.]+ pases \((\d+) dibujos' -and [int]$Matches[1] -ge 30)
  }
  # Why this is a loop and not a fixed wait. The intro movies do not play in real time in this build:
  # legal.bik (150 frames at 29.97 fps, so 5 s of video) opens at 15.83 and atvi.bik (451 frames at
  # 59.94, 7.5 s) opens at 17.57 - 1.7 s later, about three times too fast - but cod2_intro.bik is 3937
  # frames and takes its full two minutes. A fixed 158 s wait therefore either measured the menu for its
  # whole length or missed the mission. Now the scene is looked for in the log and the sequence is repeated
  # until it appears.
  #
  # The sequence is deliberately only two keys. A longer one (adding a second RETURN and a second SPACE per
  # round) walked into the attract-mode demo instead: the main menu's entries are reached by RETURN, and
  # SPACE then confirms the next screen, so a third press lands on whatever is highlighted after the
  # difficulty prompt and one of the entries there starts cod2_attract.bik.
  Start-Sleep -Seconds 25
  $en_escena = EnEscena
  for ($intento = 1; $intento -le 16 -and -not $en_escena; $intento++) {
    if ($p.HasExited) { throw 'el juego termino antes de llegar al menu' }
    & $pulsar -Teclas RETURN -Cada 2 -Veces 2 | Select-Object -Last 1
    Start-Sleep -Seconds 4
    & $pulsar -Teclas SPACE -Cada 2 -Veces 3 | Select-Object -Last 1
    Start-Sleep -Seconds 10
    $en_escena = EnEscena
    Write-Output "intento $intento de entrar en la partida: $(if ($en_escena) { 'dentro' } else { 'todavia no' })"
  }
  if ($en_escena) {
    Write-Output 'en la partida; el proceso sigue vivo'
  } else {
    Write-Output 'NO se llego a la partida; el proceso sigue vivo'
  }
}

if ($Esperar) {
  # Why this exists. A run was reported as "it crashed after the second mission" and the log simply
  # stopped: no [FATAL], no logs/excepciones.txt, no Windows Error Reporting entry for cod2.exe. The
  # game had not crashed at all - a rebuild had called Stop-Process on it four seconds after its save.
  # Nothing recorded that, and the log of a killed process and the log of a crash look identical,
  # because both are a file that ends. The exit code is what tells them apart, and until now nothing
  # read it.
  $p.WaitForExit()
  # The switch is on the formatted hex and not on the number: PowerShell parses 0xFFFFFFFF as Int32 -1,
  # and a process killed by TerminateProcess reports exactly -1, so a numeric switch silently falls
  # through to the default for the one case that matters most.
  $texto = '{0:X8}' -f ($p.ExitCode -band 0xFFFFFFFFL)
  $que = switch ($texto) {
    '00000000' { 'salio por su cuenta, sin error' }
    'FFFFFFFF' { 'lo mataron desde fuera con TerminateProcess (Stop-Process / taskkill /F)' }
    '00000001' { 'lo mataron desde fuera (cerrar la ventana a la fuerza)' }
    'C0000005' { 'violacion de acceso; mira logs/excepciones.txt y logs/pilas_*.txt' }
    'C000001D' { 'instruccion ilegal; mira logs/excepciones.txt' }
    'C0000409' { 'fail-fast (__fastfail: abort, /GS o el comprobador del monton); logs/excepciones.txt lo dice' }
    'C000013A' { 'Ctrl+C o cierre de consola' }
    default    { 'no esta en la lista de arriba: busca este codigo antes de culpar al port' }
  }
  Write-Output "cod2.exe termino: 0x$texto - $que"
  $exc = Join-Path $Destino 'logs\excepciones.txt'
  if (Test-Path -LiteralPath $exc) {
    Write-Output "excepciones: $exc ($((Get-Item -LiteralPath $exc).Length) B)"
  } else {
    Write-Output "excepciones: no hay $exc, asi que el port no vio ninguna excepcion"
  }
}

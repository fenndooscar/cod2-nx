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
param(
  [string]$Destino = (Join-Path $env:LOCALAPPDATA 'cod2-nx\pc'),
  [string]$GameDataRoot = '',
  [switch]$SinCopiar
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
$args = @(
  "--game_data_root=$GameDataRoot",
  "--user_data_root=$(Join-Path $Destino 'datos')",
  '--store_shaders=false'
)
$p = Start-Process -FilePath (Join-Path $Destino 'cod2.exe') -ArgumentList $args `
  -WorkingDirectory $Destino -PassThru
Write-Output "cod2.exe pid $($p.Id)"
Write-Output "registro: $(Join-Path $Destino 'logs')"

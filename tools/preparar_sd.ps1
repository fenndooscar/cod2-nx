# cod2 - puts the port on an SD card: the NRO, its settings and the extracted game.
#
# Why it exists: the SD card layout is not "copy the folder". The NRO has to be renamed cod2-nx.nro, the settings file
# has to be called cod2.toml, and the game files have to be LOOSE under game_root/ - the ISO cannot be used, because on
# the Switch a disc image has to be mapped whole into RAM (sdk/src/core/mapped_memory_switch.cpp caps that at 256 MB)
# and the game's ISO is about 7 GB. The copy is also 5 GB, which is where a hand copy gets interrupted and leaves a
# half-tree that fails on the console with no explanation.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\preparar_sd.ps1 -Destino E:\
#   powershell -ExecutionPolicy Bypass -File tools\preparar_sd.ps1 -Destino C:\tmp\sd -Comprobar
#
# -Destino is the root of the SD card (or any folder); the tree is created under <Destino>\switch\cod2-nx.
# -Comprobar only lists what is missing or the wrong size, and copies nothing.
param(
    [Parameter(Mandatory = $true)][string]$Destino,
    [string]$Nro = '',
    [string]$Juegos = '',
    [switch]$Comprobar
)
$ErrorActionPreference = 'Stop'

$raiz = Split-Path -Parent $PSScriptRoot
if (-not $Nro) { $Nro = Join-Path $raiz 'app\out\sw8\cod2.nro' }
if (-not $Juegos) { $Juegos = Join-Path $raiz 'assets\game_root' }
$toml = Join-Path $raiz 'app\cod2.toml'

foreach ($f in @($Nro, $Juegos, $toml)) {
    if (-not (Test-Path $f)) { throw "no existe: $f" }
}

$destino = (Resolve-Path -LiteralPath (New-Item -ItemType Directory -Force -Path $Destino)).Path
$carpeta = Join-Path $destino 'switch\cod2-nx'

# Copying into the repository would put 5 GB of game data where .gitignore has to keep catching it, and the next
# `git status` would show it. Refuse instead of doing it quietly.
if ($destino.StartsWith($raiz, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "el destino esta dentro del repositorio ($raiz). Usa la SD o una carpeta de fuera."
}

$nroMb = [math]::Round((Get-Item $Nro).Length / 1MB, 1)
$juegosMb = [math]::Round((Get-ChildItem $Juegos -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 0)
$necesitaMb = $juegosMb + $nroMb + 32

if ($Comprobar) {
    Write-Output "destino: $carpeta"
    $nroDestino = Join-Path $carpeta 'cod2-nx.nro'
    if (Test-Path $nroDestino) {
        $a = (Get-Item $nroDestino).Length; $b = (Get-Item $Nro).Length
        Write-Output ("cod2-nx.nro  {0} bytes  {1}" -f $a, $(if ($a -eq $b) { 'ok' } else { "DISTINTO de los $b del original" }))
    } else { Write-Output 'cod2-nx.nro  FALTA' }
    foreach ($n in @('cod2.toml', 'game_root')) {
        if (Test-Path (Join-Path $carpeta $n)) { Write-Output "$n  ok" } else { Write-Output "$n  FALTA" }
    }
    return
}

$libreMb = [math]::Round((Get-PSDrive (Split-Path -Qualifier $destino).TrimEnd(':')).Free / 1MB, 0)
if ($libreMb -lt $necesitaMb) {
    throw "quedan $libreMb MB en $destino y hacen falta unos $necesitaMb MB"
}

Write-Output "NRO      -> $carpeta\cod2-nx.nro  ($nroMb MB)"
New-Item -ItemType Directory -Force -Path $carpeta | Out-Null
Copy-Item -Force $Nro (Join-Path $carpeta 'cod2-nx.nro')
Copy-Item -Force $toml (Join-Path $carpeta 'cod2.toml')

Write-Output "juego    -> $carpeta\game_root  ($juegosMb MB, 20.000 ficheros; tarda unos minutos)"
# robocopy and not Copy-Item: 20,000 files, and a copy that is interrupted can be resumed by running this again.
# Its exit codes are a bit field, not an error code: 0-7 are all success, 8 and above are failures.
& robocopy $Juegos (Join-Path $carpeta 'game_root') /E /NFL /NDL /NJH /NP /R:2 /W:1 | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy fallo con $LASTEXITCODE" }

Write-Output ''
Write-Output 'Listo. En la consola tiene que quedar asi:'
Write-Output '    sdmc:/switch/cod2-nx/cod2-nx.nro'
Write-Output '    sdmc:/switch/cod2-nx/cod2.toml'
Write-Output '    sdmc:/switch/cod2-nx/game_root/...'

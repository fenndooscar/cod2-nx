# cod2 - run one PC case per tag, wait for a real mission, keep the RAW capture and the report lines beside it.
#
# WHY IT EXISTS
# tools/pc/abaliza.ps1 compares cvar sets, but it keeps only the last capture, brightens it when saving the
# comparison image (which clips anything above 85) and measures the capture against a 4x2 grid that only
# means anything while the diagnostic mosaic is on. For the black-scene work the interesting question is what
# the RAW screen holds per case, so this script copies the unmodified PNG and prints the raw log lines that
# explain it, without brightening anything.
#
# It also waits for the mission instead of for a fixed number of seconds: -Partida is flaky and a capture
# taken while the game is still in the menu looks exactly like a renderer that draws nothing.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\pc\casos_render_nativo.ps1 `
#     -Casos '--cod2_nativo_prueba_z_siempre=true' -Etiquetas 'z_siempre'
param(
  [Parameter(Mandatory = $true)][string[]]$Casos,
  [Parameter(Mandatory = $true)][string[]]$Etiquetas,
  [string]$Base = '--cod2_renderizador=nativo,--cod2_video_saltar=true,--cod2_nativo_sin_z_quad_escena=true,--cod2_nativo_formatos_extra=true,--cod2_nativo_estadisticas_pipeline=true,--cod2_captura_cada_s=5,--cod2_captura_max=60',
  [int]$MaximoEspera = 300,
  [int]$TrasEntrar = 15
)
$ErrorActionPreference = 'Stop'
$d = Join-Path $env:LOCALAPPDATA 'cod2-nx\pc'
$salida = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'app\out\pc'
if ($Casos.Count -ne $Etiquetas.Count) { throw 'Casos y Etiquetas deben tener el mismo numero de elementos' }

# The same question jugar.ps1 asks: is the three-dimensional scene being drawn? In the intro, the menu and
# the loading screens the scene target has a handful of draws or none; in a mission it has hundreds.
function EnEscena {
  $f = Get-ChildItem (Join-Path $d 'logs') -Filter 'cod2_*.log' -ErrorAction SilentlyContinue |
       Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if (-not $f) { return $false }
  $l = Select-String -Path $f.FullName -Pattern 'C6 area abierta por fotograma' | Select-Object -Last 1
  if (-not $l) { return $false }
  return ($l.Line -match 'escena [\d.]+ Mtexels en [\d.]+ pases \((\d+) dibujos' -and [int]$Matches[1] -ge 30)
}

for ($i = 0; $i -lt $Casos.Count; $i++) {
  $etiqueta = $Etiquetas[$i]
  # A case of "-" means "no extra cvar": PowerShell cannot bind an empty string to a Mandatory array
  # parameter, and the base run is the one every other case is compared against.
  $extra = if ($Casos[$i] -and $Casos[$i] -ne '-') { "$Base,$($Casos[$i])" } else { $Base }
  Write-Output "===== $etiqueta : $($Casos[$i]) ====="
  Get-Process cod2 -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Seconds 3
  Remove-Item (Join-Path $d 'capturas') -Recurse -Force -ErrorAction SilentlyContinue
  & (Join-Path $PSScriptRoot 'jugar.ps1') -Partida -Ventana 1280x720 -Extra $extra | Select-Object -Last 1
  $t0 = Get-Date
  while (-not (EnEscena) -and ((Get-Date) - $t0).TotalSeconds -lt $MaximoEspera) {
    Start-Sleep -Seconds 5
  }
  $en_escena = EnEscena
  Write-Output ("  en la partida: " + $(if ($en_escena) { 'si' } else { 'NO (la medida no vale)' }))
  Start-Sleep -Seconds $TrasEntrar
  $c = Get-ChildItem (Join-Path $d 'capturas') -Filter '*.png' -ErrorAction SilentlyContinue |
       Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if ($c) {
    Copy-Item $c.FullName (Join-Path $salida "caso_$etiqueta.png") -Force
    Write-Output "  captura: $($c.Name)"
  } else {
    Write-Output '  SIN CAPTURA'
  }
  $f = Get-ChildItem (Join-Path $d 'logs') -Filter 'cod2_*.log' | Sort-Object LastWriteTime -Descending |
       Select-Object -First 1
  foreach ($patron in 'C2 por fotograma y tipo de pasada', 'volcado de profundidad',
                     'C6: dibujados=', 'diag resueltas:', 'cielo:') {
    Select-String -Path $f.FullName -Pattern $patron | Select-Object -Last 1 |
      ForEach-Object { '  ' + ($_.Line -replace '^.*\[nativo\] ', '') }
  }
  Write-Output "  registro: $($f.Name)"
}
Get-Process cod2 -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Output 'fin'

# cod2 - captures the game window several times in a row and reports how much of the screen changes
# between frames.
#
# Why it exists: a still capture cannot tell a frozen menu from a playing cutscene from a spinning
# loading icon. Comparing consecutive captures can, and it is the only way to know from outside the
# process whether the guest is still drawing anything.
#
# Usage: powershell -ExecutionPolicy Bypass -File tools/pc/rafaga.ps1 [-Cuadros 6] [-SeparacionMs 900]
param(
  [string]$Proceso = 'cod2',
  [int]$Cuadros = 6,
  [int]$SeparacionMs = 900,
  [string]$Carpeta = 'app/out/pc/logs/rafaga'
)

$raiz = Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Force -Path $Carpeta | Out-Null
$marca = Get-Date -Format 'HHmmss'
for ($i = 0; $i -lt $Cuadros; $i++) {
  $destino = Join-Path $Carpeta "$marca`_$i.png"
  & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'pantalla.ps1') `
      -Proceso $Proceso -Salida $destino | Out-Null
  Write-Host "cuadro $i -> $destino"
  if ($i -lt $Cuadros - 1) { Start-Sleep -Milliseconds $SeparacionMs }
}

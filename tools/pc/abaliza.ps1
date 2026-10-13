# cod2 - run the same mission several times with different cvars and measure the scene target's mosaic cell.
# Usage: abaliza.ps1 -Casos '--no-x,--no-y','--no-z' -Etiquetas 'sin_saltar','sin_perezosa'
param(
  [Parameter(Mandatory = $true)][string[]]$Casos,
  [Parameter(Mandatory = $true)][string[]]$Etiquetas,
  [string]$Base = '--cod2_renderizador=nativo,--cod2_nativo_diag_resueltas=true,--cod2_captura_cada_s=10,--cod2_captura_max=60',
  [int]$Espera = 55,
  [int]$Celda = 6,
  [int]$Segundos = 10
)
$ErrorActionPreference = 'Stop'
$d = Join-Path $env:LOCALAPPDATA 'cod2-nx\pc'
$py = 'C:\Users\igor2\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe'
if ($Casos.Count -ne $Etiquetas.Count) { throw 'Casos y Etiquetas deben tener el mismo numero de elementos' }
for ($i = 0; $i -lt $Casos.Count; $i++) {
  $etiqueta = $Etiquetas[$i]
  $extra = if ($Casos[$i]) { "$Base,$($Casos[$i])" } else { $Base }
  Write-Output "===== $etiqueta : $extra ====="
  Get-Process cod2 -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Seconds 3
  Remove-Item (Join-Path $d 'capturas') -Recurse -Force -ErrorAction SilentlyContinue
  & (Join-Path $PSScriptRoot 'jugar.ps1') -Partida -Ventana 1280x720 -Extra $extra
  Start-Sleep -Seconds $Espera
  & $py -c @"
from PIL import Image
import numpy as np, glob, os, re, sys
# The label arrives as an argument and is not interpolated into the source: a two-element -Etiquetas put
# 'a','b' inside the literal and python died with 'invalid syntax', so the first case was never measured.
etiqueta = sys.argv[1]
ps = sorted(glob.glob(r'$d\capturas\*.png'), key=lambda p:int(re.search(r'_(\d+)s',p).group(1)))
if not ps: print('  SIN CAPTURAS'); raise SystemExit
p = ps[-1]
a = np.asarray(Image.open(p).convert('RGB')).astype(int)
print('  %s  %s  ventana media %.2f no-negros %d' % (etiqueta, os.path.basename(p), a.mean(), int((a.sum(axis=2)>40).sum())))
print('  celdas: ' + '  '.join('%d=%.1f/%d' % (i, a[(i//4)*180+3:(i//4)*180+177, (i%4)*320+3:(i%4)*320+317].mean(),
                                            int((a[(i//4)*180+3:(i//4)*180+177, (i%4)*320+3:(i%4)*320+317].sum(axis=2)>40).sum()))
                                      for i in range(8)))
c = $Celda
x=(c%4)*320+3; y=(c//4)*180+3
t = a[y:y+174, x:x+314]
print('  celda %d  media %.2f  desv %.2f  max %d  no-negros %d' % (c, t.mean(), t.std(), t.max(), int((t.sum(axis=2)>40).sum())))
Image.fromarray(np.clip(a*3,0,255).astype('uint8')).save(r'C:\Users\igor2\Documents\Projects\cod2-nx\app\out\pc\ab_' + etiqueta + '.png')
"@ $etiqueta
  $f = Get-ChildItem (Join-Path $d 'logs') -Filter 'cod2_*.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
  Select-String -Path $f.FullName -Pattern 'C2 por fotograma y tipo de pasada' | Select-Object -Last 1 | ForEach-Object { '  ' + ($_.Line -replace '^.*\[nativo\] ','') }
  Select-String -Path $f.FullName -Pattern 'diag resueltas' | Select-Object -Last 1 | ForEach-Object { '  ' + ($_.Line -replace '^.*\[nativo\] ','') }
}
Get-Process cod2 -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Output 'fin'

# cod2 - captura la pantalla a un PNG para mirarla.
#
# Por que existe
#   El arranque del port solo se puede comprobar mirandolo: los registros dicen que la emulacion de GPU
#   recibe ordenes, pero no si en la pantalla hay algo. Esta captura es la forma de verlo desde fuera.
#
# Uso
#   powershell -ExecutionPolicy Bypass -File tools/pc/pantalla.ps1
#   powershell -ExecutionPolicy Bypass -File tools/pc/pantalla.ps1 -Proceso cod2 -Salida app/out/pc/logs/pantalla.png
#
#   Trae la ventana del proceso al frente (AppActivate) y captura el escritorio entero. A proposito no
#   recorta a la ventana: si todavia no hay ventana, lo capturado es lo que se esta viendo, que es la
#   respuesta a la pregunta.

param(
  [string]$Proceso = 'cod2',
  [string]$Salida = 'app/out/pc/logs/pantalla.png'
)

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

# The screen is 2880x1800 but Windows scales the desktop to 1440x900 for a process that has not declared
# itself DPI aware, and CopyFromScreen then returns that scaled copy: the capture would be a blurry half
# resolution image. Declaring it makes the capture the real pixels of the game window.
Add-Type @'
using System.Runtime.InteropServices;
public class Dpi { [DllImport("user32.dll")] public static extern bool SetProcessDPIAware(); }
'@
[void][Dpi]::SetProcessDPIAware()

$traido = 'sin ventana'
foreach ($p in @(Get-Process -Name $Proceso -ErrorAction SilentlyContinue)) {
  if ($p.MainWindowHandle -ne 0) {
    try {
      (New-Object -ComObject WScript.Shell).AppActivate($p.Id) | Out-Null
      Start-Sleep -Milliseconds 700
      $traido = "pid $($p.Id) al frente"
    } catch {
      $traido = "pid $($p.Id) sin poder traerlo al frente: $($_.Exception.Message)"
    }
    break
  }
}

$rect = [System.Windows.Forms.SystemInformation]::VirtualScreen
$mapa = New-Object System.Drawing.Bitmap $rect.Width, $rect.Height
$lienzo = [System.Drawing.Graphics]::FromImage($mapa)
$lienzo.CopyFromScreen($rect.X, $rect.Y, 0, 0, $mapa.Size)
$lienzo.Dispose()

$carpeta = Split-Path -Parent $Salida
if ($carpeta -and -not (Test-Path $carpeta)) { New-Item -ItemType Directory -Path $carpeta -Force | Out-Null }
$completo = Join-Path (Get-Location) $Salida
$mapa.Save($completo, [System.Drawing.Imaging.ImageFormat]::Png)
$mapa.Dispose()

"$completo  $($rect.Width)x$($rect.Height)  $traido"
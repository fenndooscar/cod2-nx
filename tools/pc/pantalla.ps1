# cod2 - captura la ventana del juego a un PNG para mirarla.
#
# Por que existe
#   El arranque del port solo se puede comprobar mirandolo: los registros dicen que la emulacion de GPU
#   recibe ordenes, pero no si en la pantalla hay algo. Esta captura es la forma de verlo desde fuera.
#
# Uso
#   powershell -ExecutionPolicy Bypass -File tools/pc/pantalla.ps1
#   powershell -ExecutionPolicy Bypass -File tools/pc/pantalla.ps1 -Proceso cod2 -Salida C:\ruta\pantalla.png
#
#   Trae la ventana del proceso al frente (AppActivate) y captura SOLO esa ventana, recortada a su rectangulo.
#
# Por que se recorta a la ventana
#   Antes capturaba el escritorio entero, a proposito, con el argumento de que si todavia no hay ventana lo
#   capturado es lo que se esta viendo. En la practica eso devolvio una captura de OTRO juego: habia un segundo
#   port de ReXGlue (ufc3-undisputed-nx) abierto encima, AppActivate no trajo al frente la ventana de cod2
#   porque el proceso no tenia ninguna, y el PNG era el menu del otro juego - que se lee como "cod2 pinta un
#   menu" si uno no mira el titulo de la ventana. Recortar a la ventana hace que ese error sea imposible: sin
#   ventana no hay captura, y el script lo dice.

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
using System;
using System.Drawing;
using System.Runtime.InteropServices;
public class Dpi {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int cmd);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  // PW_RENDERFULLCONTENT (2) is what makes this work for a D3D/Vulkan window: without it PrintWindow asks the
  // window to paint itself through WM_PRINT, which a swapchain-backed window does not answer, and the result
  // is black. It also works when the window is behind another one, which CopyFromScreen does not.
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
  public static bool Pintar(IntPtr hWnd, Bitmap bmp) {
    using (Graphics g = Graphics.FromImage(bmp)) {
      IntPtr hdc = g.GetHdc();
      bool ok = PrintWindow(hWnd, hdc, 2);
      g.ReleaseHdc(hdc);
      return ok;
    }
  }
  // A capture that is entirely one colour is not a capture; it is the failure this script exists to catch.
  public static bool TodoNegro(Bitmap bmp) {
    for (int y = 0; y < bmp.Height; y += 7) {
      for (int x = 0; x < bmp.Width; x += 7) {
        Color c = bmp.GetPixel(x, y);
        if (c.R > 8 || c.G > 8 || c.B > 8) return false;
      }
    }
    return true;
  }
  // A minimised window has no image to read. It is restored instead of refused: in windowed mode the game's
// window ends up minimised on its own when nothing is looking at the desktop, and refusing made every
// windowed capture fail with "esta minimizada" while the game was running fine.
public static bool Restaurar(IntPtr hWnd) {
  return ShowWindow(hWnd, 9);  // SW_RESTORE
}
// Without this the RECT comes back as 0x0 and the failure reads as "the window measures 0x0", which is a
  // wrong diagnosis of the right problem: PowerShell 5.1 needs the P/Invoke declared with an explicit
  // out-parameter size, because the marshaller otherwise passes a pointer to a copy.
  public static int[] Rect(IntPtr hWnd) {
    RECT r;
    if (!GetWindowRect(hWnd, out r)) return null;
    return new int[] { r.Left, r.Top, r.Right, r.Bottom };
  }
}
'@ -ReferencedAssemblies System.Drawing
[void][Dpi]::SetProcessDPIAware()

$ventana = $null
foreach ($p in @(Get-Process -Name $Proceso -ErrorAction SilentlyContinue)) {
  if ($p.MainWindowHandle -ne 0) { $ventana = $p; break }
}
if (-not $ventana) {
  $hay = @(Get-Process -Name $Proceso -ErrorAction SilentlyContinue).Count
  throw "no hay ventana de '$Proceso' (procesos con ese nombre: $hay). No se captura el escritorio: eso ya devolvio una vez la ventana de otro juego."
}
if ([Dpi]::IsIconic($ventana.MainWindowHandle)) {
  [void][Dpi]::Restaurar($ventana.MainWindowHandle)
  Start-Sleep -Milliseconds 600
}
[void][Dpi]::SetForegroundWindow($ventana.MainWindowHandle)
Start-Sleep -Milliseconds 900

$rect = [Dpi]::Rect($ventana.MainWindowHandle)
if (-not $rect) { throw "GetWindowRect fallo para el pid $($ventana.Id)" }
$ancho = $rect[2] - $rect[0]
$alto = $rect[3] - $rect[1]
if ($ancho -le 0 -or $alto -le 0) { throw "la ventana mide ${ancho}x${alto}" }

# PrintWindow first: it reads the window itself, so another window on top of it cannot end up in the PNG, and
# neither can the desktop. If the window does not answer it (some swapchain setups do not), fall back to
# reading the screen - and say which one was used, because the fallback can pick up an occluding window.
$mapa = New-Object System.Drawing.Bitmap $ancho, $alto
$via = 'PrintWindow'
if (-not [Dpi]::Pintar($ventana.MainWindowHandle, $mapa) -or [Dpi]::TodoNegro($mapa)) {
  $via = 'CopyFromScreen (PrintWindow no dio imagen)'
  $lienzo = [System.Drawing.Graphics]::FromImage($mapa)
  $lienzo.CopyFromScreen($rect[0], $rect[1], 0, 0, $mapa.Size)
  $lienzo.Dispose()
}
$negra = [Dpi]::TodoNegro($mapa)

$carpeta = Split-Path -Parent $Salida
if ($carpeta -and -not (Test-Path $carpeta)) { New-Item -ItemType Directory -Path $carpeta -Force | Out-Null }
# Join-Path does not know an absolute path when it sees one: Join-Path C:\repo C:\otro\f.png gives
# C:\repo\C:\otro\f.png, and Bitmap::Save then throws "The given path's format is not supported" - after the
# capture has already been taken, so the screen is lost and only the error is left. An absolute -Salida is
# the obvious thing to pass, so it has to work.
$completo = if ([System.IO.Path]::IsPathRooted($Salida)) { $Salida } else { Join-Path (Get-Location) $Salida }
$mapa.Save($completo, [System.Drawing.Imaging.ImageFormat]::Png)
$mapa.Dispose()

$aviso = if ($negra) { ' *** LA VENTANA ESTA NEGRA ***' } else { '' }
"$completo  ${ancho}x${alto} en ($($rect[0]),$($rect[1]))  pid $($ventana.Id) '$($ventana.MainWindowTitle)'  via: $via$aviso"
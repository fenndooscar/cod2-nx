# cod2 - rectangulo de la ventana de un proceso, para recortar una captura a lo que dibuja el juego.
#
#   powershell -ExecutionPolicy Bypass -File tools/pc/ventana.ps1 -Proceso cod2
#   -> "x y ancho alto pid titulo" o "sin ventana"
#
# Existe porque la captura de pantalla completa incluye el escritorio, y para juzgar si el juego dibuja
# hay que mirar solo su area de cliente. El rectangulo que devuelve es el del marco; el titulo queda
# dentro, y tools/pc/pantalla.ps1 lo recorta.

param([string]$Proceso = 'cod2')

Add-Type @'
using System;
using System.Runtime.InteropServices;
public struct RECTV { public int Left, Top, Right, Bottom; }
public class Ventana {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECTV r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECTV r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINTV p);
}
public struct POINTV { public int X, Y; }
'@

foreach ($p in @(Get-Process -Name $Proceso -ErrorAction SilentlyContinue)) {
  if ($p.MainWindowHandle -eq 0) { continue }
  $r = New-Object RECTV
  if (-not [Ventana]::GetWindowRect($p.MainWindowHandle, [ref]$r)) { continue }
  $c = New-Object RECTV
  [void][Ventana]::GetClientRect($p.MainWindowHandle, [ref]$c)
  $origen = New-Object POINTV
  [void][Ventana]::ClientToScreen($p.MainWindowHandle, [ref]$origen)
  "$($r.Left) $($r.Top) $($r.Right - $r.Left) $($r.Bottom - $r.Top) $($p.Id) `"$($p.MainWindowTitle)`""
  "cliente $($origen.X) $($origen.Y) $($c.Right - $c.Left) $($c.Bottom - $c.Top)"
  exit 0
}
'sin ventana'
exit 1
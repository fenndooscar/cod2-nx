# cod2 - pulsa teclas en la ventana del juego, para llegar al juego sin manos.
#
# Por que existe
#   Todo lo que se ve despues del menu (la escena 3D) solo se puede mirar entrando en una partida, y entrar
#   pide pulsar. Sin esto, cada comprobacion del renderizador depende de que alguien se siente delante.
#
# Uso
#   powershell -ExecutionPolicy Bypass -File tools/pc/pulsar.ps1 -Teclas ENTER -Cada 3 -Veces 20
#   powershell -ExecutionPolicy Bypass -File tools/pc/pulsar.ps1 -Teclas ESC -Cada 2 -Veces 10
#
#   Trae la ventana al frente antes de cada pulsacion (el juego ignora el teclado si no tiene el foco) y manda
#   la tecla con SendInput, que es lo que ve un juego que lee el teclado por DirectInput/SDL.

param(
  [string]$Proceso = 'cod2',
  [string]$Teclas = 'ENTER',
  [double]$Cada = 3,
  [int]$Veces = 20
)

Add-Type @'
using System;
using System.Runtime.InteropServices;
public class Tecla {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public InputUnion u; }
  [StructLayout(LayoutKind.Explicit)] public struct InputUnion { [FieldOffset(0)] public KEYBDINPUT ki; }
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT {
    public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo;
  }
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, int tam);
  public static void Pulsar(ushort vk) {
    INPUT[] lote = new INPUT[2];
    lote[0].type = 1; lote[0].u.ki.wVk = vk; lote[0].u.ki.dwFlags = 0;
    lote[1].type = 1; lote[1].u.ki.wVk = vk; lote[1].u.ki.dwFlags = 2;  // KEYEVENTF_KEYUP
    SendInput(2, lote, Marshal.SizeOf(typeof(INPUT)));
  }
}
'@

# VK codes for the keys this is used with.
$codigos = @{
  'ENTER' = 0x0D; 'RETURN' = 0x0D; 'SPACE' = 0x20; 'ESC' = 0x1B; 'ESCAPE' = 0x1B;
  'UP' = 0x26; 'DOWN' = 0x28; 'LEFT' = 0x25; 'RIGHT' = 0x27; 'TAB' = 0x09; 'F1' = 0x70
}
$pedidas = @($Teclas -split '[,;]' | ForEach-Object { $_.Trim().ToUpper() } | Where-Object { $_ })
foreach ($t in $pedidas) {
  if (-not $codigos.ContainsKey($t)) { throw "tecla desconocida '$t' (se conocen: $($codigos.Keys -join ', '))" }
}

$ventana = $null
foreach ($p in @(Get-Process -Name $Proceso -ErrorAction SilentlyContinue)) {
  if ($p.MainWindowHandle -ne 0) { $ventana = $p; break }
}
if (-not $ventana) { throw "no hay ventana de '$Proceso'" }

for ($n = 1; $n -le $Veces; $n++) {
  if ($ventana.HasExited) { "el proceso termino en la pulsacion $n"; break }
  [void][Tecla]::SetForegroundWindow($ventana.MainWindowHandle)
  Start-Sleep -Milliseconds 120
  foreach ($t in $pedidas) {
    [Tecla]::Pulsar([uint16]$codigos[$t])
    Start-Sleep -Milliseconds 60
  }
  "$n/$Veces  $($pedidas -join '+')"
  Start-Sleep -Seconds $Cada
}

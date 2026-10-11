# Builds the NRO of the default tree (app/) for the Switch: app/out/sw8/cod2.nro.
#
#   powershell -ExecutionPolicy Bypass -File tools\build.ps1 -MesaSdk C:\mesa-sdk\opt\devkitpro\portlibs\switch
#
# Needs devkitPro, CMake 3.25+, Ninja, the generated code (tools/codegen.sh) and the Mesa SDK (mesa/README.md).
# The first run configures app/out/sw8; later runs only build. CMake runs through Start-Process: in Windows
# PowerShell 5.1 every line a native program writes to stderr (a plain CMake warning) becomes an error that stops the
# script. The output goes to app/out/sw8/configure.log, build.log and their .err files.
#
# -Pgo is empty by default: pgo/ has no profile for this game yet (pgo/README.md). Pass -Pgo usar only once you have
# recorded one, or -Pgo generar to record it.
param(
    [Parameter(Mandatory = $true)][string]$MesaSdk,
    [ValidateSet('usar', 'generar', '')][string]$Pgo = '',
    [int]$Jobs = 4,
    [string]$CMake = 'cmake'
)
$ErrorActionPreference = 'Stop'
$root = (Split-Path -Parent $PSScriptRoot).Replace('\', '/')
$app = "$root/app"
$out = "$app/out/sw8"
$MesaSdk = $MesaSdk.Replace('\', '/')

function Invoke-CMake([string]$name, [string[]]$arguments) {
    $quoted = $arguments | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }
    $p = Start-Process -FilePath $CMake -ArgumentList $quoted -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput "$out/$name.log" -RedirectStandardError "$out/$name.err"
    if ($p.ExitCode) { throw "cmake ($name) failed with $($p.ExitCode), see $out/$name.log and $name.err" }
}

New-Item -ItemType Directory -Force $out | Out-Null
if (-not (Test-Path "$out/CMakeCache.txt")) {
    Invoke-CMake 'configure' @('-S', $app, '-B', $out, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_TOOLCHAIN_FILE=$root/tools/switch/cmake/switch-devkitA64.cmake", "-DREXSDK_DIR=$root/sdk",
        "-DREXGLUE_SWITCH_NVK_SDK=$MesaSdk", "-DCOD2_PGO=$Pgo", '-DCOD2_BUILD_LAUNCHER=OFF')
}
$start = Get-Date
# The NRO is not a ninja output: it is a POST_BUILD step of the `cod2` target, so `ninja` only regenerates it when it
# relinks. With the ELF already up to date, deleting cod2.nro and rebuilding did nothing, printed "Built ... in 0 min"
# and left no NRO. Removing the ELF makes ninja relink and run the step again.
if (-not (Test-Path "$out/cod2.nro")) { Remove-Item -Force "$out/cod2" -ErrorAction SilentlyContinue }
Invoke-CMake 'build' @('--build', $out, '-j', "$Jobs")
if (-not (Test-Path "$out/cod2.nro")) { throw "the build left no $out/cod2.nro" }
Write-Output "Built $out/cod2.nro in $([int]((Get-Date) - $start).TotalMinutes) min"

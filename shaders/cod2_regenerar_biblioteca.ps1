# cod2 - build `cod2_shaders.nfsp` on Windows, out of the shader containers inside the `.ff` fastfiles.
#
# Why this exists
#     The Xbox 360 version of Call of Duty 2 ships no loose shader files: the containers are assets
#     inside the `.ff` fastfiles. `shaders/cod2_buscar_contenedores.cpp` walks loose files, so it
#     cannot see them, and the pipeline that would have fed it
#     (`shaders/cod2_regenerar_biblioteca_pcf.sh`) starts from a folder of containers that no tool in
#     this repository produced. This script is that missing first step plus the steps after it, on
#     Windows, where this port's shader work is done.
#
#     `tools/extraer_shaders.py` inflates each fastfile and writes the containers out; this script
#     translates them to HLSL, compiles that to SPIR-V with DXC, validates it, and packs the library.
#
#     Verified on the extracted retail game: 111 fastfiles, none skipped, 7,508 container
#     occurrences, 1,124 distinct (653 pixel, 471 vertex), all 1,124 translating, compiling and
#     validating, into a 22,456,836-byte library. `shaders/README.md` has the details.
#
# What is deliberately NOT here, compared with the .sh
#     The .sh rewrites three things in the HLSL before compiling: the shadow-map samples to
#     `tfetch2DSombra` / `tfetch2DSombraMin` (behind specialization constants) and the radial blur of
#     the composition shader. All three are Need for Speed: Most Wanted's, keyed by that game's
#     container names (`p_000139`) and that game's sampler defines. None of them is needed to produce
#     a library that loads and draws; they are performance work for a renderer this game does not
#     have yet. Add them the same way once CoD2's own shaders are identified.
#
# How to use it
#     powershell -ExecutionPolicy Bypass -File shaders\cod2_regenerar_biblioteca.ps1
#     powershell -ExecutionPolicy Bypass -File shaders\cod2_regenerar_biblioteca.ps1 -Salida assets/bib
#
#     Needs clang++ on PATH, and DXC and spirv-val from the Vulkan SDK (`-VulkanSdk` if it is not at
#     the default location). Everything it writes is game data and is ignored: `*.bin`, `*.nfsp` and
#     `assets/` are in `.gitignore`.
#
# Turn it off without a rebuild
#     There is nothing to turn off: this script is not part of the build. The library is only read by
#     the native renderer, and `app/src/cod2_nativo_shaders.cpp` is the only thing that loads it.
#     Nothing in the NRO reads it today (docs/building.md step 7).

[CmdletBinding()]
param(
    # Where the intermediate folders and the library go. Must be a path `.gitignore` covers. The
    # layout is `contenedores/`, `hlsl/`, `spirv/` and `cod2_shaders.nfsp`, which is what
    # `tools/comprobar_shaders_ff.py` reads.
    [string]$Salida = "assets/shaders_ff",
    # The extracted game, with its `.ff` fastfiles.
    [string]$GameRoot = "assets/game_root",
    # The Vulkan SDK, for dxc.exe and spirv-val.exe.
    [string]$VulkanSdk = "C:\VulkanSDK\1.4.363.0",
    # Where the library ends up: next to the NRO, which is what the SD layout expects.
    [string]$Destino = "app/out/sw8/cod2_shaders.nfsp",
    # Skip the extraction, the translation and DXC, and redo only the pack, the check and the copy
    # over an extraction that is already there. DXC over the whole corpus takes a while.
    [switch]$SoloEmpaquetar
)

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
$Salida = Join-Path $raiz $Salida
$contenedores = Join-Path $Salida "contenedores"
$hlsl = Join-Path $Salida "hlsl"
$spirv = Join-Path $Salida "spirv"
$dxc = Join-Path $VulkanSdk "Bin\dxc.exe"
$val = Join-Path $VulkanSdk "Bin\spirv-val.exe"

foreach ($herramienta in @($dxc, $val)) {
    if (-not (Test-Path $herramienta)) { throw "not found: $herramienta (use -VulkanSdk)" }
}

function Paso($texto) { Write-Host "== $texto" -ForegroundColor Cyan }
function Ejecutar($programa, $argumentos, $log) {
    # PowerShell variable names are case-insensitive: a loop variable named `$destino` would silently
    # overwrite the `$Destino` parameter, and the last step would then copy to a path with a colon in
    # the middle. The same trap applies to `$Salida`/`$salida`; keep the two spellings apart.
    #
    # `$ErrorActionPreference = "Stop"` plus `2>>` is the other trap: Windows PowerShell 5.1 turns a
    # native command's stderr into a terminating error, and DXC writes ordinary warnings there. The
    # script then stops on the first shader that warns, with five of 1,124 compiled and an empty log.
    # Inside this function the preference is Continue and the exit code is what decides.
    $ErrorActionPreference = "Continue"
    if ($log) { & $programa @argumentos 2>> $log } else { & $programa @argumentos }
    $codigo = $LASTEXITCODE
    if ($codigo -ne 0) {
        $donde = if ($log) { " (stderr in $log)" } else { "" }
        throw "$programa exited with $codigo$donde"
    }
}

if (-not $SoloEmpaquetar) {
    if (Test-Path $Salida) { Remove-Item -Recurse -Force $Salida }
    New-Item -ItemType Directory -Force -Path $Salida, $hlsl, $spirv | Out-Null

    # 1. The containers, out of the fastfiles.
    Paso "extract the containers from $GameRoot"
    Ejecutar "python" @(
        (Join-Path $raiz "tools/extraer_shaders.py"), $contenedores,
        "--game-root", (Join-Path $raiz $GameRoot))

    # 2. The translator. clang++ is what this repository is verified with; the .sh uses MinGW g++.
    #    -DNOMINMAX: pch_min.h includes Windows.h, which defines a `min` macro that
    #    cod2_contenedor.h's std::min collides with under the MSVC headers clang uses here.
    Paso "build the translator"
    Ejecutar "clang++" @(
        "-std=c++23", "-O1", "-DNOMINMAX",
        "-I$(Join-Path $raiz 'shaders')", "-I$(Join-Path $raiz 'shaders/XenosRecomp')",
        "-I$(Join-Path $raiz 'sdk/thirdparty/fmt/include')",
        "-I$(Join-Path $raiz 'mesa/mesa-switch/src/util')",
        "-include", (Join-Path $raiz "shaders/pch_min.h"),
        "-DFMT_HEADER_ONLY", "-DXXH_INLINE_ALL", "-DCOD2_RECOMP",
        (Join-Path $raiz "shaders/cod2_hlsl.cpp"),
        (Join-Path $raiz "shaders/XenosRecomp/shader_recompiler.cpp"),
        "-o", (Join-Path $raiz "shaders/cod2_hlsl.exe"))

    # 3. Microcode -> HLSL. It fails on the first container `cod2::Convertir2005` rejects, which is
    #    the real validation of what the extractor found.
    Paso "translate to HLSL"
    Ejecutar (Join-Path $raiz "shaders/cod2_hlsl.exe") @(
        $contenedores, $hlsl, (Join-Path $raiz "shaders/XenosRecomp/shader_common.h"))

    # 4. HLSL -> SPIR-V, and the validator. `p_` is a pixel container, `v_` a vertex one; the
    #    extractor names them after the container's own signature bit, the same bit the library
    #    checks the SPIR-V execution model against. A swapped name is a DXC error, not a silent one.
    Paso "compile with DXC and validate"
    $comun = @("-spirv", "-E", "main", "-HV", "2021", "-fspv-target-env=vulkan1.2",
        "-fvk-use-dx-layout", "-Werror=parameter-usage")
    $cuenta = 0
    foreach ($f in Get-ChildItem $hlsl -Filter *.hlsl | Sort-Object Name) {
        $spv = Join-Path $spirv ($f.BaseName + ".spv")
        $perfil = if ($f.BaseName.StartsWith("p_")) { "ps_6_6" } else { "vs_6_6" }
        $extra = if ($perfil -eq "ps_6_6") { @() } else { @("-fvk-invert-y") }
        Ejecutar $dxc (@("-T", $perfil) + $comun + $extra + @("-Fo", $spv, $f.FullName)) (Join-Path $Salida "dxc.log")
        Ejecutar $val @("--target-env", "vulkan1.2", "--scalar-block-layout", $spv)
        $cuenta++
    }
    Write-Host "   $cuenta shaders compiled and validated"
}

# 5. Pack. `cod2_empaquetar.cpp` reads the containers back, converts each again and refuses to write
#    a library where one of them is not found by its fingerprint.
Paso "pack the library"
Ejecutar "clang++" @(
    "-std=c++23", "-O1", "-DNOMINMAX",
    "-I$(Join-Path $raiz 'shaders')",
    "-I$(Join-Path $raiz 'app/src')",
    "-I$(Join-Path $raiz 'sdk/thirdparty/xxHash')",
    (Join-Path $raiz "shaders/cod2_empaquetar.cpp"),
    (Join-Path $raiz "app/src/cod2_shader_library.cpp"),
    "-o", (Join-Path $raiz "shaders/cod2_empaquetar.exe"))
$paquete = Join-Path $Salida "cod2_shaders.nfsp"
if (Test-Path $paquete) { Remove-Item -Force $paquete }
Ejecutar (Join-Path $raiz "shaders/cod2_empaquetar.exe") @($contenedores, $spirv, $paquete)

# 6. The regression check: the library loads, every original container is found in it, and every
#    alteration of the package is rejected. `cod2_shader_vulkan.*` is still parked, and this is the
#    one thing that links it.
Paso "check the library"
Ejecutar "clang++" @(
    "-std=c++23", "-O1", "-DNOMINMAX",
    "-I$(Join-Path $raiz 'shaders')",
    "-I$(Join-Path $raiz 'app/src')",
    "-I$(Join-Path $raiz 'app/src/aparcado')",
    "-I$(Join-Path $VulkanSdk 'Include')",
    "-I$(Join-Path $raiz 'sdk/thirdparty/xxHash')",
    (Join-Path $raiz "shaders/cod2_probar_biblioteca.cpp"),
    (Join-Path $raiz "app/src/cod2_shader_library.cpp"),
    (Join-Path $raiz "app/src/aparcado/cod2_shader_vulkan.cpp"),
    "-o", (Join-Path $raiz "shaders/cod2_probar_biblioteca.exe"))
Ejecutar (Join-Path $raiz "shaders/cod2_probar_biblioteca.exe") @($paquete, $contenedores)

# 7. Next to the NRO, which is where the SD layout looks for it.
$DestinoAbs = Join-Path $raiz $Destino
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $DestinoAbs) | Out-Null
Copy-Item $paquete $DestinoAbs
Write-Host "library: $Destino ($((Get-Item $DestinoAbs).Length) bytes)" -ForegroundColor Green

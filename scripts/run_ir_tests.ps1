$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "out\build\x64-Debug"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -property installationPath
    $cmake = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
} else {
    $cmake = "cmake"
}

& $cmake -S $root -B $buildDir -G "Visual Studio 17 2022" -A x64
& $cmake --build $buildDir --config Debug

Push-Location $buildDir
try {
    ctest -C Debug --output-on-failure
} finally {
    Pop-Location
}

Write-Host "IR tests completed."

$ErrorActionPreference = "Stop"

cmake -S . -B build
cmake --build build

$Compiler = ".\build\compiler.exe"
if (-not (Test-Path $Compiler)) {
    $Compiler = ".\build\Debug\compiler.exe"
}
if (-not (Test-Path $Compiler)) {
    throw "compiler executable was not found under build/"
}

Get-ChildItem tests/parser -Filter *.tc | ForEach-Object {
    Write-Host "Parsing $($_.FullName)"
    Get-Content -Raw $_.FullName | & $Compiler --dump-ast | Out-Null
}

Write-Host "Frontend parser tests passed."

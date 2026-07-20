$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root

if (Get-Command cmake -ErrorAction SilentlyContinue) {
    cmake -S . -B build
    cmake --build build
    & "$Root/build/test_sema"
    & "$Root/build/test_sema_integration"
} else {
    $sources = @(
        "src/ast/ast.cpp",
        "src/frontend/lexer.cpp",
        "src/frontend/parser.cpp",
        "src/sema/scope.cpp",
        "src/sema/semantic_context.cpp",
        "src/sema/constant_evaluator.cpp",
        "src/sema/semantic_analyzer.cpp"
    )
    g++ -std=c++2a -Wall -Wextra -pedantic -Iinclude tests/sema/test_sema.cpp $sources -o build/test_sema.exe
    g++ -std=c++2a -Wall -Wextra -pedantic -Iinclude tests/sema/test_sema_integration.cpp $sources -o build/test_sema_integration.exe
    & "$Root/build/test_sema.exe"
    & "$Root/build/test_sema_integration.exe"
}

Write-Host "Sema tests finished."

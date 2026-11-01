param(
    [switch]$Mod,
    [switch]$Test
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$compiler = 'C:\Program Files\Windhawk\Compiler'
$engine = 'C:\Program Files\Windhawk\Engine\1.7.3\64'
$clang = Join-Path $compiler 'bin\clang++.exe'
$build = Join-Path $root 'build'

if (-not ($Mod -or $Test)) {
    Write-Error 'Usage: build.ps1 [-Mod] [-Test]'
    exit 1
}

New-Item -ItemType Directory -Force $build | Out-Null
Push-Location $root
try {
    if ($Mod) {
        $flags = Get-Content (Join-Path $compiler 'compile_flags.txt') | Where-Object { $_.Trim() }
        $libs = '-lcomctl32', '-lole32', '-loleaut32', '-lshlwapi', '-lruntimeobject'
        $clangArgs = @($flags) + @('-I', (Join-Path $compiler 'include'),
            '-shared', '-O2', '-o', 'build\explorer-tweaks.dll', 'explorer-tweaks.wh.cpp',
            '-L', $engine, '-lwindhawk',
            # Windhawk'ın kendi derleme komutuyla aynı: DllGetClassObject (XAML TAP)
            # gibi otomatik dışa aktarımdan dışlanan semboller de dışa aktarılır.
            '-Wl,--export-all-symbols') + $libs
        & $clang @clangArgs
        if ($LASTEXITCODE -ne 0) { Write-Host 'mod build FAILED'; exit 1 }
        Write-Host 'mod build OK: build\explorer-tweaks.dll'
    }

    if ($Test) {
        & $clang -x c++ -std=c++23 -target x86_64-w64-mingw32 -DUNICODE -D_UNICODE -Wall -Wextra -static `
            -o build\logic_test.exe tests\logic_test.cpp
        if ($LASTEXITCODE -ne 0) { Write-Host 'test build FAILED'; exit 1 }
        & (Join-Path $build 'logic_test.exe')
        if ($LASTEXITCODE -ne 0) { Write-Host 'tests FAILED'; exit 1 }
        Write-Host 'tests OK'
    }
}
finally {
    Pop-Location
}
exit 0

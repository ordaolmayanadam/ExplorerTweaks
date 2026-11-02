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
$source = Join-Path $root 'explorer-tweaks.wh.cpp'

if (-not ($Mod -or $Test)) {
    Write-Error 'Usage: build.ps1 [-Mod] [-Test]'
    exit 1
}

# Mod meta verisinden tek bir alan (// @name değer).
function Get-ModMetadata([string]$name) {
    $line = Select-String -Path $source -Pattern "^//\s*@$name\s+(.+)$" | Select-Object -First 1
    if (-not $line) { return '' }
    return $line.Matches[0].Groups[1].Value.Trim()
}

# Windows PowerShell 5.1 (ve 7.3 öncesi / Legacy mod) yerel programa giden
# argümanlardaki çift tırnakları kaçırmaz; C çalışma zamanı onları atar.
function ConvertTo-NativeArg([string]$arg) {
    $standard = $PSVersionTable.PSVersion -ge [version]'7.3' -and
        (Get-Variable PSNativeCommandArgumentPassing -ValueOnly -ErrorAction SilentlyContinue) -ne 'Legacy'
    if ($standard) { return $arg }
    return $arg.Replace('"', '\"')
}

New-Item -ItemType Directory -Force $build | Out-Null
Push-Location $root
try {
    if ($Mod) {
        # Windhawk'ın kendi derleme komutunun aynısı (UI\resources\app\extensions\
        # windhawk\dist\extension.js, compileModInternal): WH_EDITING yok, mod
        # kimliği/sürümü tanımlı, windhawk_api.h zorla eklenir, windhawk.lib
        # bağlanır, tüm semboller dışa aktarılır ve @compilerOptions sona eklenir.
        $id = Get-ModMetadata 'id'
        $version = Get-ModMetadata 'version'
        $options = @((Get-ModMetadata 'compilerOptions') -split '\s+' | Where-Object { $_ })
        $out = Join-Path $build 'explorer-tweaks.dll'
        $clangArgs = @(
            '-std=c++23', '-O2', '-shared', '-DUNICODE', '-D_UNICODE',
            '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00', '-D_WIN32_IE=0x0A00',
            '-DNTDDI_VERSION=0x0A000008', '-D__USE_MINGW_ANSI_STDIO=0', '-DWH_MOD',
            (ConvertTo-NativeArg "-DWH_MOD_ID=L`"$id`""),
            (ConvertTo-NativeArg "-DWH_MOD_VERSION=L`"$version`""),
            (Join-Path $engine 'windhawk.lib'),
            '-x', 'c++', $source,
            '-include', 'windhawk_api.h',
            '-target', 'x86_64-w64-mingw32',
            '-Wl,--export-all-symbols',
            '-o', $out) + $options
        # Windhawk derleyiciyi kendi klasöründe çalıştırır.
        Push-Location $compiler
        try { & $clang @clangArgs } finally { Pop-Location }
        if ($LASTEXITCODE -ne 0) { Write-Host 'mod build FAILED'; exit 1 }
        Write-Host "mod build OK: build\explorer-tweaks.dll (id=$id, version=$version)"
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

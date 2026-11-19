param(
    [string] $OutputDirectory = (Join-Path $PSScriptRoot '.build')
)

$ErrorActionPreference = 'Stop'
$Compiler = Get-Command cl.exe -ErrorAction SilentlyContinue
$ResourceCompiler = Get-Command rc.exe -ErrorAction SilentlyContinue
if (-not $Compiler -or -not $ResourceCompiler) {
    throw 'Open an x64 Developer PowerShell with MSVC and the Windows SDK.'
}

$BuildRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
New-Item -ItemType Directory -Path $BuildRoot -Force | Out-Null
$Resource = Join-Path $BuildRoot 'app.res'
$Sources = @('src/main.cpp', 'src/platform.cpp', 'src/registry.cpp') |
    ForEach-Object { Join-Path $PSScriptRoot $_ }

Push-Location -LiteralPath $PSScriptRoot
try {
    & $ResourceCompiler.Source /nologo /fo $Resource 'src/app.rc'
    if ($LASTEXITCODE -ne 0) { throw "Resource compilation failed (exit $LASTEXITCODE)." }
} finally {
    Pop-Location
}

Push-Location -LiteralPath $BuildRoot
try {
    & $Compiler.Source /nologo /utf-8 /std:c++17 /permissive- /W4 /EHsc /MT /O1 /DNDEBUG /DUNICODE /D_UNICODE /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 @Sources $Resource /link /MACHINE:X64 /SUBSYSTEM:WINDOWS /MANIFEST:NO /OPT:REF /OPT:ICF /OUT:ContextMenuToggler.exe advapi32.lib comctl32.lib gdi32.lib gdiplus.lib ktmw32.lib msimg32.lib ole32.lib shell32.lib shlwapi.lib user32.lib windowscodecs.lib uuid.lib
    if ($LASTEXITCODE -ne 0) { throw "C++ compilation failed (exit $LASTEXITCODE)." }
    Get-Item -LiteralPath 'ContextMenuToggler.exe'
} finally {
    Pop-Location
}

param([string]$Compiler = 'C:\msys64\mingw64\bin\gcc.exe')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$output = Join-Path $repo 'build-tests/dma-snapshot'
[void](New-Item -ItemType Directory -Path $output -Force)
$executable = Join-Path $output 'dma_snapshot_test.exe'
Push-Location -LiteralPath $repo
try {
    & $Compiler -std=c11 -O2 -flto -fwhole-program -ffunction-sections -fdata-sections `
        -DSNESRECOMP_TRACE=0 -DSNESRECOMP_SDL3=1 -DSDL_MAIN_HANDLED `
        -Irunner/src/desktop -Irunner/src tests/dma_snapshot_test.c `
        runner/src/snes/dma.c '-Wl,--gc-sections' -lm -o $executable
    if ($LASTEXITCODE -ne 0) { throw 'DMA snapshot test compilation failed' }
    & $executable
    if ($LASTEXITCODE -ne 0) { throw 'DMA snapshot regression failed' }
} finally {
    Pop-Location
}

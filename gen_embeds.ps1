Param(
    [string]$EnvName = "esp32cam-jade"
)

$ErrorActionPreference = "Stop"

# Diretórios base
$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $projectDir) { $projectDir = (Get-Location).Path }

$buildDir = Join-Path $projectDir ".pio\build\$EnvName"

# Caminho do cmake e script de embed do ESP-IDF instalados pelo PlatformIO
$cmakeExe = Join-Path $env:USERPROFILE ".platformio\packages\tool-cmake\bin\cmake.exe"
if (-not (Test-Path $cmakeExe)) {
    Write-Error "cmake.exe não encontrado em $cmakeExe"
}

$frameworkDir = Join-Path $env:USERPROFILE ".platformio\packages\framework-espidf"
$embedScript = Join-Path $frameworkDir "tools\cmake\scripts\data_file_embed_asm.cmake"
if (-not (Test-Path $embedScript)) {
    Write-Error "data_file_embed_asm.cmake não encontrado em $embedScript"
}

if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

# Arquivos binários que o Jade embute no firmware
$files = @(
    @{ src = "pinserver_public_key.pub";        out = "pinserver_public_key.pub.S" },
    @{ src = "logo\splash.bin.gz";             out = "splash.bin.gz.S" },
    @{ src = "logo\ce.bin.gz";                 out = "ce.bin.gz.S" },
    @{ src = "logo\fcc.bin.gz";                out = "fcc.bin.gz.S" },
    @{ src = "logo\statusbar_small.bin.gz";    out = "statusbar_small.bin.gz.S" },
    @{ src = "logo\statusbar_large.bin.gz";    out = "statusbar_large.bin.gz.S" },
    @{ src = "logo\weee.bin.gz";               out = "weee.bin.gz.S" }
)

foreach ($f in $files) {
    $srcPath = Join-Path $projectDir $f.src
    if (-not (Test-Path $srcPath)) {
        # Alguns arquivos podem não existir dependendo da configuração; apenas ignore.
        continue
    }

    $outPath = Join-Path $buildDir $f.out
    Write-Host "Gerando $($f.out) a partir de $($f.src)..."
    & $cmakeExe "-DDATA_FILE=$srcPath" "-DSOURCE_FILE=$outPath" "-DFILE_TYPE=BINARY" "-P$embedScript"
}

Write-Host "Arquivos .S gerados em $buildDir."


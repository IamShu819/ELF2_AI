param(
  [string]$Root = (Join-Path $PSScriptRoot "..\models")
)

$ErrorActionPreference = "Stop"

function Resolve-FullPath([string]$Path) {
  $executionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
}

function Download-File([string]$Url, [string]$Destination) {
  if ((Test-Path $Destination) -and ((Get-Item $Destination).Length -gt 0)) {
    Write-Host "skip existing archive: $Destination"
    return
  }
  if (Test-Path $Destination) {
    Remove-Item -LiteralPath $Destination -Force
  }
  Write-Host "download: $Url"
  $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
  if ($curl) {
    & $curl.Source -L --fail --retry 3 --retry-delay 2 --connect-timeout 30 -o $Destination $Url
  } else {
    Invoke-WebRequest -Uri $Url -OutFile $Destination
  }
  if (-not (Test-Path $Destination) -or ((Get-Item $Destination).Length -eq 0)) {
    throw "download failed or produced an empty file: $Destination"
  }
}

function Expand-TarBz2([string]$Archive, [string]$Destination) {
  New-Item -ItemType Directory -Force -Path $Destination | Out-Null
  Write-Host "extract: $Archive -> $Destination"
  tar -xjf $Archive -C $Destination
}

$rootFull = Resolve-FullPath $Root
$cache = Join-Path $rootFull "_downloads"
$asrRoot = Join-Path $rootFull "asr"
$ttsRoot = Join-Path $rootFull "tts"
$vadRoot = Join-Path $rootFull "vad"

New-Item -ItemType Directory -Force -Path $cache, $asrRoot, $ttsRoot, $vadRoot | Out-Null

$asrName = "sherpa-onnx-streaming-paraformer-bilingual-zh-en"
$asrArchive = Join-Path $cache "$asrName.tar.bz2"
$asrDir = Join-Path $asrRoot $asrName
if (-not (Test-Path (Join-Path $asrDir "encoder.int8.onnx"))) {
  Download-File `
    "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/$asrName.tar.bz2" `
    $asrArchive
  Expand-TarBz2 $asrArchive $asrRoot
} else {
  Write-Host "skip existing ASR model: $asrDir"
}

$ttsName = "vits-melo-tts-zh_en"
$ttsArchive = Join-Path $cache "$ttsName.tar.bz2"
$ttsDir = Join-Path $ttsRoot $ttsName
if (-not (Test-Path (Join-Path $ttsDir "model.onnx"))) {
  Download-File `
    "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/$ttsName.tar.bz2" `
    $ttsArchive
  Expand-TarBz2 $ttsArchive $ttsRoot
} else {
  Write-Host "skip existing TTS model: $ttsDir"
}

$vadModel = Join-Path $vadRoot "silero_vad.onnx"
Download-File `
  "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/silero_vad.onnx" `
  $vadModel

Write-Host ""
Write-Host "voice models are ready under: $rootFull"

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Fail($message) {
  Write-Error $message
  exit 1
}

function Require-File($path, $description) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    Fail "$description not found: $path"
  }
  return (Resolve-Path -LiteralPath $path).Path
}

function Find-MingwBin {
  $candidates = @()
  if ($env:VOICE_MINGW_BIN) { $candidates += $env:VOICE_MINGW_BIN }
  if ($env:MINGW_BIN) { $candidates += $env:MINGW_BIN }
  $candidates += @(
    "D:\msys64\ucrt64\bin",
    "D:\QT\Tools\mingw1310_64\bin"
  )

  foreach ($candidate in $candidates) {
    if ([string]::IsNullOrWhiteSpace($candidate)) { continue }
    $expanded = [Environment]::ExpandEnvironmentVariables($candidate.Trim())
    if (Test-Path -LiteralPath (Join-Path $expanded "gcc.exe") -PathType Leaf) {
      return (Resolve-Path -LiteralPath $expanded).Path
    }
  }
  Fail "MinGW bin not found. Set VOICE_MINGW_BIN or install MinGW at D:\msys64\ucrt64\bin."
}

$root = Split-Path -Parent $PSScriptRoot
$mingw = Find-MingwBin
$gcc = Require-File (Join-Path $mingw "gcc.exe") "gcc.exe"
$gccVersion = & $gcc --version
if ($LASTEXITCODE -ne 0) {
  Fail "gcc.exe --version failed with exit code $($LASTEXITCODE): $gcc"
}
$goCommand = Get-Command go -ErrorAction SilentlyContinue
if ($null -eq $goCommand) {
  Fail "go command not found in PATH"
}
$goExe = $goCommand.Source
$goVersion = & $goExe version
if ($LASTEXITCODE -ne 0) {
  Fail "go command failed: $goExe version"
}

$runtimeDlls = @(
  "libgcc_s_seh-1.dll",
  "libstdc++-6.dll",
  "libwinpthread-1.dll"
)
$runtimeDllPaths = @()
foreach ($dll in $runtimeDlls) {
  $runtimeDllPaths += Require-File (Join-Path $mingw $dll) "MinGW runtime $dll"
}

$ortRuntime = Join-Path $root "runtime\onnxruntime-win-x64-1.24.3\lib"
$onnxRuntimeDll = Require-File (Join-Path $ortRuntime "onnxruntime.dll") "ONNX Runtime 1.24.3 onnxruntime.dll"
if ($onnxRuntimeDll -notlike "*1.24.3*") {
  Fail "ONNX Runtime source must be version 1.24.3, got: $onnxRuntimeDll"
}
$onnxRuntimeDlls = Get-ChildItem -LiteralPath $ortRuntime -Filter "*.dll" -File
if ($onnxRuntimeDlls.Count -eq 0) {
  Fail "no ONNX Runtime 1.24.3 DLLs found in: $ortRuntime"
}

$sherpaDllDir = Join-Path $env:USERPROFILE "go\pkg\mod\github.com\k2-fsa\sherpa-onnx-go-windows@v1.13.2\lib\x86_64-pc-windows-gnu"
if (-not (Test-Path -LiteralPath $sherpaDllDir -PathType Container)) {
  Fail "sherpa-onnx Windows DLL directory not found: $sherpaDllDir"
}
$sherpaDlls = Get-ChildItem -LiteralPath $sherpaDllDir -Filter "*.dll" -File
if ($sherpaDlls.Count -eq 0) {
  Fail "no sherpa-onnx Windows DLLs found in: $sherpaDllDir"
}

Write-Host "Using MinGW bin: $mingw"
Write-Host "Using gcc: $gcc ($($gccVersion | Select-Object -First 1))"
Write-Host "Using Go: $goExe ($goVersion)"
Write-Host "Using ONNX Runtime 1.24.3: $onnxRuntimeDll"

$env:PATH = "$mingw;$env:PATH"
$env:CGO_ENABLED = "1"
$env:CC = $gcc

$finalExe = Join-Path $root "voice-engine.exe"
$tempExe = Join-Path $root "voice-engine.tmp.exe"
if (Test-Path -LiteralPath $tempExe) {
  Remove-Item -LiteralPath $tempExe -Force
}

Push-Location $root
try {
  & $goExe build -o $tempExe .
  if ($LASTEXITCODE -ne 0) {
    throw "go build failed with exit code $($LASTEXITCODE)"
  }
  if (-not (Test-Path -LiteralPath $tempExe -PathType Leaf)) {
    throw "go build did not create temporary executable: $tempExe"
  }

  Copy-Item -LiteralPath $sherpaDlls.FullName -Destination $root -Force
  Copy-Item -LiteralPath $onnxRuntimeDlls.FullName -Destination $root -Force
  Copy-Item -LiteralPath $runtimeDllPaths -Destination $root -Force

  Move-Item -LiteralPath $tempExe -Destination $finalExe -Force

  Write-Host "Copied sherpa DLLs from: $sherpaDllDir"
  Write-Host "Copied ONNX Runtime DLLs from: $ortRuntime"
  Write-Host "Copied MinGW runtime DLLs: $($runtimeDllPaths -join ', ')"
  Write-Host "Build completed: $finalExe"
}
catch {
  if (Test-Path -LiteralPath $tempExe) {
    Remove-Item -LiteralPath $tempExe -Force -ErrorAction SilentlyContinue
  }
  Write-Error $_
  exit 1
}
finally {
  Pop-Location
}

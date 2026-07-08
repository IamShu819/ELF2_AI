$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
  .\voice-engine.exe
}
finally {
  Pop-Location
}

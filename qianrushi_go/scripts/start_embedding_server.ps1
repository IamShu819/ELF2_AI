$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Fail($message) {
  Write-Error $message
  exit 1
}

function EnvOrDefault($name, $defaultValue) {
  $value = [Environment]::GetEnvironmentVariable($name)
  if ([string]::IsNullOrWhiteSpace($value)) {
    return $defaultValue
  }
  return $value.Trim()
}

function Require-File($path, $description) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    Fail "$description not found: $path"
  }
  return (Resolve-Path -LiteralPath $path).Path
}

function Test-PortAvailable($hostName, [int]$port) {
  $listener = $null
  try {
    $address = [System.Net.IPAddress]::Any
    if ($hostName -ne "0.0.0.0" -and $hostName -ne "*") {
      $addresses = @([System.Net.Dns]::GetHostAddresses($hostName) | Where-Object { $_.AddressFamily -eq [System.Net.Sockets.AddressFamily]::InterNetwork })
      if ($addresses.Count -eq 0) {
        Fail "cannot resolve IPv4 listen host: $hostName"
      }
      $address = $addresses[0]
    }
    $listener = [System.Net.Sockets.TcpListener]::new($address, $port)
    $listener.Start()
    return $true
  }
  catch {
    return $false
  }
  finally {
    if ($null -ne $listener) {
      $listener.Stop()
    }
  }
}

$llamaServer = Require-File (EnvOrDefault "VOICE_EMBEDDING_LLAMA_SERVER" (EnvOrDefault "LLAMA_SERVER" "D:\Q\download\bin\llama-b8021\llama-server.exe")) "llama-server.exe"
$modelPath = Require-File (EnvOrDefault "VOICE_EMBEDDING_MODEL_PATH" "D:\Q\download\models\bge-small-zh-v1.5-q8_0.gguf") "bge-small-zh-v1.5 GGUF model"
$alias = EnvOrDefault "VOICE_EMBEDDING_MODEL" "bge-small-zh-v1.5"
$hostName = EnvOrDefault "VOICE_EMBEDDING_HOST" "0.0.0.0"
$portText = EnvOrDefault "VOICE_EMBEDDING_PORT" "8001"
[int]$port = 0
if (-not [int]::TryParse($portText, [ref]$port) -or $port -le 0 -or $port -gt 65535) {
  Fail "invalid VOICE_EMBEDDING_PORT: $portText"
}

if (-not (Test-PortAvailable $hostName $port)) {
  Fail "embedding server port is already in use or unavailable: ${hostName}:${port}"
}

Write-Host "Starting embedding server"
Write-Host "llama-server: $llamaServer"
Write-Host "model: $modelPath"
Write-Host "alias: $alias"
Write-Host "listen: ${hostName}:${port}"

& $llamaServer `
  --model $modelPath `
  --alias $alias `
  --host $hostName `
  --port $port `
  --embedding `
  --pooling cls

if ($LASTEXITCODE -ne 0) {
  Fail "llama-server exited with code $LASTEXITCODE"
}

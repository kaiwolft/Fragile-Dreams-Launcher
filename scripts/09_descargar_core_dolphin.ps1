# Fragile Dreams - Paso 9: descargar el core de Dolphin (libretro) para que el launcher sea el propio emulador
# Descarga de buildbot.libretro.com:
#   core\dolphin_libretro.dll                 (el emulador como biblioteca)
#   core\system\dolphin-emu\Sys\...           (archivos de sistema que el core necesita: GameSettings, fuentes, etc.)
# y deja core\core_info.txt con fecha y huellas SHA256. No toca nada del launcher actual ni de Dolphin-x64.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$core = Join-Path $root 'core'
$dl   = Join-Path $core 'descargas'
$sys  = Join-Path $core 'system'
$urlCore = 'https://buildbot.libretro.com/nightly/windows/x86_64/latest/dolphin_libretro.dll.zip'
$urlSys  = 'https://buildbot.libretro.com/assets/system/Dolphin.zip'
try {
  [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
  $ProgressPreference = 'SilentlyContinue'
  New-Item -ItemType Directory -Force -Path $dl, $sys | Out-Null
  $zc = Join-Path $dl 'dolphin_libretro.dll.zip'
  $zs = Join-Path $dl 'Dolphin.zip'
  Write-Host 'Descargando el core de Dolphin (unos 7 MB)...'
  Invoke-WebRequest -Uri $urlCore -OutFile $zc -UseBasicParsing
  Write-Host 'Descargando los archivos de sistema del core (unos 3 MB)...'
  Invoke-WebRequest -Uri $urlSys -OutFile $zs -UseBasicParsing
  Write-Host 'Descomprimiendo...'
  Expand-Archive -LiteralPath $zc -DestinationPath $core -Force
  Expand-Archive -LiteralPath $zs -DestinationPath $sys -Force
  $dll = Join-Path $core 'dolphin_libretro.dll'
  if (-not (Test-Path -LiteralPath $dll)) { throw 'No aparecio dolphin_libretro.dll dentro del zip.' }
  $sysDir = Get-ChildItem -LiteralPath $sys -Recurse -Directory -Filter 'Sys' | Select-Object -First 1
  if (-not $sysDir) { throw 'No aparecio la carpeta Sys dentro de Dolphin.zip.' }
  $info = @(
    'Core de Dolphin (libretro) descargado el ' + (Get-Date -Format 'yyyy-MM-dd HH:mm'),
    'Origen core: ' + $urlCore,
    'Origen sistema: ' + $urlSys,
    'dolphin_libretro.dll  ' + (Get-Item $dll).Length + ' bytes  SHA256 ' + (Get-FileHash $dll -Algorithm SHA256).Hash,
    'Carpeta Sys: ' + $sysDir.FullName
  )
  Set-Content -LiteralPath (Join-Path $core 'core_info.txt') -Value $info -Encoding ASCII
  Write-Host ''
  $info | ForEach-Object { Write-Host $_ }
  Write-Host ''
  Write-Host 'Listo. Avisale a Claude.' -ForegroundColor Green
} catch { Write-Host ('ERROR: ' + $_.Exception.Message) -ForegroundColor Red }

# Fragile Dreams - Paso 4: instalar el launcher (Electron) usando el motor del launcher de Pandora's Tower
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$lau  = Join-Path $root 'launcher'
$src  = Join-Path (Split-Path -Parent $root) "Proyecto Pandoras Tower\launcher"
Start-Transcript -Path (Join-Path $root 'logs\04_launcher.log') -Force | Out-Null
try {
  if (-not (Test-Path (Join-Path $src 'PandorasTower.exe'))) { throw "No encuentro el motor del launcher en $src" }
  if (Get-Process -Name FragileDreams -ErrorAction SilentlyContinue) { throw 'Cierra el launcher de Fragile Dreams primero.' }
  Write-Host "=== 1. Motor Electron desde $src"
  $n = 0
  Get-ChildItem $src -File | Where-Object { $_.Name -ne 'PandorasTower.exe' } | ForEach-Object { Copy-Item $_.FullName (Join-Path $lau $_.Name) -Force; $n++ }
  Copy-Item (Join-Path $src 'PandorasTower.exe') (Join-Path $lau 'FragileDreams.exe') -Force
  robocopy (Join-Path $src 'locales') (Join-Path $lau 'locales') /E /NFL /NDL /NJH /NJS /NP | Out-Null
  Get-ChildItem (Join-Path $src 'resources') | Where-Object { $_.Name -ne 'app' } | ForEach-Object {
    if ($_.PSIsContainer) { robocopy $_.FullName (Join-Path $lau "resources\$($_.Name)") /E /NFL /NDL /NJH /NJS /NP | Out-Null }
    else { Copy-Item $_.FullName (Join-Path $lau "resources\$($_.Name)") -Force } }
  Write-Host "  Archivos del motor: $n + FragileDreams.exe + locales"

  Write-Host "`n=== 2. Comprobando"
  foreach ($f in @('FragileDreams.exe', 'resources\app\main.js', 'resources\app\preload.js', 'resources\app\bridge.ps1', 'ui\index.html', 'ui\exit.html',
                   'ui\assets\bg.jpg', 'ui\assets\icon.ico', 'ui\fonts\Cinzel.ttf', 'data\parche_mando_eur.json')) {
    if (-not (Test-Path (Join-Path $lau $f))) { throw "Falta: launcher\$f" } }
  foreach ($f in @('Dolphin-x64\Dolphin.exe', 'juego\EU_ESPANOL\DATA\sys\boot.bin', 'juego\EU_ORIGINAL\DATA\sys\boot.bin', 'juego\dol\espanol.dol', 'juego\dol\original.dol')) {
    if (Test-Path (Join-Path $root $f)) { Write-Host "  OK  $f" } else { Write-Host "  FALTA  $f" -ForegroundColor Yellow } }
  $cfgFile = Join-Path $lau 'data\config.json'
  if (-not (Test-Path $cfgFile)) { '{ "text": "es", "voice": "ja", "ui": "es" }' | Set-Content $cfgFile -Encoding UTF8 }

  Write-Host "`n=== 3. Accesos directos"
  $ws = New-Object -ComObject WScript.Shell
  foreach ($dst in @((Join-Path $root 'FRAGILE DREAMS.lnk'), (Join-Path ([Environment]::GetFolderPath('Desktop')) 'Fragile Dreams.lnk'))) {
    $s = $ws.CreateShortcut($dst)
    $s.TargetPath = Join-Path $lau 'FragileDreams.exe'
    $s.WorkingDirectory = $lau
    $s.IconLocation = (Join-Path $lau 'ui\assets\icon.ico') + ',0'
    $s.Description = "Fragile Dreams - Dionixu's Launcher"
    $s.Save(); Write-Host "  $dst"
  }
  Write-Host "`nLISTO. Abre el juego con el acceso directo FRAGILE DREAMS."
} catch {
  Write-Host "`nERROR: $($_.Exception.Message)" -ForegroundColor Red
}
Stop-Transcript | Out-Null
Read-Host 'Presiona Enter para cerrar'

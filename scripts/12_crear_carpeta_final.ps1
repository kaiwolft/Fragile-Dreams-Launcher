# Fragile Dreams - Paso 12: carpeta final portatil (launcher + motor + Dolphin + juego) en
#   E:\Juegos de DIonis Con launcher personalizado\Fragile Dreams (Dionixu's Launcher)
# - Se copia todo (es otro disco). EU_ESPANOL y EU_ORIGINAL comparten casi todos los archivos: en el destino
#   se enlazan (enlaces duros) para no ocupar el doble.
# - No se copian: registros, cache de Chromium, volcados del motor, cache de shaders ni capturas.
# - Se conservan tu partida guardada, tus ajustes y tus logros.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$base = 'E:\Juegos de DIonis Con launcher personalizado'
$dst  = Join-Path $base "Fragile Dreams (Dionixu's Launcher)"
New-Item -ItemType Directory -Force -Path (Join-Path $root 'logs') | Out-Null
Start-Transcript -Path (Join-Path $root 'logs\12_carpeta_final.log') -Force | Out-Null
function RC($src, $to, [string[]]$xd = @(), [string[]]$xf = @()) {
  $a = @($src, $to, '/E', '/R:1', '/W:1', '/NFL', '/NDL', '/NJH', '/NJS', '/NP', '/MT:8')
  if ($xd.Count) { $a += '/XD'; $a += $xd }
  if ($xf.Count) { $a += '/XF'; $a += $xf }
  & robocopy @a | Out-Null
  if ($LASTEXITCODE -ge 8) { throw "robocopy fallo copiando $src (codigo $LASTEXITCODE)" }
}
try {
  foreach ($f in @('launcher\FragileDreams.exe', 'launcher\resources\app\main.js', 'motor\fd_motor.exe', 'core\dolphin_libretro.dll',
                   'Dolphin-x64\Dolphin.exe', 'juego\EU_ESPANOL\DATA\sys\boot.bin', 'juego\EU_ORIGINAL\DATA\sys\boot.bin', 'juego\dol\espanol.dol')) {
    if (-not (Test-Path (Join-Path $root $f))) { throw "Falta $f" } }
  if (Get-Process -Name FragileDreams, fd_motor, Dolphin -ErrorAction SilentlyContinue) { throw 'Cierra el launcher y el juego antes de continuar.' }
  if (-not (Test-Path 'E:\')) { throw 'No encuentro el disco E:' }
  $free = (Get-PSDrive E).Free
  Write-Host ("Destino: {0}`nEspacio libre en E: {1:N1} GB (se necesitan unos 5 GB)`n" -f $dst, ($free / 1GB))
  New-Item -ItemType Directory -Force -Path $dst | Out-Null

  Write-Host '=== 1. Launcher'
  $L = Join-Path $root 'launcher'
  RC $L (Join-Path $dst 'launcher') @((Join-Path $L 'data\chromium')) @('*.log', 'activos.txt', '*.bak')

  Write-Host '=== 2. Motor y core de Dolphin'
  New-Item -ItemType Directory -Force -Path (Join-Path $dst 'motor') | Out-Null
  Copy-Item (Join-Path $root 'motor\fd_motor.exe') (Join-Path $dst 'motor\fd_motor.exe') -Force
  RC (Join-Path $root 'core') (Join-Path $dst 'core') @((Join-Path $root 'core\descargas'))

  Write-Host '=== 3. Dolphin (configuracion, partida guardada, texturas de botones)'
  $dol = Join-Path $root 'Dolphin-x64'
  RC $dol (Join-Path $dst 'Dolphin-x64') @((Join-Path $dol 'User\Cache'), (Join-Path $dol 'User\Logs'), (Join-Path $dol 'User\Dump'),
     (Join-Path $dol 'User\ScreenShots'), (Join-Path $dol 'User\Shaders'), (Join-Path $dol 'User\StateSaves'))

  Write-Host '=== 4. Juego (parche espanol integrado + version original)'
  $J = Join-Path $root 'juego'; $JD = Join-Path $dst 'juego'
  RC (Join-Path $J 'dol') (Join-Path $JD 'dol')
  RC (Join-Path $J 'EU_ORIGINAL') (Join-Path $JD 'EU_ORIGINAL')
  # EU_ESPANOL: enlaza lo que es igual a EU_ORIGINAL (mismo tamano y fecha) y copia lo que cambia (parche y main.dol)
  $srcES = Join-Path $J 'EU_ESPANOL'; $n = 0; $l = 0
  $files = Get-ChildItem -LiteralPath $srcES -Recurse -File -Force
  foreach ($f in $files) {
    $rel = $f.FullName.Substring($srcES.Length).TrimStart('\')
    $out = Join-Path $JD ('EU_ESPANOL\' + $rel)
    $dir = Split-Path -Parent $out; if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Force }
    $o = Get-Item -LiteralPath (Join-Path $J ('EU_ORIGINAL\' + $rel)) -ErrorAction SilentlyContinue
    $done = $false
    if ($o -and $o.Length -eq $f.Length -and $o.LastWriteTimeUtc -eq $f.LastWriteTimeUtc -and $rel -ne 'DATA\sys\main.dol') {
      try { New-Item -ItemType HardLink -Path $out -Target (Join-Path $JD ('EU_ORIGINAL\' + $rel)) -ErrorAction Stop | Out-Null; $l++; $done = $true } catch {}
    }
    if (-not $done) { Copy-Item -LiteralPath $f.FullName -Destination $out -Force }
    $n++; if ($n % 300 -eq 0) { Write-Progress -Activity 'EU_ESPANOL' -Status "$n / $($files.Count)" -PercentComplete ($n * 100 / $files.Count) }
  }
  Write-Progress -Activity 'EU_ESPANOL' -Completed
  Write-Host "  EU_ESPANOL: $n archivos ($l enlazados con EU_ORIGINAL)"

  Write-Host "`n=== 5. Accesos directos, LEEME y arte de Steam"
  $act = @'
@echo off
set "FDROOT=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$r=$env:FDROOT.TrimEnd('\'); $l=Join-Path $r 'launcher'; $ws=New-Object -ComObject WScript.Shell; foreach($d in @((Join-Path $r 'FRAGILE DREAMS.lnk'),(Join-Path ([Environment]::GetFolderPath('Desktop')) 'Fragile Dreams.lnk'))){ $s=$ws.CreateShortcut($d); $s.TargetPath=Join-Path $l 'FragileDreams.exe'; $s.WorkingDirectory=$l; $s.IconLocation=(Join-Path $l 'ui\assets\icon.ico')+',0'; $s.Description='Fragile Dreams - Dionixu''s Launcher'; $s.Save() }; Write-Host 'Accesos directos actualizados.'"
pause
'@
  Set-Content -LiteralPath (Join-Path $dst 'ACTUALIZAR ACCESOS DIRECTOS.bat') -Value $act -Encoding ASCII
  $leeme = @"
FRAGILE DREAMS: FAREWELL RUINS OF THE MOON - DIONIXU'S LAUNCHER
===============================================================
Abrir el juego: FRAGILE DREAMS (acceso directo) o launcher\FragileDreams.exe

Esta carpeta es portatil: muevela a donde quieras (otro disco, otra PC).
Despues de moverla, ejecuta "ACTUALIZAR ACCESOS DIRECTOS.bat" y, si lo tienes en Steam,
vuelve a ejecutar AGREGAR_A_STEAM.bat (en la carpeta de arriba).

Contenido
  launcher\     Launcher (ajustes en launcher\data\config.json, logros en launcher\data\logros)
  motor\        Motor propio (camara con stick/raton, 360 grados, 60 FPS)
  core\         Nucleo de Dolphin que usa el motor
  Dolphin-x64\  Partida guardada (User\Wii) y configuracion
  juego\        Version europea R2GP99: EU_ESPANOL (parche espanol 1.1 integrado) y EU_ORIGINAL

En el juego: F1 o L3 + R3 = menu de pausa (volver al launcher).
Los logros se guardan al guardar la partida en el juego.
"@
  Set-Content -LiteralPath (Join-Path $dst 'LEEME.txt') -Value $leeme -Encoding UTF8
  $ws = New-Object -ComObject WScript.Shell
  $lau = Join-Path $dst 'launcher'
  foreach ($d in @((Join-Path $dst 'FRAGILE DREAMS.lnk'), (Join-Path ([Environment]::GetFolderPath('Desktop')) 'Fragile Dreams.lnk'))) {
    $s = $ws.CreateShortcut($d); $s.TargetPath = Join-Path $lau 'FragileDreams.exe'; $s.WorkingDirectory = $lau
    $s.IconLocation = (Join-Path $lau 'ui\assets\icon.ico') + ',0'; $s.Description = "Fragile Dreams - Dionixu's Launcher"; $s.Save()
    Write-Host "  $d" }

  $size = (Get-ChildItem -LiteralPath $dst -Recurse -File -Force | Measure-Object Length -Sum).Sum
  Write-Host ("`nLISTO: {0}`nTamano (contando los enlaces dos veces): {1:N1} GB" -f $dst, ($size / 1GB))
  Write-Host "`nPara agregarlo a Steam con su arte: ejecuta '$base\AGREGAR_A_STEAM.bat'"
} catch {
  Write-Host "`nERROR: $($_.Exception.Message)" -ForegroundColor Red
}
Stop-Transcript | Out-Null
Read-Host 'Presiona Enter para cerrar'

# Fragile Dreams - Paso 2: carpetas del juego (original y con parche espanol), ejecutables base, controles y codigos
# - juego\EU_ORIGINAL : disco europeo tal cual (textos ingles/frances/aleman/japones)
# - juego\EU_ESPANOL  : mismo disco + traduccion al castellano 1.1 (sustituye el texto ingles)
# Todo con enlaces duros (no ocupa espacio extra). main.dol NO se enlaza: el launcher lo regenera en cada arranque
# a partir de juego\dol\*.dol con el idioma de texto y de voz elegidos.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$logs = Join-Path $root 'logs'
New-Item -ItemType Directory -Force -Path $logs | Out-Null
Start-Transcript -Path (Join-Path $logs '02_preparar.log') -Force | Out-Null

function Link-Tree($src, $to) {
  $n = 0; $l = 0
  $files = Get-ChildItem -LiteralPath $src -Recurse -File -Force
  foreach ($f in $files) {
    $rel = $f.FullName.Substring($src.Length).TrimStart('\')
    $out = Join-Path $to $rel
    $dir = Split-Path -Parent $out
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Force }
    try { New-Item -ItemType HardLink -Path $out -Target $f.FullName -ErrorAction Stop | Out-Null; $l++ }
    catch { Copy-Item -LiteralPath $f.FullName -Destination $out -Force }
    $n++
    if ($n % 200 -eq 0) { Write-Progress -Activity $to -Status "$n / $($files.Count)" -PercentComplete ($n * 100 / $files.Count) }
  }
  Write-Progress -Activity $to -Completed
  Write-Host ("  {0} archivos ({1} enlazados)" -f $n, $l)
}

try {
  # 0. El paso 1 extrajo el disco europeo en 'extraido\US': se renombra a EU
  $ex = Join-Path $root 'extraido'
  if ((Test-Path (Join-Path $ex 'US\DATA')) -and -not (Test-Path (Join-Path $ex 'EU'))) { Rename-Item (Join-Path $ex 'US') 'EU'; Write-Host 'extraido\US -> extraido\EU' }
  if ((Test-Path (Join-Path $logs 'lista_US.tsv')) -and -not (Test-Path (Join-Path $logs 'lista_EU.tsv'))) { Rename-Item (Join-Path $logs 'lista_US.tsv') 'lista_EU.tsv' }
  $pc = Join-Path $root 'para_claude'
  if ((Test-Path (Join-Path $pc 'US')) -and -not (Test-Path (Join-Path $pc 'EU'))) { Rename-Item (Join-Path $pc 'US') 'EU' }
  $data = Join-Path $ex 'EU\DATA'
  if (-not (Test-Path (Join-Path $data 'sys\main.dol'))) { throw "No encuentro el juego extraido en $data (ejecuta el paso 1)" }
  $hdr = [IO.File]::ReadAllBytes((Join-Path $data 'sys\boot.bin'))
  $id = [Text.Encoding]::ASCII.GetString($hdr, 0, 6)
  if ($id -ne 'R2GP99') { throw "El juego extraido es $id, se necesita la version europea R2GP99" }

  $tradu = Join-Path $root 'FragileDreamsESP11\FragileESP\tradu'
  if (-not (Test-Path (Join-Path $tradu 'main.dol'))) { throw "No encuentro el parche en $tradu" }

  $juego = Join-Path $root 'juego'
  $dolDir = Join-Path $juego 'dol'
  New-Item -ItemType Directory -Force -Path $dolDir | Out-Null

  Write-Host '=== 1. juego\EU_ORIGINAL'
  $orig = Join-Path $juego 'EU_ORIGINAL\DATA'
  Link-Tree $data $orig
  Write-Host '=== 2. juego\EU_ESPANOL'
  $esp = Join-Path $juego 'EU_ESPANOL\DATA'
  Link-Tree $data $esp
  $n = 0
  Get-ChildItem $tradu -File | Where-Object { $_.Name -ne 'main.dol' } | ForEach-Object {
    $dst = Join-Path $esp ('files\' + $_.Name)
    if (Test-Path -LiteralPath $dst) { Remove-Item -LiteralPath $dst -Force }   # rompe el enlace antes de sustituir
    Copy-Item -LiteralPath $_.FullName -Destination $dst; $n++ }
  Write-Host "  Archivos de la traduccion aplicados: $n"

  Write-Host '=== 3. Ejecutables base'
  Copy-Item (Join-Path $data 'sys\main.dol') (Join-Path $dolDir 'original.dol') -Force
  Copy-Item (Join-Path $tradu 'main.dol') (Join-Path $dolDir 'espanol.dol') -Force
  foreach ($d in @($orig, $esp)) {
    $m = Join-Path $d 'sys\main.dol'
    if (Test-Path -LiteralPath $m) { Remove-Item -LiteralPath $m -Force }   # que no quede enlazado al extraido
  }
  Copy-Item (Join-Path $dolDir 'original.dol') (Join-Path $orig 'sys\main.dol')
  Copy-Item (Join-Path $dolDir 'espanol.dol') (Join-Path $esp 'sys\main.dol')
  Write-Host '  juego\dol\original.dol y espanol.dol'

  Write-Host '=== 4. Dolphin: codigos del mando clasico y configuracion'
  $dol  = Join-Path $root 'Dolphin-x64'
  $user = Join-Path $dol 'User'
  if (-not (Test-Path (Join-Path $dol 'portable.txt'))) { New-Item -ItemType File -Path (Join-Path $dol 'portable.txt') | Out-Null }
  $txt = Get-Content (Join-Path $root 'codigos\classic_controller_vague_rant.txt')
  $eur = @(); $on = $false
  foreach ($line in $txt) {
    if ($line -eq '[EUR]') { $on = $true; continue }
    if ($line.StartsWith('[')) { $on = $false; continue }
    if ($on -and $line -match '^[0-9A-F]{8} [0-9A-F]{8}$') { $eur += $line } }
  if ($eur.Count -ne 85) { throw "Codigo EUR incompleto ($($eur.Count) lineas)" }
  $gs = Join-Path $user 'GameSettings'
  New-Item -ItemType Directory -Force -Path $gs | Out-Null
  $ini = @('[Gecko]', '$Classic Controller Support [Vague Rant]') + $eur + @('[Gecko_Enabled]', '$Classic Controller Support [Vague Rant]')
  Set-Content (Join-Path $gs 'R2GP99.ini') $ini -Encoding ASCII
  Write-Host "  GameSettings\R2GP99.ini ($($eur.Count) lineas)"
  Write-Host "`nLISTO. Ahora prueba con scripts\03_PROBAR.bat"
} catch {
  Write-Host "`nERROR: $($_.Exception.Message)" -ForegroundColor Red
}
Stop-Transcript | Out-Null
Read-Host 'Presiona Enter para cerrar'

# Fragile Dreams - Paso 1: extraer el disco USA, inventario y copia de analisis para Claude
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot      # carpeta del proyecto
$dol  = Join-Path $root 'Dolphin-x64'
$tool = Join-Path $dol 'DolphinTool.exe'
$logs = Join-Path $root 'logs'
$out  = Join-Path $root 'extraido\US'
$pc   = Join-Path $root 'para_claude'
New-Item -ItemType Directory -Force -Path $logs, $out, $pc | Out-Null
Start-Transcript -Path (Join-Path $logs '01_extraer.log') -Force | Out-Null

# Dolphin portable: toda la configuracion queda en Dolphin-x64\User
$pt = Join-Path $dol 'portable.txt'
if (-not (Test-Path $pt)) { New-Item -ItemType File -Path $pt | Out-Null; Write-Host 'Dolphin en modo portable (portable.txt creado).' }

$iso = Get-ChildItem -LiteralPath $root -Filter '*.rvz' | Select-Object -First 1
if (-not $iso) { Write-Host 'ERROR: no encuentro el .rvz del juego en la carpeta del proyecto' }
else {
  $drive = (Get-Item $root).PSDrive
  Write-Host ("Espacio libre en {0}: {1:N1} GB (se necesitan ~5 GB)" -f $drive.Name, ($drive.Free/1GB))
  Write-Host "=== Disco: $($iso.Name)"
  & $tool header -i $iso.FullName
  if (Test-Path (Join-Path $out 'DATA')) { Write-Host 'Ya extraido, lo salto.' }
  else {
    Write-Host 'Extrayendo (puede tardar varios minutos)...'
    & $tool extract -i $iso.FullName -o $out
    Write-Host "Codigo de salida: $LASTEXITCODE"
  }
  # Inventario: ruta relativa, tamano y MD5
  $lista = Join-Path $logs 'lista_US.tsv'
  Get-ChildItem -LiteralPath $out -Recurse -File | ForEach-Object {
    $rel = $_.FullName.Substring($out.Length + 1)
    $md5 = (Get-FileHash -LiteralPath $_.FullName -Algorithm MD5).Hash
    "{0}`t{1}`t{2}" -f $rel, $_.Length, $md5
  } | Set-Content -Path $lista -Encoding UTF8
  Write-Host "Inventario: $((Get-Content $lista).Count) archivos -> $lista"

  # Copia para analisis: ejecutable, indice, archivos que toca el parche y los pequenos (< 2 MB)
  $parche = @{}
  Get-ChildItem (Join-Path $root 'FragileDreamsESP11\FragileESP\tradu') -File -ErrorAction SilentlyContinue | ForEach-Object { $parche[$_.Name] = $true }
  $n = 0; $mb = 0
  Get-ChildItem -LiteralPath $out -Recurse -File | Where-Object {
      $parche.ContainsKey($_.Name) -or $_.Name -in @('main.dol','index.bin','boot.bin','bi2.bin','apploader.img','fst.bin') -or $_.Length -lt 2MB } | ForEach-Object {
    $dst = Join-Path $pc ('US\' + $_.FullName.Substring($out.Length + 1))
    $dir = Split-Path -Parent $dst
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    Copy-Item -LiteralPath $_.FullName -Destination $dst -Force
    $n++; $mb += $_.Length
  }
  Write-Host ("Copiados para Claude: {0} archivos ({1:N1} MB) -> {2}" -f $n, ($mb/1MB), $pc)
}
Write-Host "`nLISTO. Ya puedes avisarle a Claude."
Stop-Transcript | Out-Null
Read-Host 'Presiona Enter para cerrar'

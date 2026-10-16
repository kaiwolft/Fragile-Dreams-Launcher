# Fragile Dreams - Paso 5: sacar main.dol de la version USA (los logros de RetroAchievements estan hechos para USA;
# con este archivo Claude traduce sus direcciones de memoria a la version europea que usamos)
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$tool = Join-Path $root 'Dolphin-x64\DolphinTool.exe'
$tmp  = Join-Path $root 'extraido\US_tmp'
$pc   = Join-Path $root 'para_claude\US_REF'
Start-Transcript -Path (Join-Path $root 'logs\05_dol_usa.log') -Force | Out-Null
$iso = Get-ChildItem -LiteralPath $root -Filter '*(USA)*.rvz' | Select-Object -First 1
if (-not $iso) { Write-Host 'ERROR: no encuentro el .rvz de la version USA' -ForegroundColor Red }
else {
  & $tool header -i $iso.FullName
  New-Item -ItemType Directory -Force -Path $tmp, $pc | Out-Null
  Write-Host 'Extrayendo solo el ejecutable...'
  & $tool extract -i $iso.FullName -o $tmp -s 'sys/main.dol' 2>&1 | Out-Host
  $dol = Get-ChildItem -LiteralPath $tmp -Recurse -Filter 'main.dol' -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $dol) {
    Write-Host 'Extrayendo la particion completa (unos minutos)...'
    & $tool extract -i $iso.FullName -o $tmp -p DATA 2>&1 | Select-Object -Last 3 | Out-Host
    $dol = Get-ChildItem -LiteralPath $tmp -Recurse -Filter 'main.dol' -ErrorAction SilentlyContinue | Select-Object -First 1
  }
  if ($dol) {
    Copy-Item $dol.FullName (Join-Path $pc 'main.dol') -Force
    Write-Host "main.dol USA copiado ($($dol.Length) bytes)" -ForegroundColor Green
    Remove-Item -LiteralPath $tmp -Recurse -Force
  } else { Write-Host 'ERROR: no se pudo extraer main.dol' -ForegroundColor Red }
}
Write-Host "`nLISTO. Ya puedes avisarle a Claude."
Stop-Transcript | Out-Null
Read-Host 'Presiona Enter para cerrar'

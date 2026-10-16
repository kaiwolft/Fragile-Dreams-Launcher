# Fragile Dreams - Paso 7: activar el volcado de texturas de Dolphin
# Dolphin guarda cada textura que el juego dibuja en Dolphin-x64\User\Dump\Textures\R2GP99 (con el nombre-codigo
# que luego usa para reemplazarla). Con eso Claude hace el paquete HD con los botones de Xbox.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$gfx  = Join-Path $root 'Dolphin-x64\User\Config\GFX.ini'
$dump = Join-Path $root 'Dolphin-x64\User\Dump\Textures\R2GP99'

function Set-Ini($file, $section, $key, $val) {
  $lines = @(); if (Test-Path -LiteralPath $file) { $lines = @(Get-Content -LiteralPath $file) }
  $start = -1
  for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i].Trim() -eq ('[' + $section + ']')) { $start = $i; break } }
  if ($start -lt 0) { $lines += ''; $lines += ('[' + $section + ']'); $start = $lines.Count - 1 }
  $end = $lines.Count
  for ($i = $start + 1; $i -lt $lines.Count; $i++) { if ($lines[$i].Trim().StartsWith('[')) { $end = $i; break } }
  $done = $false
  for ($i = $start + 1; $i -lt $end; $i++) { if ($lines[$i].Split('=')[0].Trim() -eq $key) { $lines[$i] = $key + ' = ' + $val; $done = $true } }
  if (-not $done) { $before = @($lines[0..($end - 1)]); $after = @(); if ($end -lt $lines.Count) { $after = @($lines[$end..($lines.Count - 1)]) }; $lines = $before + @($key + ' = ' + $val) + $after }
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $file) | Out-Null
  Set-Content -LiteralPath $file -Value $lines -Encoding ASCII
}

try {
  Set-Ini $gfx 'Settings' 'DumpTextures' 'True'
  Set-Ini $gfx 'Settings' 'DumpBaseTextures' 'True'
  Set-Ini $gfx 'Settings' 'DumpMipTextures' 'False'
  Set-Ini $gfx 'Settings' 'HiresTextures' 'False'
  if (Test-Path -LiteralPath $dump) {
    $old = $dump + '_anterior_' + (Get-Date -Format 'yyyyMMdd_HHmmss')
    Move-Item -LiteralPath $dump -Destination $old
    Write-Host ('Habia un volcado anterior; se movio a ' + $old)
  }
  Write-Host ''
  Write-Host 'Volcado de texturas ACTIVADO.' -ForegroundColor Green
  Write-Host ''
  Write-Host 'Ahora abre el juego desde el launcher y pasa por los sitios donde salen botones en pantalla:'
  Write-Host '  - menu de titulo y seleccion de partida'
  Write-Host '  - el principio del juego (tutoriales con iconos de botones)'
  Write-Host '  - menu de pausa / inventario / mapa / opciones'
  Write-Host '  - un dialogo, un objeto recogido y un combate'
  Write-Host 'Con 10-15 minutos basta. Al terminar, cierra el juego y ejecuta 08_EMPAQUETAR_TEXTURAS.'
} catch { Write-Host ('ERROR: ' + $_.Exception.Message) -ForegroundColor Red }

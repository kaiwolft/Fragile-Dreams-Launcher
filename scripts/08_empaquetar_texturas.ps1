# Fragile Dreams - Paso 8: desactivar el volcado y empaquetarlo en para_claude\texturas para que Claude lo analice
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$gfx  = Join-Path $root 'Dolphin-x64\User\Config\GFX.ini'
$dump = Join-Path $root 'Dolphin-x64\User\Dump\Textures\R2GP99'
$out  = Join-Path $root 'para_claude\texturas'

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
  if (Get-Process Dolphin -ErrorAction SilentlyContinue) { throw 'Cierra el juego primero.' }
  Set-Ini $gfx 'Settings' 'DumpTextures' 'False'
  Write-Host 'Volcado de texturas desactivado.'
  if (-not (Test-Path -LiteralPath $dump)) { throw ('No hay volcado en ' + $dump + '. Ejecuta 07 y juega un rato primero.') }
  $files = @(Get-ChildItem -LiteralPath $dump -Recurse -File -Filter '*.png')
  if ($files.Count -eq 0) { throw 'El volcado esta vacio.' }
  $total = ($files | Measure-Object Length -Sum).Sum
  Write-Host ('Texturas: ' + $files.Count + ' (' + [math]::Round($total / 1MB) + ' MB)')
  New-Item -ItemType Directory -Force -Path $out | Out-Null
  Get-ChildItem -LiteralPath $out -Filter 'volcado_*.zip' | Remove-Item -Force
  Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
  # partes de hasta ~300 MB (limite de lo que Claude puede recibir por archivo)
  $part = 0; $size = 0; $zip = $null
  foreach ($f in ($files | Sort-Object Name)) {
    if ($zip -eq $null -or $size -gt 300MB) {
      if ($zip) { $zip.Dispose() }
      $part++; $size = 0
      $zip = [IO.Compression.ZipFile]::Open((Join-Path $out ('volcado_' + $part + '.zip')), 'Create')
    }
    [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $f.FullName, $f.Name, 'Fastest')
    $size += $f.Length
  }
  if ($zip) { $zip.Dispose() }
  Write-Host ''
  Write-Host ('Listo: ' + $part + ' archivo(s) volcado_N.zip en ' + $out) -ForegroundColor Green
  Write-Host 'Avisale a Claude.'
} catch { Write-Host ('ERROR: ' + $_.Exception.Message) -ForegroundColor Red }

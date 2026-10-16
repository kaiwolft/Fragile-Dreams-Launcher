# Cambia el nombre que muestra Windows (Administrador de tareas, propiedades del archivo) de FragileDreams.exe:
# el ejecutable venía del launcher de The Last Story. Mismo largo de texto, se escribe en su sitio.
$ErrorActionPreference = 'Stop'
function Hex([string]$h) { $r = New-Object byte[] ($h.Length / 2); for ($i = 0; $i -lt $r.Length; $i++) { $r[$i] = [Convert]::ToByte($h.Substring($i * 2, 2), 16) }; return ,$r }
try {
  $root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
  $exe = Join-Path $root 'launcher\FragileDreams.exe'
  if (Get-Process FragileDreams -ErrorAction SilentlyContinue) { throw 'Cierra el launcher primero (y revisa en el Administrador de tareas que no quede abierto).' }
  $ok = 'C17F3275C2E892944EEDD206951290A0'
  if ((Get-FileHash $exe -Algorithm MD5).Hash -eq $ok) { Write-Host 'El nombre ya estaba cambiado.'; return }
  $patches = @(
    @(244604632,'4700690074004800750062002C00200049006E0063002E00','440069006F006E0069007800750000000000000000000000'),
    @(244604700,'54006800650020004C006100730074002000530074006F0072007900','460072006100670069006C006500200044007200650061006D007300'),
    @(244605092,'54006800650020004C006100730074002000530074006F0072007900','460072006100670069006C006500200044007200650061006D007300'),
    @(244605024,'5400680065004C00610073007400530074006F00720079002E00650078006500','460072006100670069006C00650044007200650061006D007300000000000000')
  )
  $fs = [IO.File]::Open($exe, 'Open', 'ReadWrite')
  try {
    foreach ($p in $patches) {
      $old = Hex($p[1]); $new = Hex($p[2])
      $cur = New-Object byte[] $old.Length; [void]$fs.Seek($p[0], 'Begin'); [void]$fs.Read($cur, 0, $cur.Length)
      if ([BitConverter]::ToString($cur) -ne [BitConverter]::ToString($old)) { throw ('FragileDreams.exe no es la version esperada en ' + $p[0]) }
    }
    foreach ($p in $patches) { $new = Hex($p[2]); [void]$fs.Seek($p[0], 'Begin'); $fs.Write($new, 0, $new.Length) }
  } finally { $fs.Close() }
  $h = (Get-FileHash $exe -Algorithm MD5).Hash
  if ($h -ne $ok) { Write-Host ('Aviso: el resultado no coincide con el esperado (' + $h + '), pero los textos se cambiaron.') -ForegroundColor Yellow }
  (Get-Item $exe).LastWriteTime = Get-Date
  Write-Host 'Listo: ahora se llama "Fragile Dreams" en el Administrador de tareas.' -ForegroundColor Green
} catch { Write-Host ('ERROR: ' + $_.Exception.Message) -ForegroundColor Red }

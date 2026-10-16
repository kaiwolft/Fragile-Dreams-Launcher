# Fragile Dreams - Paso 3 (prueba): elegir idioma de texto y de voz, aplicar controles y arrancar Dolphin
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dol  = Join-Path $root 'Dolphin-x64'
$cfg  = Join-Path $dol 'User\Config'
Start-Transcript -Path (Join-Path $root 'logs\03_probar.log') -Force | Out-Null

# Idioma en memoria del juego (0x80326208): +0x10 texto (0 japones, 1 ingles, 3 frances, 5 aleman), +0x14 voz.
# Se fuerzan los valores parcheando las instrucciones que los escriben (arranque, carga de opciones y menu de titulo).
function Patch-Dol($src, $dst, [int]$T, [int]$V) {
  $b = [IO.File]::ReadAllBytes($src)
  $h = { param([string]$x) [Convert]::ToUInt32($x, 16) }   # PowerShell 5 lee 0x8xxxxxxx como negativo
  $put = { param([string]$addr, [uint32]$word, [string]$expect)
    $o = [int]((& $h $addr) - (& $h '80004060'))   # seccion de texto 1: 0x80006820 <-> archivo 0x27C0
    $cur = ([uint32]$b[$o] -shl 24) -bor ([uint32]$b[$o+1] -shl 16) -bor ([uint32]$b[$o+2] -shl 8) -bor [uint32]$b[$o+3]
    if ($cur -ne (& $h $expect)) { throw ("main.dol inesperado en {0}: {1:X8}" -f $addr, $cur) }
    $b[$o] = [byte](($word -shr 24) -band 0xFF); $b[$o+1] = [byte](($word -shr 16) -band 0xFF)
    $b[$o+2] = [byte](($word -shr 8) -band 0xFF); $b[$o+3] = [byte]($word -band 0xFF) }
  & $put '80056AEC' (& $h '48000028') '2C000000'             # arranque: saltar la comprobacion del idioma del Wii
  & $put '80056B14' ((& $h '38000000') + $T) '38000001'      # arranque: texto = T
  & $put '80056B20' ((& $h '38600000') + $V) '38600001'      # arranque: voz = V
  & $put '801A0618' ((& $h '38000000') + $T) '38000001'      # opciones guardadas: texto = T
  & $put '801A0628' ((& $h '38000000') + $T) '7C03002E'
  & $put '801A05E8' ((& $h '38E00000') + $V) '55070FFE'      # opciones guardadas: voz = V
  & $put '801A51A4' ((& $h '38000000') + $T) '801E0028'      # menu de titulo: texto = T
  # Mando clasico (Vague Rant) integrado en el ejecutable: 6 instrucciones + rutinas escritas sobre funciones
  # muertas del propio juego (0x8025E590), dentro de la seccion de codigo que ya se carga siempre.
  if ($script:Mando -eq 'dol') {
    $cc = Get-Content -Raw (Join-Path $root 'codigos\parche_mando_eur.json') | ConvertFrom-Json
    foreach ($w in $cc.writes) { & $put $w.addr (& $h $w.value) $w.expect }
    $o = [int]((& $h $cc.cave_addr) - (& $h '80004060'))
    $n = [int]($cc.cave.Length / 2)
    $md5 = [BitConverter]::ToString([Security.Cryptography.MD5]::Create().ComputeHash($b, $o, $n)).Replace('-', '').ToLower()
    if ($md5 -ne $cc.cave_expect_md5) { throw "main.dol inesperado en la zona libre ($md5)" }
    for ($i = 0; $i -lt $n; $i++) { $b[$o + $i] = [Convert]::ToByte($cc.cave.Substring(2 * $i, 2), 16) }
  }
  if (Test-Path -LiteralPath $dst) { Remove-Item -LiteralPath $dst -Force }
  [IO.File]::WriteAllBytes($dst, $b)
}

try {
  Write-Host "TEXTO:  1 Espanol   2 Ingles   3 Frances   4 Aleman   5 Japones"
  $t = Read-Host 'Elige texto'
  Write-Host "VOZ:    1 Ingles    2 Japones"
  $v = Read-Host 'Elige voz'
  Write-Host "MANDO:  1 Integrado en el juego (recomendado)   2 Trucos Gecko de Dolphin"
  $script:Mando = if ((Read-Host 'Elige metodo del mando') -eq '2') { 'gecko' } else { 'dol' }
  Write-Host "CONTROL: 1 Mando (Xbox)   2 Teclado y raton"
  $pc = (Read-Host 'Elige control') -eq '2'
  $map = @{ '1' = @('EU_ESPANOL', 'espanol', 1); '2' = @('EU_ORIGINAL', 'original', 1); '3' = @('EU_ORIGINAL', 'original', 3);
            '4' = @('EU_ORIGINAL', 'original', 5); '5' = @('EU_ORIGINAL', 'original', 0) }
  if (-not $map.ContainsKey($t)) { $t = '1' }
  $sel = $map[$t]; $V = if ($v -eq '2') { 0 } else { 1 }
  $game = Join-Path $root ('juego\' + $sel[0])
  $main = Join-Path $game 'DATA\sys\main.dol'
  Patch-Dol (Join-Path $root ('juego\dol\' + $sel[1] + '.dol')) $main $sel[2] $V
  Write-Host ("Juego: {0}  texto={1}  voz={2}" -f $sel[0], $sel[2], $V)

  # Controles: mando clasico emulado (con el codigo de Vague Rant) <- mando Xbox + teclado y raton a la vez
  New-Item -ItemType Directory -Force -Path $cfg | Out-Null
  $K = 'DInput/0/Keyboard Mouse'
  function kb { ($args | ForEach-Object { '`' + $K + ':' + $_ + '`' }) -join ' | ' }
@"
[Wiimote1]
Device = XInput/0/Gamepad
Extension = Classic
Classic/Buttons/A = ``Button A`` | $(kb 'Click 0' 'RETURN' 'E')
Classic/Buttons/B = ``Button B`` | $(kb 'ESCAPE' 'BACK' 'Q')
Classic/Buttons/X = ``Button Y`` | $(kb 'F')
Classic/Buttons/Y = ``Button X`` | $(kb 'SPACE')
Classic/Buttons/ZL = ``Shoulder L`` | ``Thumb R`` | $(kb 'Click 2' 'R')
Classic/Buttons/ZR = ``Trigger L`` | $(kb 'Click 1' 'V')
Classic/Buttons/+ = ``Start`` | $(kb 'EQUALS' 'ADD')
Classic/Buttons/- = ``Back`` | $(kb 'MINUS' 'SUBTRACT')
Classic/Buttons/Home = 
Classic/Triggers/L = ``Shoulder R`` | ``Thumb L`` | $(kb 'LCONTROL' 'C')
Classic/Triggers/R = ``Trigger R`` | $(kb 'LSHIFT')
Classic/Triggers/L-Analog = ``Shoulder R`` | ``Thumb L`` | $(kb 'LCONTROL' 'C')
Classic/Triggers/R-Analog = ``Trigger R`` | $(kb 'LSHIFT')
Classic/D-Pad/Up = ``Pad N`` | $(kb 'UP' '1')
Classic/D-Pad/Down = ``Pad S`` | $(kb 'DOWN' '2')
Classic/D-Pad/Left = ``Pad W`` | $(kb 'LEFT' '3')
Classic/D-Pad/Right = ``Pad E`` | $(kb 'RIGHT' '4')
Classic/Left Stick/Up = deadzone(``Left Y+``, 0.15) | $(kb 'W')
Classic/Left Stick/Down = deadzone(``Left Y-``, 0.15) | $(kb 'S')
Classic/Left Stick/Left = deadzone(``Left X-``, 0.15) | $(kb 'A')
Classic/Left Stick/Right = deadzone(``Left X+``, 0.15) | $(kb 'D')
Classic/Left Stick/Dead Zone = 0.
Classic/Right Stick/Up = $(if ($pc) { '`' + $K + ':Cursor Y-`' } else { 'deadzone(`Right Y+`, 0.15)' }) | $(kb 'I')
Classic/Right Stick/Down = $(if ($pc) { '`' + $K + ':Cursor Y+`' } else { 'deadzone(`Right Y-`, 0.15)' }) | $(kb 'K')
Classic/Right Stick/Left = $(if ($pc) { '`' + $K + ':Cursor X-`' } else { 'deadzone(`Right X-`, 0.15)' }) | $(kb 'J')
Classic/Right Stick/Right = $(if ($pc) { '`' + $K + ':Cursor X+`' } else { 'deadzone(`Right X+`, 0.15)' }) | $(kb 'L')
Classic/Right Stick/Dead Zone = 0.
IR/Up = $(if ($pc) { '`' + $K + ':Cursor Y-`' } else { 'deadzone(`Right Y+`, 0.15)' })
IR/Down = $(if ($pc) { '`' + $K + ':Cursor Y+`' } else { 'deadzone(`Right Y-`, 0.15)' })
IR/Left = $(if ($pc) { '`' + $K + ':Cursor X-`' } else { 'deadzone(`Right X-`, 0.15)' })
IR/Right = $(if ($pc) { '`' + $K + ':Cursor X+`' } else { 'deadzone(`Right X+`, 0.15)' })
IR/Relative Input = $(if ($pc) { 'False' } else { 'True' })
IR/Auto-Hide = False
Source = 1
[Wiimote2]
Source = 0
[Wiimote3]
Source = 0
[Wiimote4]
Source = 0
[BalanceBoard]
Source = 0
"@ | Set-Content (Join-Path $cfg 'WiimoteNew.ini') -Encoding ASCII
@"
[Hotkeys]
Device = $K
General/Toggle Fullscreen = @(Alt+RETURN)
General/Take Screenshot = F9
General/Stop = F10
"@ | Set-Content (Join-Path $cfg 'Hotkeys.ini') -Encoding ASCII

  # Metodo del mando: integrado (sin trucos) o codigo Gecko con los trucos de Dolphin activados en Dolphin.ini
  $gsFile = Join-Path $dol 'User\GameSettings\R2GP99.ini'
  $dini = Join-Path $cfg 'Dolphin.ini'
  $lines = @(); if (Test-Path $dini) { $lines = @(Get-Content $dini | Where-Object { $_ -notmatch '^\s*EnableCheats\s*=' }) }
  $ci = [array]::IndexOf($lines, '[Core]')
  $cheat = 'EnableCheats = ' + $(if ($script:Mando -eq 'gecko') { 'True' } else { 'False' })
  if ($ci -lt 0) { $lines += @('[Core]', $cheat) } else { $lines = $lines[0..$ci] + @($cheat) + $(if ($ci + 1 -lt $lines.Count) { $lines[($ci + 1)..($lines.Count - 1)] } else { @() }) }
  Set-Content $dini $lines -Encoding ASCII
  if ($script:Mando -eq 'gecko') {
    $txt = Get-Content (Join-Path $root 'codigos\classic_controller_vague_rant.txt'); $eur = @(); $on = $false
    foreach ($line in $txt) { if ($line -eq '[EUR]') { $on = $true; continue }; if ($line.StartsWith('[')) { $on = $false; continue }
      if ($on -and $line -match '^[0-9A-F]{8} [0-9A-F]{8}$') { $eur += $line } }
    Set-Content $gsFile (@('[Gecko]', '$Classic Controller Support [Vague Rant]') + $eur + @('[Gecko_Enabled]', '$Classic Controller Support [Vague Rant]')) -Encoding ASCII
  } else {
    Set-Content $gsFile @('# Mando clasico integrado en main.dol') -Encoding ASCII
  }
  Write-Host "Mando: $($script:Mando)"

  $args2 = @('-b', '-e', $main,
    '-C', ('Dolphin.Core.EnableCheats=' + $(if ($script:Mando -eq 'gecko') { 'True' } else { 'False' })),
    '-C', 'SYSCONF.IPL.LNG=1', '-C', 'SYSCONF.IPL.AR=True',
    '-C', 'Dolphin.Display.Fullscreen=False', '-C', 'Dolphin.Interface.ConfirmStop=False')
  Write-Host "Arrancando Dolphin... (F10 = detener, Alt+Enter = pantalla completa)"
  Start-Process -FilePath (Join-Path $dol 'Dolphin.exe') -ArgumentList ($args2 | ForEach-Object { if ($_ -match ' ') { '"' + $_ + '"' } else { $_ } }) -WorkingDirectory $dol
} catch {
  Write-Host "`nERROR: $($_.Exception.Message)" -ForegroundColor Red
  Read-Host 'Presiona Enter para cerrar'
}
Stop-Transcript | Out-Null

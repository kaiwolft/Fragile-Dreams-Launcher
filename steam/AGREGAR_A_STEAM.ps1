# Agrega ESTE juego a Steam (como "juego que no es de Steam") con su icono y su arte:
# portada vertical, portada horizontal, fondo y logo. Funciona en cualquier PC y con cualquier usuario de Steam.
# Si el juego ya estaba agregado, solo lo actualiza (no pierde tu tiempo de juego). Antes guarda una copia de shortcuts.vdf.
# Vuelve a ejecutarlo si mueves la carpeta del juego.
# ---------------------------------------------------------------------------------------------------------------
# Adds THIS game to Steam (as a "non-Steam game") with its icon and artwork. Works on any PC and Steam account.
# If it was already added it is just updated. A backup of shortcuts.vdf is made first. Run it again if you move the folder.
$ErrorActionPreference = 'Stop'
# ---- datos del juego (rutas relativas a la carpeta de este script)
$NOMBRE = 'Fragile Dreams: Farewell Ruins of the Moon'
$EXE    = 'launcher\FragileDreams.exe'
$ICONO  = @('launcher\ui\assets\icon.ico')
# ----
# El script busca el juego en su propia carpeta y en las de arriba; si no lo encuentra, te deja elegir el .exe.
# The script looks for the game next to itself and in the parent folders; if not found, it lets you pick the .exe.
$base = $PSScriptRoot
Add-Type -TypeDefinition @"
using System; using System.IO; using System.Text; using System.Collections.Generic;
public class VNode { public byte T; public string K; public string S = ""; public int I; public List<VNode> C = new List<VNode>(); }
public static class SteamVdf {
  static string RStr(BinaryReader r) { var b = new List<byte>(); byte c; while ((c = r.ReadByte()) != 0) b.Add(c); return Encoding.UTF8.GetString(b.ToArray()); }
  static void WStr(BinaryWriter w, string s) { w.Write(Encoding.UTF8.GetBytes(s ?? "")); w.Write((byte)0); }
  static List<VNode> RMap(BinaryReader r) {
    var l = new List<VNode>();
    while (r.BaseStream.Position < r.BaseStream.Length) {
      byte t = r.ReadByte(); if (t == 8) break;
      var n = new VNode(); n.T = t; n.K = RStr(r);
      if (t == 0) n.C = RMap(r); else if (t == 1) n.S = RStr(r); else if (t == 2) n.I = r.ReadInt32();
      else throw new Exception("tipo desconocido en shortcuts.vdf: " + t);
      l.Add(n);
    }
    return l;
  }
  static void WMap(BinaryWriter w, List<VNode> l) {
    foreach (var n in l) {
      w.Write(n.T); WStr(w, n.K);
      if (n.T == 0) { WMap(w, n.C); w.Write((byte)8); } else if (n.T == 1) WStr(w, n.S); else w.Write(n.I);
    }
  }
  public static List<VNode> Load(string f) {
    if (!File.Exists(f)) { var root = new VNode(); root.T = 0; root.K = "shortcuts"; var l0 = new List<VNode>(); l0.Add(root); return l0; }
    using (var r = new BinaryReader(File.OpenRead(f))) { var l = RMap(r); if (l.Count == 0) { var root = new VNode(); root.T = 0; root.K = "shortcuts"; l.Add(root); } return l; }
  }
  public static void Save(string f, List<VNode> l) {
    using (var w = new BinaryWriter(File.Create(f))) { WMap(w, l); w.Write((byte)8); }
  }
  static VNode S(string k, string v) { var n = new VNode(); n.T = 1; n.K = k; n.S = v; return n; }
  static VNode I(string k, int v) { var n = new VNode(); n.T = 2; n.K = k; n.I = v; return n; }
  public static uint Crc32(string s) {
    uint crc = 0xFFFFFFFF; foreach (byte b in Encoding.UTF8.GetBytes(s)) { crc ^= b; for (int k = 0; k < 8; k++) crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1; }
    return ~crc;
  }
  // agrega o actualiza el acceso; devuelve el appid (el que usan los nombres del arte)
  public static uint Upsert(List<VNode> file, string name, string exe, string dir, string icon) {
    VNode root = null; foreach (var n in file) if (n.T == 0 && n.K.ToLower() == "shortcuts") root = n;
    if (root == null) { root = new VNode(); root.T = 0; root.K = "shortcuts"; file.Add(root); }
    string qexe = "\"" + exe + "\"";
    uint appid = Crc32(qexe + name) | 0x80000000u;
    VNode e = null;
    foreach (var it in root.C) foreach (var f in it.C) if (f.T == 1 && f.K.ToLower() == "appname" && f.S == name) e = it;
    var fields = new List<VNode>();
    fields.Add(I("appid", unchecked((int)appid))); fields.Add(S("AppName", name)); fields.Add(S("Exe", qexe));
    fields.Add(S("StartDir", "\"" + dir + "\"")); fields.Add(S("icon", icon)); fields.Add(S("ShortcutPath", ""));
    fields.Add(S("LaunchOptions", "")); fields.Add(I("IsHidden", 0)); fields.Add(I("AllowDesktopConfig", 1));
    fields.Add(I("AllowOverlay", 1)); fields.Add(I("OpenVR", 0)); fields.Add(I("Devkit", 0)); fields.Add(S("DevkitGameID", ""));
    fields.Add(I("DevkitOverrideAppID", 0)); fields.Add(I("LastPlayTime", 0)); fields.Add(S("FlatpakAppID", ""));
    if (e == null) {
      e = new VNode(); e.T = 0; e.K = root.C.Count.ToString(); root.C.Add(e);
      var tags = new VNode(); tags.T = 0; tags.K = "tags"; fields.Add(tags); e.C = fields;
    } else {
      // conserva lo que Steam ya tenia (tiempo jugado, etiquetas...) y actualiza lo nuestro
      foreach (var nf in fields) {
        VNode old = null; foreach (var f in e.C) if (f.K.ToLower() == nf.K.ToLower()) old = f;
        if (nf.K == "LastPlayTime" && old != null) continue;
        if (old == null) e.C.Add(nf); else { old.T = nf.T; old.K = nf.K; old.S = nf.S; old.I = nf.I; }
      }
    }
    return appid;
  }
}
"@

function Buscar-Juego {
  $d = $base
  for ($i = 0; $i -lt 4 -and $d; $i++) {
    $c = Join-Path $d $EXE; if (Test-Path -LiteralPath $c) { return (Resolve-Path -LiteralPath $c).Path }
    $c = Join-Path $d (Split-Path $EXE -Leaf); if (Test-Path -LiteralPath $c) { return (Resolve-Path -LiteralPath $c).Path }
    $d = Split-Path $d -Parent
  }
  Write-Host "No encuentro $(Split-Path $EXE -Leaf) junto a este archivo: eligelo en la ventana que se abre."
  Write-Host "Could not find $(Split-Path $EXE -Leaf) next to this file: pick it in the window that opens."
  Add-Type -AssemblyName System.Windows.Forms
  $dlg = New-Object System.Windows.Forms.OpenFileDialog
  $dlg.Title = "$NOMBRE - $(Split-Path $EXE -Leaf)"
  $dlg.Filter = "$(Split-Path $EXE -Leaf)|$(Split-Path $EXE -Leaf)|*.exe|*.exe"
  if ($dlg.ShowDialog() -eq 'OK') { return $dlg.FileName }
  return $null
}

try {
  $exe = Buscar-Juego
  if (-not $exe) { throw 'No se eligio el juego / No game selected.' }
  # carpeta raiz del juego = la que contiene la ruta relativa $EXE (p. ej. ...\launcher\Juego.exe -> ...)
  $root = Split-Path $exe
  $sub = Split-Path $EXE -Parent
  if ($sub -and $root.ToLower().EndsWith($sub.ToLower())) { $root = $root.Substring(0, $root.Length - $sub.Length).TrimEnd('\') }
  $ico = $exe
  foreach ($i in $ICONO) { $c = Join-Path $root $i; if (Test-Path -LiteralPath $c) { $ico = $c; break } }
  # arte: junto a este script (arte\ o steam\) o en la carpeta del juego (steam\)
  $arteDir = $null
  foreach ($c in @((Join-Path $base 'arte'), (Join-Path $base 'steam'), (Join-Path $base 'steam\arte'), (Join-Path $root 'steam'), (Join-Path $root 'steam\arte'))) {
    if (Test-Path -LiteralPath (Join-Path $c 'p.png')) { $arteDir = $c; break }
  }
  if (-not $arteDir) { Write-Host 'Aviso: no encuentro el arte (carpeta arte\), se agrega sin imagenes / Artwork not found, adding without images.' }
  Write-Host "Juego / Game: $exe"
  $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
  if (-not $steam -or -not (Test-Path $steam)) { throw 'No encuentro Steam instalado / Steam is not installed.' }
  $steam = $steam -replace '/', '\'
  $users = Get-ChildItem (Join-Path $steam 'userdata') -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^\d+$' -and $_.Name -ne '0' }
  if (-not $users) { throw 'No hay usuarios de Steam: inicia sesion en Steam al menos una vez / Log in to Steam at least once.' }
  if (Get-Process steam -ErrorAction SilentlyContinue) {
    Write-Host 'Cerrando Steam / Closing Steam...'
    Start-Process (Join-Path $steam 'steam.exe') -ArgumentList '-shutdown'
    for ($i = 0; $i -lt 60 -and (Get-Process steam -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 500 }
    if (Get-Process steam -ErrorAction SilentlyContinue) { throw 'Steam no se cerro: cierralo (Steam > Salir) y vuelve a ejecutar esto / Close Steam and run this again.' }
    Start-Sleep -Seconds 2
  }
  foreach ($u in $users) {
    $cfg = Join-Path $u.FullName 'config'; New-Item -ItemType Directory -Force $cfg | Out-Null
    $vdf = Join-Path $cfg 'shortcuts.vdf'
    if (Test-Path $vdf) { Copy-Item $vdf ($vdf + '.antes_launchers') -Force }
    $data = [SteamVdf]::Load($vdf)
    $id = [SteamVdf]::Upsert($data, $NOMBRE, $exe, (Split-Path $exe), $ico)
    $grid = Join-Path $cfg 'grid'; New-Item -ItemType Directory -Force $grid | Out-Null
    foreach ($pair in @(@('p', 'p'), @('wide', ''), @('hero', '_hero'), @('logo', '_logo'))) {
      if (-not $arteDir) { break }
      $src = Join-Path $arteDir ($pair[0] + '.png')
      if (Test-Path -LiteralPath $src) { Copy-Item -LiteralPath $src (Join-Path $grid ("$id" + $pair[1] + '.png')) -Force }
    }
    [SteamVdf]::Save($vdf, $data)
    Write-Host "  [Steam $($u.Name)] $NOMBRE  (id $id)"
  }
  Write-Host ''
  Write-Host 'Listo: abre Steam y lo veras en tu Biblioteca con su arte. / Done: open Steam, it is in your Library.'
  Write-Host 'Si el mando no responde en el launcher: Steam > clic derecho al juego > Propiedades > Mando > "Desactivar Steam Input".'
  Write-Host 'If the controller does not respond: Steam > right-click the game > Properties > Controller > "Disable Steam Input".'
} catch {
  Write-Host ('ERROR: ' + $_.Exception.Message) -ForegroundColor Red
}

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Agrega ESTE juego a Steam en Linux (como "juego que no es de Steam") con su icono y su arte:
portada vertical, portada horizontal, fondo y logo. Funciona con todos los usuarios de Steam del equipo
(también Steam Deck en modo escritorio). Si ya estaba agregado, solo lo actualiza (no pierdes el tiempo jugado).
Antes guarda una copia de shortcuts.vdf (shortcuts.vdf.antes_launchers). Vuelve a ejecutarlo si mueves la carpeta.

Adds THIS game to Steam on Linux (as a "non-Steam game") with its icon and artwork, for every Steam account on
this computer (Steam Deck desktop mode too). If it was already added it is only updated. Run it again if you move the folder.

Uso / usage:  ./agregar_a_steam.sh    (o / or: python3 agregar_a_steam.py [ruta/al/ejecutable])
Solo usa la biblioteca estándar de Python 3. / Standard library only.
"""
import os, sys, glob, shutil, struct, subprocess, time, zlib

# ---- datos del juego (rutas relativas a la carpeta del juego) / game data (relative to the game folder)
NOMBRE = 'Fragile Dreams: Farewell Ruins of the Moon'
EXE = 'launcher/fragiledreams'
ICONO = ['launcher/ui/assets/icon.png']
OPCIONES = '--no-sandbox'          # opciones de lanzamiento / launch options
# ----

HERE = os.path.dirname(os.path.abspath(__file__))


def say(es, en=None):
    print(es if en is None else es + '\n' + en)


# ---------------------------------------------------------------- shortcuts.vdf (VDF binario)
def _rstr(b, i):
    j = b.index(b'\x00', i)
    return b[i:j].decode('utf-8', 'replace'), j + 1


def _rmap(b, i):
    out = []
    while i < len(b):
        t = b[i]; i += 1
        if t == 8:
            break
        k, i = _rstr(b, i)
        if t == 0:
            v, i = _rmap(b, i)
        elif t == 1:
            v, i = _rstr(b, i)
        elif t == 2:
            v = struct.unpack_from('<i', b, i)[0]; i += 4
        else:
            raise ValueError('tipo desconocido en shortcuts.vdf: %d' % t)
        out.append([t, k, v])
    return out, i


def _wmap(items):
    o = bytearray()
    for t, k, v in items:
        o.append(t); o += k.encode('utf-8') + b'\x00'
        if t == 0:
            o += _wmap(v); o.append(8)
        elif t == 1:
            o += str(v).encode('utf-8') + b'\x00'
        else:
            o += struct.pack('<i', v)
    return bytes(o)


def load_vdf(path):
    if not os.path.exists(path):
        return [[0, 'shortcuts', []]]
    data = open(path, 'rb').read()
    items, _ = _rmap(data, 0) if data else ([], 0)
    return items or [[0, 'shortcuts', []]]


def save_vdf(path, items):
    with open(path, 'wb') as f:
        f.write(_wmap(items) + b'\x08')


def upsert(items, name, exe, start_dir, icon, opts):
    root = next((n for n in items if n[0] == 0 and n[1].lower() == 'shortcuts'), None)
    if root is None:
        root = [0, 'shortcuts', []]; items.append(root)
    qexe = '"%s"' % exe
    appid = (zlib.crc32((qexe + name).encode('utf-8')) & 0xffffffff) | 0x80000000
    sid = appid - (1 << 32) if appid >= (1 << 31) else appid
    fields = [[2, 'appid', sid], [1, 'AppName', name], [1, 'Exe', qexe], [1, 'StartDir', '"%s"' % start_dir], [1, 'icon', icon],
              [1, 'ShortcutPath', ''], [1, 'LaunchOptions', opts], [2, 'IsHidden', 0], [2, 'AllowDesktopConfig', 1],
              [2, 'AllowOverlay', 1], [2, 'OpenVR', 0], [2, 'Devkit', 0], [1, 'DevkitGameID', ''], [2, 'DevkitOverrideAppID', 0],
              [2, 'LastPlayTime', 0], [1, 'FlatpakAppID', '']]
    entry = None
    for it in root[2]:
        if it[0] == 0 and any(f[0] == 1 and f[1].lower() == 'appname' and f[2] == name for f in it[2]):
            entry = it
    if entry is None:
        root[2].append([0, str(len(root[2])), fields + [[0, 'tags', []]]])
    else:   # conserva lo que Steam ya tenía (tiempo jugado, etiquetas...)
        for nf in fields:
            old = next((f for f in entry[2] if f[1].lower() == nf[1].lower()), None)
            if nf[1] == 'LastPlayTime' and old is not None:
                continue
            if old is None:
                entry[2].append(nf)
            else:
                old[:] = nf
    return appid


# ---------------------------------------------------------------- Steam
def steam_roots():
    home = os.path.expanduser('~')
    cands = [os.path.join(home, '.steam', 'steam'), os.path.join(home, '.local', 'share', 'Steam'), os.path.join(home, '.steam', 'root'),
             os.path.join(home, '.var', 'app', 'com.valvesoftware.Steam', '.local', 'share', 'Steam'),
             os.path.join(home, 'snap', 'steam', 'common', '.local', 'share', 'Steam')]
    seen, out = set(), []
    for c in cands:
        if os.path.isdir(os.path.join(c, 'userdata')):
            r = os.path.realpath(c)
            if r not in seen:
                seen.add(r); out.append(c)
    return out


def steam_running():
    try:
        return subprocess.run(['pgrep', '-x', 'steam'], stdout=subprocess.DEVNULL).returncode == 0
    except FileNotFoundError:
        return False


def close_steam():
    if not steam_running():
        return
    say('Cerrando Steam...', 'Closing Steam...')
    for cmd in (['steam', '-shutdown'], ['flatpak', 'run', 'com.valvesoftware.Steam', '-shutdown']):
        try:
            subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL); break
        except FileNotFoundError:
            continue
    for _ in range(60):
        if not steam_running():
            break
        time.sleep(0.5)
    if steam_running():
        raise SystemExit('Steam no se cerró: ciérralo (Steam > Salir) y vuelve a ejecutar esto.\nSteam did not close: quit Steam and run this again.')
    time.sleep(2)


def find_game():
    if len(sys.argv) > 1 and os.path.isfile(sys.argv[1]):
        return os.path.abspath(sys.argv[1])
    d = HERE
    for _ in range(4):
        for c in (os.path.join(d, EXE), os.path.join(d, os.path.basename(EXE))):
            if os.path.isfile(c):
                return os.path.abspath(c)
        d = os.path.dirname(d)
    return None


def main():
    exe = find_game()
    if not exe:
        raise SystemExit('No encuentro %s junto a este archivo. Ejecuta: python3 agregar_a_steam.py /ruta/a/%s\n'
                         'Could not find %s next to this file. Run: python3 agregar_a_steam.py /path/to/%s'
                         % (EXE, os.path.basename(EXE), EXE, os.path.basename(EXE)))
    root = os.path.dirname(exe)
    sub = os.path.dirname(EXE)
    if sub and root.endswith(os.sep + sub.replace('/', os.sep)):
        root = root[: -len(sub) - 1]
    icon = next((os.path.join(root, i) for i in ICONO if os.path.isfile(os.path.join(root, i))), exe)
    arte = next((c for c in (os.path.join(HERE, 'arte'), os.path.join(HERE, 'steam', 'arte'), os.path.join(root, 'steam', 'arte'),
                             os.path.join(root, 'steam')) if os.path.isfile(os.path.join(c, 'p.png'))), None)
    if not arte:
        say('Aviso: no encuentro el arte (steam/arte), se agrega sin imágenes.', 'Artwork not found, adding without images.')
    try:
        os.chmod(exe, 0o755)
    except OSError:
        pass
    roots = steam_roots()
    if not roots:
        raise SystemExit('No encuentro Steam instalado (inicia sesión en Steam al menos una vez).\nSteam not found (log in to Steam at least once).')
    if any('com.valvesoftware.Steam' in r for r in roots):
        say('Nota: con Steam en Flatpak el juego queda dentro de su sandbox; si no abre, usa el Steam nativo de tu distribución.',
            'Note: with Flatpak Steam the game runs inside its sandbox; if it does not start, use your distro\'s native Steam.')
    close_steam()
    print('Juego / Game: ' + exe)
    for sr in roots:
        for u in sorted(glob.glob(os.path.join(sr, 'userdata', '*'))):
            if not os.path.basename(u).isdigit() or os.path.basename(u) == '0':
                continue
            cfg = os.path.join(u, 'config'); os.makedirs(cfg, exist_ok=True)
            vdf = os.path.join(cfg, 'shortcuts.vdf')
            if os.path.exists(vdf):
                shutil.copy2(vdf, vdf + '.antes_launchers')
            items = load_vdf(vdf)
            appid = upsert(items, NOMBRE, exe, os.path.dirname(exe), icon, OPCIONES)
            grid = os.path.join(cfg, 'grid'); os.makedirs(grid, exist_ok=True)
            if arte:
                for src, suf in (('p', 'p'), ('wide', ''), ('hero', '_hero'), ('logo', '_logo')):
                    f = os.path.join(arte, src + '.png')
                    if os.path.isfile(f):
                        shutil.copyfile(f, os.path.join(grid, '%d%s.png' % (appid, suf)))
            save_vdf(vdf, items)
            print('  [Steam %s] %s  (id %d)' % (os.path.basename(u), NOMBRE, appid))
    say('\nListo: abre Steam y lo verás en tu Biblioteca con su arte.', 'Done: open Steam, it is in your Library.')
    say('Si el mando no responde en el launcher: Steam > clic derecho al juego > Propiedades > Mando > "Desactivar Steam Input".',
        'If the controller does not respond: Steam > right-click the game > Properties > Controller > "Disable Steam Input".')


if __name__ == '__main__':
    try:
        main()
    except SystemExit as e:
        if e.code not in (None, 0):
            print('ERROR: %s' % e.code); sys.exit(1)

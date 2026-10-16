// Fragile Dreams: Farewell Ruins of the Moon — Dionixu's Launcher (Electron / Chromium) · Windows y Linux
// Basado en los launchers de The Last Story y Pandora's Tower: lanza Dolphin, aplica idioma, ajustes y controles,
// menú de pausa con Esc / L3+R3.
// Idiomas: el juego europeo (R2GP99) guarda el idioma de texto y de voz en 0x80326208 (+0x10 texto, +0x14 voz).
// En cada arranque se genera main.dol desde juego\dol\{original|espanol}.dol forzando esos valores y con el
// mando clásico de Vague Rant integrado (rutinas escritas sobre funciones muertas del juego).
const { app, BrowserWindow, ipcMain, screen, protocol, net, session } = require('electron');
const { pathToFileURL } = require('url');
const path = require('path');
const fs = require('fs');
const crypto = require('crypto');
const { spawn, execFile } = require('child_process');

const IS_WIN = process.platform === 'win32';
const IS_LINUX = process.platform === 'linux';
const NL = IS_WIN ? '\r\n' : '\n';

const APP_NAME = "Fragile Dreams — Dionixu's Launcher";
const ROOT = path.dirname(process.execPath);          // carpeta launcher
const UI = path.join(ROOT, 'ui');
const DATA = path.join(ROOT, 'data');
const CFG = path.join(DATA, 'config.json');
const PATHS = path.join(DATA, 'paths.json');
const LOG = path.join(DATA, 'launcher.log');
const GAME_ID = 'R2GP99';

app.commandLine.appendSwitch('autoplay-policy', 'no-user-gesture-required');
if (IS_LINUX) app.commandLine.appendSwitch('ozone-platform-hint', 'auto');
protocol.registerSchemesAsPrivileged([{ scheme: 'fd', privileges: { standard: true, secure: true, supportFetchAPI: true, stream: true } }]);
app.setPath('userData', path.join(DATA, 'chromium'));
app.setName("Dionixu's Launcher");

const log = (s) => { try { fs.appendFileSync(LOG, new Date().toISOString() + '  ' + s + '\n'); } catch (e) {} };
const readJson = (f, d) => { try { return JSON.parse(fs.readFileSync(f, 'utf8').replace(/^﻿/, '')); } catch (e) { return d; } };
const exists = (f) => { try { return fs.existsSync(f); } catch (e) { return false; } };
const saveCfg = () => { try { fs.mkdirSync(DATA, { recursive: true }); fs.writeFileSync(CFG, JSON.stringify(cfg, null, 1)); } catch (e) {} };

let win = null, game = null, exitWin = null, bridge = null, exitOpen = false, gameStart = 0;
let cfg = readJson(CFG, {});

// ---------------- rutas (absolutas o relativas a la carpeta del launcher)
const paths = (() => {
  const raw = readJson(PATHS, {});
  const abs = (p) => path.resolve(ROOT, p);
  const pick = (k, d) => { const v = raw[k] ? abs(raw[k]) : null; return v && exists(v) ? v : abs(d); };
  const out = {
    gameES: pick('gameES', '../juego/EU_ESPANOL'),
    gameOrig: pick('gameOrig', '../juego/EU_ORIGINAL'),
    dols: pick('dols', '../juego/dol')
  };
  if (IS_WIN) {
    out.dolphinExe = pick('dolphinExe', '../Dolphin-x64/Dolphin.exe');
    out.dolphinUser = raw.dolphinUser ? abs(raw.dolphinUser) : path.join(path.dirname(out.dolphinExe), 'User');
  } else {
    const d = raw.dolphinExe || 'dolphin-emu';
    out.dolphinExe = d.startsWith('flatpak:') || !d.includes('/') ? d : abs(d);
    out.dolphinUser = abs(raw.dolphinUser || '../dolphin-user');
  }
  out.root = path.resolve(ROOT, '..');
  // motor propio (core de Dolphin dentro de fd_motor.exe): ../motor y ../core
  out.motorExe = pick('motorExe', '../motor/fd_motor.exe');
  out.coreDll = pick('coreDll', '../core/dolphin_libretro.dll');
  out.coreSystem = pick('coreSystem', '../core/system');
  return out;
})();
const useMotor = () => IS_WIN && cfg.engine !== 'dolphin' && exists(paths.motorExe) && exists(paths.coreDll);
const userPath = (...p) => path.join(paths.dolphinUser, ...p);
function dolphinCommand() {
  const d = paths.dolphinExe;
  if (IS_LINUX && d.startsWith('flatpak:'))
    return { cmd: 'flatpak', pre: ['run', '--filesystem=' + paths.root, '--env=QT_QPA_PLATFORM=xcb', d.slice(8)], ok: true };
  if (IS_LINUX && !d.includes('/')) {
    const found = (process.env.PATH || '').split(':').some((dir) => dir && exists(path.join(dir, d)));
    return { cmd: d, pre: [], ok: found };
  }
  return { cmd: d, pre: [], ok: exists(d) };
}
const helper = (name) => {
  const f = path.join(__dirname, name);
  if (!IS_WIN && exists(f)) { try { fs.chmodSync(f, 0o755); } catch (e) {} }
  return f;
};
const send = (type, data) => { if (win && !win.isDestroyed()) win.webContents.send('msg', JSON.stringify({ type, data })); };

function state() {
  const sysDir = (dir) => path.join(dir || '', 'DATA', 'sys');
  return {
    logros: achPublic(),
    config: cfg,
    fullscreen: !!(win && win.isFullScreen()),
    dolphinOk: useMotor() || dolphinCommand().ok,
    engine: useMotor() ? 'motor' : 'dolphin',
    platform: process.platform,
    esOk: exists(path.join(sysDir(paths.gameES), 'boot.bin')) && exists(path.join(paths.dols, 'espanol.dol')),
    origOk: exists(path.join(sysDir(paths.gameOrig), 'boot.bin')) && exists(path.join(paths.dols, 'original.dol')),
    padPatchOk: exists(path.join(DATA, 'parche_mando_eur.json'))
  };
}

// ---------------- .ini de Dolphin: editar claves conservando el resto
function setIni(file, values) {
  let text = '';
  try { text = fs.readFileSync(file, 'utf8'); } catch (e) {}
  const lines = text.split(/\r?\n/);
  for (const [section, entries] of Object.entries(values)) {
    let start = lines.findIndex((l) => l.trim().toLowerCase() === '[' + section.toLowerCase() + ']');
    if (start < 0) { if (lines.length && lines[lines.length - 1] !== '') lines.push(''); lines.push('[' + section + ']'); start = lines.length - 1; }
    for (const [key, val] of Object.entries(entries)) {
      let end = lines.findIndex((l, i) => i > start && /^\s*\[/.test(l)); if (end < 0) end = lines.length;
      const i = lines.findIndex((l, j) => j > start && j < end && l.split('=')[0].trim().toLowerCase() === key.toLowerCase());
      const line = key + ' = ' + val;
      if (i >= 0) lines[i] = line; else lines.splice(end, 0, line);
    }
  }
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, lines.join(NL).replace(/(\r?\n)+$/, '') + NL);
}

// ---------------- main.dol: idioma de texto y voz + mando clásico integrado
const TEXT_LANG = { es: 1, en: 1, fr: 3, de: 5 };       // valores del juego: 0 japonés, 1 inglés, 3 francés, 5 alemán
const VOICE_LANG = { en: 1, ja: 0 };
function buildDol(text, voice, dst) {
  const base = path.join(paths.dols, text === 'es' ? 'espanol.dol' : 'original.dol');
  const b = fs.readFileSync(base);
  const T = TEXT_LANG[text] ?? 1, V = VOICE_LANG[voice] ?? 0;
  const off = (addr) => {                       // dirección -> posición en el archivo según la cabecera DOL
    for (let i = 0; i < 18; i++) {
      const o = b.readUInt32BE(i * 4), a = b.readUInt32BE(0x48 + i * 4), s = b.readUInt32BE(0x90 + i * 4);
      if (s && addr >= a && addr < a + s) return o + addr - a;
    }
    throw new Error('Dirección fuera del ejecutable: ' + addr.toString(16));
  };
  const put = (addr, word, expect) => {
    const o = off(addr), cur = b.readUInt32BE(o);
    if (cur !== expect >>> 0) throw new Error('main.dol inesperado en ' + addr.toString(16) + ': ' + cur.toString(16));
    b.writeUInt32BE(word >>> 0, o);
  };
  put(0x80056AEC, 0x48000028, 0x2C000000);          // arranque: saltar la comprobación del idioma del Wii
  put(0x80056B14, 0x38000000 + T, 0x38000001);      // arranque: texto
  put(0x80056B20, 0x38600000 + V, 0x38600001);      // arranque: voz
  put(0x801A0618, 0x38000000 + T, 0x38000001);      // opciones guardadas: texto
  put(0x801A0628, 0x38000000 + T, 0x7C03002E);
  put(0x801A05E8, 0x38E00000 + V, 0x55070FFE);      // opciones guardadas: voz
  put(0x801A51A4, 0x38000000 + T, 0x801E0028);      // menú de título: texto
  // BOOT_SEQ estado 2 = pantalla de la correa del Wiimote: en vez de mostrarla, ir directo a su salida (estado 4)
  put(0x800999F4, 0x48000254, 0x807F0164);          // lwz r3,356(r31) -> b 0x80099C48
  const applyPatch = (p, what) => {
    for (const w of p.writes) put(parseInt(w.addr, 16), parseInt(w.value, 16), parseInt(w.expect, 16));
    const code = Buffer.from(p.cave, 'hex'), o = off(parseInt(p.cave_addr, 16));
    const md5 = crypto.createHash('md5').update(b.subarray(o, o + code.length)).digest('hex');
    if (md5 !== p.cave_expect_md5) throw new Error('main.dol inesperado en la zona del ' + what + ' (' + md5 + ')');
    code.copy(b, o);
  };
  const cc = readJson(path.join(DATA, 'parche_mando_eur.json'), null);
  if (!cc) throw new Error('Falta data/parche_mando_eur.json');
  applyPatch(cc, 'mando');
  // mod de juego: movimiento 360 + cámara con el stick derecho (la cámara la alimenta el motor; con Dolphin queda inactiva)
  const gm = readJson(path.join(DATA, 'parche_juego_eur.json'), null);
  if (gm) { applyPatch(gm, 'mod de juego'); log('main.dol: ' + gm.nombre); }
  // 60 FPS: el juego ya sabe correr con intervalo de imagen 1 (lo usa en algunas pantallas y en PAL 50 Hz); en el
  // campo lo pone a 2 (30 fps). El parche cambia esos "2" por "1". Se puede volver a 30 en Ajustes -> Gráficos.
  // (16 oct) Con el parche fijo la escena inicial se quedaba trabada: los eventos cuentan fotogramas. Ahora los 60 FPS
  // los pone el motor solo mientras se juega (cámara del jugador activa) y vuelve a 30 en escenas y menús.
  // codigos\parche_60fps_eur.json queda como referencia, ya no se aplica.
  try { fs.rmSync(dst, { force: true }); } catch (e) {}       // nunca escribir a través de un enlace duro
  fs.writeFileSync(dst, b);
  log('main.dol: texto ' + text + ' (' + T + '), voz ' + voice + ' (' + V + ')');
}

// ---------------- controles: mando clásico emulado (lo traduce el parche de Vague Rant a Wiimote + Nunchuk)
// Mando Xbox + teclado a la vez. El stick derecho / puntero: con mando, el stick; con teclado, el ratón.
const KBDEV = IS_WIN ? 'DInput/0/Keyboard Mouse' : 'XInput2/0/Virtual core keyboard';
const PTRDEV = IS_WIN ? KBDEV : 'XInput2/0/Virtual core pointer';
const DI2X = { SPACE: 'space', LSHIFT: 'Shift_L', RSHIFT: 'Shift_R', LCONTROL: 'Control_L', RCONTROL: 'Control_R', LMENU: 'Alt_L', RMENU: 'ISO_Level3_Shift',
  RETURN: 'Return', NUMPADENTER: 'KP_Enter', ESCAPE: 'Escape', TAB: 'Tab', BACK: 'BackSpace', CAPITAL: 'Caps_Lock', UP: 'Up', DOWN: 'Down', LEFT: 'Left',
  RIGHT: 'Right', MINUS: 'minus', EQUALS: 'equal', LBRACKET: 'bracketleft', RBRACKET: 'bracketright', SEMICOLON: 'semicolon', APOSTROPHE: 'apostrophe',
  GRAVE: 'grave', BACKSLASH: 'backslash', OEM_102: 'less', COMMA: 'comma', PERIOD: 'period', SLASH: 'slash', INSERT: 'Insert', DELETE: 'Delete',
  HOME: 'Home', END: 'End', PRIOR: 'Prior', NEXT: 'Next', ADD: 'KP_Add', SUBTRACT: 'KP_Subtract', MULTIPLY: 'KP_Multiply', DIVIDE: 'KP_Divide',
  DECIMAL: 'KP_Decimal', 'Click 0': 'Click 1', 'Click 1': 'Click 3', 'Click 2': 'Click 2', 'Click 3': 'Click 8', 'Click 4': 'Click 9' };
const isMouseName = (n) => /^Click \d$|^Axis Z/.test(n);
const keyName = (n) => IS_WIN ? n : (DI2X[n] || (/^NUMPAD(\d)$/.test(n) ? 'KP_' + n.slice(6) : /^[A-Z]$/.test(n) ? n.toLowerCase() : n));
const SDL_PADS = ['Xbox 360 Controller', 'Xbox One Controller', 'Xbox Series X Controller', 'Xbox Wireless Controller', 'PS4 Controller', 'PS5 Controller',
  'Nintendo Switch Pro Controller', 'Steam Deck', 'Steam Virtual Gamepad', 'Steam Controller', '8BitDo Pro 2', 'Logitech Dual Action'];
// botón del mando clásico -> acción en Fragile Dreams (con el parche de Vague Rant)
const PAD_DEF = { a: 'Button A', b: 'Button B', x: 'Button Y', y: '', r: '', l: 'Shoulder R', zl: 'Shoulder L', zr: 'Trigger L',
  plus: 'Start', minus: 'Back', du: 'Pad N', dd: 'Pad S', dl: 'Pad W', dr: 'Pad E' };
const KB_DEF = { ls_up: 'W', ls_down: 'S', ls_left: 'A', ls_right: 'D', rs_up: 'I', rs_down: 'K', rs_left: 'J', rs_right: 'L',
  a: 'Click 0', b: 'Q', x: 'F', y: '', r: '', l: 'C', zl: 'R', zr: 'Click 1', plus: 'RETURN', minus: 'TAB',
  du: 'UP', dd: 'DOWN', dl: 'LEFT', dr: 'RIGHT' };
// Un botón = una acción (16 oct). El parche del mando clásico manda A, R e Y al mismo botón A del Wii y B/ZR al B:
// RT y X quedaban como copias de A, L3/R3 de RB/LB. Ahora quedan libres (L3 + R3 sigue siendo la pausa del launcher).
// LT se mantiene para la primera persona (en el Wii es "mantener B"), Enter solo para aceptar en menús.
const PAD_EXTRA = {};
const KB_EXTRA = { a: ['E'] };   // E = interactuar (costumbre de PC); Enter = + (saltar escenas / menú), Tab = −
const okName = (v) => typeof v === 'string' && /^[A-Za-z0-9 +\-_.]{1,24}$/.test(v);
const ctlDevice = () => cfg.ctl === 'kb' || cfg.ctl === 'pad' ? cfg.ctl : (cfg.lastDevice === 'kb' ? 'kb' : 'pad');
function writeControls() {
  const pb = Object.assign({}, PAD_DEF, cfg.padBinds || {}), kbb = Object.assign({}, KB_DEF, cfg.kbBinds || {});
  for (const k of Object.keys(kbb)) if (kbb[k] === 'ESCAPE') kbb[k] = KB_DEF[k];   // Esc queda reservado para la pausa del launcher
  const usedKb = new Set(Object.values(kbb)), usedPad = new Set(Object.values(pb));
  const or = (...e) => e.filter(Boolean).join(' | ');
  const kb = (n) => {
    if (!okName(n)) return null;
    if (IS_WIN) return '`' + KBDEV + ':' + n + '`';
    const x = keyName(n);
    if (isMouseName(n)) return '`' + PTRDEV + ':' + x + '`';
    const names = /^[a-z]$/.test(x) ? [x, x.toUpperCase()] : [x];
    return or(...names.flatMap((nm) => ['`' + KBDEV + ':' + nm + '`', '`' + PTRDEV + ':' + nm + '`']));
  };
  const padNames = IS_WIN ? [] : [...new Set([...(cfg.padNames || []), ...SDL_PADS])].slice(0, 16);
  const pad = (n, dz) => {
    if (!okName(n)) return null;
    if (IS_WIN) return dz ? 'deadzone(`' + n + '`, 0.15)' : '`' + n + '`';
    return or(...padNames.map((d) => dz ? 'deadzone(`SDL/0/' + d + ':' + n + '`, 0.15)' : '`SDL/0/' + d + ':' + n + '`'));
  };
  const P = (id) => [pad(pb[id]), PAD_EXTRA[id] && !usedPad.has(PAD_EXTRA[id]) ? pad(PAD_EXTRA[id]) : null];
  const K = (id) => [kb(kbb[id]), ...((KB_EXTRA[id] || []).filter((x) => !usedKb.has(x)).map(kb))];
  const mouseMode = ctlDevice() === 'kb';
  const pinv = cfg.padInvert === 'on', inv = cfg.invert === 'on';
  const cursor = (ax) => '`' + PTRDEV + ':Cursor ' + ax + '`';
  const L = ['[Wiimote1]', 'Device = ' + (IS_WIN ? 'XInput/0/Gamepad' : 'SDL/0/' + padNames[0]), 'Extension = Classic'];
  const set = (key, v) => L.push(key + ' = ' + v);
  const C = (key, v) => set('Classic/' + key, v);
  C('Buttons/A', or(...P('a'), ...K('a')));
  C('Buttons/B', or(...P('b'), ...K('b')));
  C('Buttons/X', or(...P('x'), ...K('x')));
  C('Buttons/Y', or(...P('y'), ...K('y')));
  C('Buttons/ZL', or(...P('zl'), ...K('zl')));
  C('Buttons/ZR', or(...P('zr'), ...K('zr')));
  C('Buttons/+', or(...P('plus'), ...K('plus')));
  C('Buttons/-', or(...P('minus'), ...K('minus')));
  C('Buttons/Home', '');
  C('Triggers/L', or(...P('l'), ...K('l')));
  C('Triggers/R', or(...P('r'), ...K('r')));
  C('Triggers/L-Analog', or(...P('l'), ...K('l')));
  C('Triggers/R-Analog', or(...P('r'), ...K('r')));
  C('D-Pad/Up', or(...P('du'), ...K('du')));
  C('D-Pad/Down', or(...P('dd'), ...K('dd')));
  C('D-Pad/Left', or(...P('dl'), ...K('dl')));
  C('D-Pad/Right', or(...P('dr'), ...K('dr')));
  C('Left Stick/Up', or(pad('Left Y+', true), kb(kbb.ls_up)));
  C('Left Stick/Down', or(pad('Left Y-', true), kb(kbb.ls_down)));
  C('Left Stick/Left', or(pad('Left X-', true), kb(kbb.ls_left)));
  C('Left Stick/Right', or(pad('Left X+', true), kb(kbb.ls_right)));
  C('Left Stick/Dead Zone', '0.');
  // linterna: con mando el stick derecho; con teclado, la posición del ratón (apuntar directamente)
  const rUp = pinv ? 'Right Y-' : 'Right Y+', rDn = pinv ? 'Right Y+' : 'Right Y-';
  C('Right Stick/Up', or(mouseMode ? cursor(inv ? 'Y+' : 'Y-') : pad(rUp, true), kb(kbb.rs_up)));
  C('Right Stick/Down', or(mouseMode ? cursor(inv ? 'Y-' : 'Y+') : pad(rDn, true), kb(kbb.rs_down)));
  C('Right Stick/Left', or(mouseMode ? cursor('X-') : pad('Right X-', true), kb(kbb.rs_left)));
  C('Right Stick/Right', or(mouseMode ? cursor('X+') : pad('Right X+', true), kb(kbb.rs_right)));
  C('Right Stick/Dead Zone', '0.');
  // puntero de la Wii (menús): ratón directo, o stick derecho como cursor relativo
  // ---- Mod de mando (12 oct): el puntero de la Wii con el stick derecho, distinto en tercera y primera persona.
  // Tercera persona: como una cámara moderna. Curva (pow 1.6): poco stick = apuntar la linterna sin girar; a fondo el
  // puntero llega al borde y el juego gira la cámara. Al soltar vuelve al frente suave (0,3 s): si se quedara de lado
  // el juego giraría sin parar (la "cámara pegada").
  // Primera persona (LT = ZR = B del Wiimote): el puntero se queda donde lo dejas (relative con estado compartido $px/$py).
  // Todo con expresiones de Dolphin (if/pow/smooth/relative): no se toca el main.dol.
  const fp = '(' + (pad(pb.zr) || pad('Trigger L')) + ' > 0.5)';     // botón de primera persona (LT por defecto)
  const tp = (n) => 'smooth(pow(' + pad(n, true) + ', 1.6), 0.06, 0.3)';
  const rel = (n, sign, v) => 'relative(' + pad(n, true) + ' * ' + fp + ', 1.4, ' + sign + '1.0, ' + v + ')';
  const irPad = (n, sign, v) => '(' + fp + ' * ' + rel(n, sign, v) + ') + ((!' + fp + ') * ' + tp(n) + ')';
  set('IR/Up', mouseMode ? cursor('Y-') : irPad(rUp, '', '$py'));
  set('IR/Down', mouseMode ? cursor('Y+') : irPad(rDn, '-', '$py'));
  set('IR/Left', mouseMode ? cursor('X-') : irPad('Right X-', '-', '$px'));
  set('IR/Right', mouseMode ? cursor('X+') : irPad('Right X+', '', '$px'));
  set('IR/Relative Input', 'False');
  set('IR/Total Yaw', '25.');
  set('IR/Total Pitch', '20.');
  set('IR/Recenter', '');
  set('IR/Hide', '');
  set('IR/Auto-Hide', 'False');
  L.push('Source = 1', '[Wiimote2]', 'Source = 0', '[Wiimote3]', 'Source = 0', '[Wiimote4]', 'Source = 0', '[BalanceBoard]', 'Source = 0');
  const dir = userPath('Config'); fs.mkdirSync(dir, { recursive: true });
  if (cfg.autoControls !== 'off') fs.writeFileSync(path.join(dir, 'WiimoteNew.ini'), L.join(NL) + NL);
  else log('Controles: se conserva WiimoteNew.ini de Dolphin (controles automáticos apagados)');
  fs.writeFileSync(path.join(dir, 'Hotkeys.ini'), ['[Hotkeys]', 'Device = ' + KBDEV,
    'General/Toggle Fullscreen = @(Alt+' + (IS_WIN ? 'RETURN' : 'Return') + ')', 'General/Take Screenshot = F9'].join(NL) + NL);
  log('Controles: ' + (mouseMode ? 'teclado y ratón' : 'mando'));
}

// sin trucos de Dolphin: el mando ya va dentro de main.dol
function writeGameSettings() {
  const f = userPath('GameSettings', GAME_ID + '.ini');
  fs.mkdirSync(path.dirname(f), { recursive: true });
  fs.writeFileSync(f, '# Generado por Dionixu\'s Launcher: idioma y mando clasico van integrados en main.dol' + NL);
}

// ---------------- logros (set oficial de RetroAchievements, juego 131, evaluado sin conexión por fd_logros)
// El set está hecho para la versión USA (R2GEXJ); el launcher usa la europea (R2GP99). Las direcciones de datos
// estáticos se traducen con data/mapa_usa_eur.json (sacado comparando ambos main.dol); el montón de MEM1 se desplaza
// lo mismo que ArenaLo (0x804290E0 USA -> 0x8042ABC0 EUR). Los punteros (AddAddress) se siguen en tiempo real.
const RA_GAME = 131;
const LOGROS = path.join(DATA, 'logros');
const RA_SET = path.join(LOGROS, 'ra_' + RA_GAME + '.json');
const PROG = path.join(LOGROS, 'progreso.json');
const BADGES = path.join(UI, 'assets', 'logros');
let achSet = null, achProc = null;
let prog = readJson(PROG, { unlocked: {}, progress: {} });
let toastWin = null, toastQueue = [], toastBusy = false;
const loadSet = () => {
  achSet = readJson(RA_SET, null);
  if (achSet && !Array.isArray(achSet.achievements)) achSet = null;
  if (achSet) achSet.achievements = achSet.achievements.filter((a) => a.id < 100000000 && a.points > 0);
  return achSet;
};
const saveProg = () => { try { fs.mkdirSync(LOGROS, { recursive: true }); fs.writeFileSync(PROG, JSON.stringify(prog)); } catch (e) { log('progreso: ' + e.message); } };
let progTimer = null;
const saveProgSoon = () => { if (!progTimer) progTimer = setTimeout(() => { progTimer = null; saveProg(); }, 3000); };
function achPublic() {
  if (!loadSet()) return null;
  const badge = (a, l) => { const f = (a.badge || '') + (l ? '_lock' : '') + '.png'; return exists(path.join(BADGES, f)) ? 'assets/logros/' + f : ''; };
  return {
    game: achSet.game,
    list: achSet.achievements.map((a) => ({ id: a.id, title: a.title, description: a.description, points: a.points, author: a.author, type: a.type, badge: badge(a, false), badgeLock: badge(a, true) })),
    unlocked: prog.unlocked, progress: prog.progress,
    tr: readJson(path.join(LOGROS, 'traducciones.json'), {})
  };
}
// --- traducción de direcciones USA -> EUR
let addrMap = null;
function mapAddr(ra) {
  if (!addrMap) {
    const m = readJson(path.join(DATA, 'mapa_usa_eur.json'), { points: [] });
    addrMap = m.points.map(([a, d]) => [parseInt(a, 16), d]);
  }
  if (ra >= 0x01800000) return ra;                                  // MEM2: sin cambios
  const abs = (0x80000000 + ra) >>> 0;
  if (abs >= 0x804290E0) return ra + 0x1AE0;                        // montón de MEM1 (sigue a ArenaLo)
  if (abs < 0x80269E80 || !addrMap.length) return ra;               // código: sin cambios
  let lo = 0, hi = addrMap.length - 1;
  if (abs < addrMap[0][0]) return ra + addrMap[0][1];
  while (lo < hi) { const mid = (lo + hi + 1) >> 1; if (addrMap[mid][0] <= abs) lo = mid; else hi = mid - 1; }
  return ra + addrMap[lo][1];
}
function translateMemaddr(mem) {
  // (16 oct) El set oficial ya trae un grupo alternativo para la versión europea: comprueba 0x80000000 == 'R2GP'
  // (1379026768) con sus propias direcciones (USA + 0x740…). Traducirlo desplazaba ese grupo dos veces y ningún logro
  // podía saltar. Si el logro ya trae el grupo PAL se usa tal cual; la traducción queda solo para logros sin él.
  if (/=1379026768(?![0-9])/.test(mem)) return mem;
  // grupos separados por "S"; condiciones por "_"; tras "I:" (AddAddress) la dirección de la siguiente condición es un desplazamiento
  return mem.split(/(?<!0x)S/).map((grp) => {
    let offsetNext = false;
    return grp.split('_').map((cond) => {
      const isAdd = /^I:/.test(cond);
      const keepOffset = offsetNext;
      const out = cond.replace(/(^|[^0-9A-Za-z])((?:[dpb~]?)(?:0x[HLUMNOPQRSTXWGIJK ]?|f[FBHIM]))([0-9a-fA-F]{1,8})/g, (all, pre, kind, hex) => {
        if (keepOffset) return all;
        const v = mapAddr(parseInt(hex, 16));
        return pre + kind + v.toString(16).padStart(hex.length, '0');
      });
      offsetNext = isAdd;
      return out;
    }).join('_');
  }).join('S');
}
// (16 oct) Los logros solo se guardan cuando el juego guarda la partida: al saltar quedan pendientes (con su aviso) y se
// confirman cuando cambia el archivo de guardado de la NAND (Wii\title\00010000\52324750\data\*). Si sales sin
// guardar, se descartan y vuelven a poder conseguirse.
let pending = {}, saveStamp = null, saveTimer = null;
const SAVE_DIR = () => userPath('Wii', 'title', '00010000', '52324750', 'data');
function saveSignature() {
  try { return fs.readdirSync(SAVE_DIR()).filter((f) => f !== 'banner.bin').map((f) => { const st = fs.statSync(path.join(SAVE_DIR(), f)); return f + ':' + st.size + ':' + st.mtimeMs; }).sort().join('|'); }
  catch (e) { return ''; }
}
function commitPending(reason) {
  const ids = Object.keys(pending); if (!ids.length) return;
  for (const id of ids) prog.unlocked[id] = pending[id];
  pending = {}; saveProg();
  log('Partida guardada (' + reason + '): ' + ids.length + ' logro(s) guardado(s): ' + ids.join(', '));
  for (const id of ids) send('logro', { id: parseInt(id, 10), date: prog.unlocked[id] });
}
function watchSaves(on) {
  if (saveTimer) { clearInterval(saveTimer); saveTimer = null; }
  if (!on) return;
  saveStamp = saveSignature();
  saveTimer = setInterval(() => {
    const sig = saveSignature();
    if (sig !== saveStamp) { saveStamp = sig; commitPending('guardado del juego'); }
  }, 1500);
}
function unlock(id) {
  if (!achSet || prog.unlocked[id] || pending[id]) return;
  const a = achSet.achievements.find((x) => x.id === id); if (!a) return;
  pending[id] = new Date().toISOString(); delete prog.progress[id];
  log('LOGRO ' + id + ' ' + a.title + ' (pendiente: se guarda al guardar la partida)');
  const tr = readJson(path.join(LOGROS, 'traducciones.json'), {}), t = tr[cfg.ui || 'es'] && tr[cfg.ui || 'es'][a.id];
  const f = (a.badge || '') + '.png';
  toastQueue.push({ title: t ? t[0] : a.title, description: t ? t[1] : a.description, points: a.points,
    badge: exists(path.join(BADGES, f)) ? 'assets/logros/' + f : '', lang: cfg.ui || 'es', sound: cfg.sfx !== 'off' });
  nextToast();
}
const RA_UA = "FragileDreamsLauncher/1.0 (Dionixu's Launcher)";
async function raDownload(user, pass) {
  const post = async (params) => {
    const r = await net.fetch('https://retroachievements.org/dorequest.php', { method: 'POST',
      headers: { 'User-Agent': RA_UA, 'Content-Type': 'application/x-www-form-urlencoded' }, body: new URLSearchParams(params).toString() });
    return r.json();
  };
  send('raProgress', { step: 'login' });
  const login = await post({ r: 'login2', u: user, p: pass });
  if (!login || !login.Success || !login.Token) throw new Error((login && login.Error) || 'login');
  send('raProgress', { step: 'set' });
  const j = await post({ r: 'patch', u: login.User || user, t: login.Token, g: String(RA_GAME) });
  if (!j || !j.Success || !j.PatchData) throw new Error((j && j.Error) || 'patch');
  const p = j.PatchData;
  const list = (p.Achievements || []).filter((a) => a.Flags === 3).map((a) => ({
    id: a.ID, title: a.Title, description: a.Description, points: a.Points, badge: a.BadgeName, type: a.Type, author: a.Author, memaddr: a.MemAddr }));
  fs.mkdirSync(LOGROS, { recursive: true });
  fs.writeFileSync(RA_SET, JSON.stringify({ game: { id: p.ID, title: p.Title, icon: p.ImageIcon }, achievements: list }, null, 1));
  fs.mkdirSync(BADGES, { recursive: true });
  let done = 0;
  for (const a of list) {
    for (const suf of ['', '_lock']) {
      const f = path.join(BADGES, a.badge + suf + '.png'); if (exists(f)) continue;
      try { const r = await net.fetch('https://media.retroachievements.org/Badge/' + a.badge + suf + '.png', { headers: { 'User-Agent': RA_UA } });
        if (r.ok) fs.writeFileSync(f, Buffer.from(await r.arrayBuffer())); } catch (e) {}
    }
    send('raProgress', { step: 'badges', done: ++done, total: list.length });
  }
  log('Set de logros descargado: ' + list.length);
}
function createToastWindow() {
  const d = screen.getPrimaryDisplay().bounds;
  toastWin = new BrowserWindow({
    x: d.x + Math.round((d.width - 620) / 2), y: d.y, width: 620, height: 150, show: false, frame: false, transparent: true,
    resizable: false, focusable: false, skipTaskbar: true, alwaysOnTop: true, fullscreenable: false, hasShadow: false, backgroundColor: '#00000000',
    webPreferences: { preload: path.join(__dirname, 'preload.js'), contextIsolation: true, nodeIntegration: false, sandbox: true, backgroundThrottling: false }
  });
  toastWin.setAlwaysOnTop(true, 'screen-saver'); toastWin.setIgnoreMouseEvents(true); toastWin.setMenu(null);
  toastWin.loadURL('fd://ui/toast.html');
}
function nextToast() {
  if (toastBusy || !toastQueue.length || !toastWin || toastWin.isDestroyed()) return;
  if (cfg.achToast === 'off') { toastQueue = []; return; }
  toastBusy = true;
  const d = screen.getPrimaryDisplay().bounds;
  toastWin.setBounds({ x: d.x + Math.round((d.width - 620) / 2), y: d.y, width: 620, height: 150 });
  toastWin.showInactive(); toastWin.setAlwaysOnTop(true, 'screen-saver');
  toastWin.webContents.send('msg', JSON.stringify({ type: 'show', data: toastQueue.shift() }));
  setTimeout(() => { if (toastBusy) { toastBusy = false; nextToast(); } }, 9000);
}
function startAch(pid) {
  stopAch();
  if (cfg.achievements === 'off' || !loadSet()) return;
  const exe = path.join(__dirname, IS_WIN ? 'fd_logros.exe' : 'fd_logros');
  if (!exists(exe)) { log('Falta ' + exe); return; }
  if (!IS_WIN) { try { fs.chmodSync(exe, 0o755); } catch (e) {} }
  const defs = achSet.achievements.filter((a) => a.memaddr && !prog.unlocked[a.id]).map((a) => a.id + '\t' + translateMemaddr(a.memaddr)).join('\n') + '\n';
  const f = path.join(LOGROS, 'activos.txt');
  fs.mkdirSync(LOGROS, { recursive: true }); fs.writeFileSync(f, defs);
  achProc = spawn(exe, [String(pid), f], { windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
  pending = {}; watchSaves(true);
  let buf = '';
  achProc.stdout.on('data', (d) => {
    buf += d.toString(); let i;
    while ((i = buf.indexOf('\n')) >= 0) {
      const p = buf.slice(0, i).trim().split(' '); buf = buf.slice(i + 1);
      if (p[0] === 'UNLOCK') unlock(parseInt(p[1], 10));
      else if (p[0] === 'PROGRESS') { prog.progress[p[1]] = [parseInt(p[2], 10), parseInt(p[3], 10)]; saveProgSoon(); }
      else if (p[0]) log('logros: ' + p.join(' '));
    }
  });
  achProc.stderr.on('data', (d) => log('logros(err): ' + d.toString().trim()));
  achProc.on('exit', (c) => { log('motor de logros terminado (' + c + ')'); achProc = null; });
  achProc.on('error', (e) => { log('motor de logros: ' + e.message); achProc = null; });
}
function stopAch() {
  if (saveTimer) {
    const sig = saveSignature(); if (sig !== saveStamp) commitPending('guardado al salir');
    watchSaves(false);
    const lost = Object.keys(pending); if (lost.length) log('Sin guardar la partida: se descartan ' + lost.length + ' logro(s): ' + lost.join(', '));
    pending = {};
  }
  if (!achProc) return;
  const p = achProc; achProc = null;
  try { p.stdin.write('QUIT\n'); } catch (e) {}
  setTimeout(() => { try { p.kill(); } catch (e) {} }, 800);
  if (progTimer) { clearTimeout(progTimer); progTimer = null; }
  saveProg();
}

// ---------------- puente de mando + menú de pausa
const sendExit = (type, data) => { if (exitWin && !exitWin.isDestroyed()) exitWin.webContents.send('msg', JSON.stringify({ type, data })); };
const bridgeCmd = (s) => { try { if (bridge && bridge.stdin.writable) bridge.stdin.write(s + '\n'); } catch (e) {} };
const hwnd = (w) => { const b = w.getNativeWindowHandle(); return (b.length >= 8 ? b.readBigUInt64LE(0) : BigInt(b.readUInt32LE(0))).toString(); };

function createExitWindow() {
  const d = screen.getPrimaryDisplay().bounds;
  exitWin = new BrowserWindow({
    x: d.x, y: d.y, width: d.width, height: d.height, show: false, frame: false, transparent: true, resizable: false,
    skipTaskbar: true, alwaysOnTop: true, fullscreenable: false, hasShadow: false, backgroundColor: '#00000000',
    webPreferences: { preload: path.join(__dirname, 'preload.js'), contextIsolation: true, nodeIntegration: false, sandbox: true, backgroundThrottling: false }
  });
  exitWin.setAlwaysOnTop(true, 'screen-saver');
  exitWin.setMenu(null);
  exitWin.loadURL('fd://ui/exit.html');
}
let winWaiters = [];
function startBridge() {
  stopBridge();
  bridge = IS_WIN
    ? spawn('powershell.exe', ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', path.join(__dirname, 'bridge.ps1')], { windowsHide: true, stdio: ['pipe', 'pipe', 'ignore'] })
    : spawn(helper('pt_bridge'), [], { stdio: ['pipe', 'pipe', 'ignore'] });
  let buf = '';
  bridge.stdout.on('data', (d) => {
    buf += d.toString(); let i;
    while ((i = buf.indexOf('\n')) >= 0) {
      const line = buf.slice(0, i).trim(); buf = buf.slice(i + 1);
      if (line.startsWith('COMBO')) openExit(line === 'COMBO KB' ? 'kb' : 'pad');
      else if (line.startsWith('BTN ') && exitOpen) sendExit('btn', line.slice(4));
      else if (line === 'AUDIO OK' || line === 'AUDIO NO') { const v = line === 'AUDIO OK' ? 'ok' : 'no'; if (cfg.audioCtl !== v) { cfg.audioCtl = v; saveCfg(); } }
      else if (line.startsWith('WIN ')) { const w = winWaiters; winWaiters = []; w.forEach((f) => f()); }
      else if (line.startsWith('PAD ')) {
        const n = line.slice(4).trim();
        if (n && n !== '?' && !(cfg.padNames || []).includes(n)) { cfg.padNames = [n, ...(cfg.padNames || [])].slice(0, 6); saveCfg(); }
      }
    }
  });
  bridge.on('error', (e) => { log('puente: ' + e.message); bridge = null; });
  bridge.on('exit', () => { bridge = null; });
}
function stopBridge() { if (bridge) { bridgeCmd('QUIT'); const b = bridge; setTimeout(() => { try { b.kill(); } catch (e) {} }, 500); bridge = null; } }
let volTarget = null, volTimer = null;
const pactl = (args) => new Promise((ok) => execFile('pactl', args, { timeout: 4000 }, (e, out) => ok(e ? null : String(out))));
async function linuxApplyVolume() {
  if (volTarget == null || !game) return;
  const out = await pactl(['list', 'sink-inputs']);
  if (out == null) { if (cfg.audioCtl !== 'no') { cfg.audioCtl = 'no'; saveCfg(); } return; }
  for (const block of out.split(/\n(?=Sink Input #)/)) {
    const m = /Sink Input #(\d+)/.exec(block);
    if (m && /application\.(name|process\.binary) = "[^"]*dolphin/i.test(block)) await pactl(['set-sink-input-volume', m[1], volTarget + '%']);
  }
}
function setGameVolume(v) {
  volTarget = Math.max(0, Math.min(100, parseInt(v, 10) || 0));
  if (gameIsMotor) { motorCmd('VOLUME ' + volTarget); return; }
  if (IS_WIN) { if (game && cfg.audioCtl !== 'no') bridgeCmd('VOLUME ' + game.pid + ' ' + volTarget); return; }
  linuxApplyVolume();
  if (!volTimer) volTimer = setInterval(() => { if (!game) { clearInterval(volTimer); volTimer = null; } else linuxApplyVolume(); }, 1500);
}
function openExit(via) {
  if (!game || exitOpen || !exitWin) return;
  exitOpen = true;
  motorCmd('PAUSE');                       // el motor se congela (imagen y sonido) mientras está el menú
  exitWin.setBounds(screen.getPrimaryDisplay().bounds);
  exitWin.show(); exitWin.setAlwaysOnTop(true, 'screen-saver');
  bridgeCmd('FOCUS ' + hwnd(exitWin));
  exitWin.focus();
  sendExit('show', { lang: cfg.ui || 'es', via: via || 'pad', gvol: cfg.gvol || '100' });
}
function closeExit(back) {
  exitOpen = false;
  if (back) {
    send('prepReturn');
    win.setAlwaysOnTop(true, 'screen-saver');
    win.show(); if (cfg.launcherFull !== 'off') win.setFullScreen(true);
    bridgeCmd('FOCUS ' + hwnd(win)); win.focus();
    setTimeout(() => { exitWin.hide(); sendExit('hide'); }, 120);
    if (game) { const g = game; if (gameIsMotor) motorCmd('QUIT'); else bridgeCmd('CLOSEPID ' + g.pid); setTimeout(() => { if (game === g) { try { g.kill(); } catch (e) {} } }, 5000); }
  } else {
    exitWin.hide(); sendExit('hide');
    motorCmd('RESUME');
    // devolver el foco a la imagen del juego (dos veces: la segunda por si Windows aún no soltó la ventana de pausa)
    // devolver el foco a la imagen del juego con un clic de botón lateral (X1, o X2 si X1 está asignado; nada si ambos lo están)
    if (game) {
      const pid = game.pid, used = new Set(Object.values(Object.assign({}, KB_DEF, cfg.kbBinds || {})));
      const xb = !used.has('Click 3') ? 1 : !used.has('Click 4') ? 2 : 0;
      setTimeout(() => { if (game && game.pid === pid && !exitOpen) bridgeCmd('RESUMEPID ' + pid + ' ' + xb); }, 150);
    }
  }
}
function waitGameWindow(pid) {
  return new Promise((resolve) => {
    let done = false;
    const finish = () => { if (!done) { done = true; resolve(); } };
    setTimeout(finish, 9000);
    if (!IS_WIN) { winWaiters.push(() => setTimeout(finish, 600)); return; }
    const ps = `$t=0; while ($t -lt 90) { $p = Get-Process -Id ${pid} -ErrorAction SilentlyContinue; if (-not $p) { exit 2 };
      $w = $p.MainWindowTitle; if ($w -and $w -ne 'Dolphin' -and $t -gt 5) { exit 0 }; Start-Sleep -Milliseconds 100; $t++ }; exit 1`;
    const w = spawn('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', ps], { windowsHide: true, stdio: 'ignore' });
    w.on('exit', finish); w.on('error', () => {});
  });
}

// ---------------- jugar
let hideTimer = null;
function hideForGame() {
  clearTimeout(hideTimer); hideTimer = null;
  if (!game || !win.isVisible() || exitOpen) return;
  win.setAlwaysOnTop(false); win.hide(); bridgeCmd('FOCUSPID ' + game.pid); motorCmd('FOCUS');
}
let gameIsMotor = false;
const motorCmd = (s) => { try { if (gameIsMotor && game && game.stdin && game.stdin.writable) game.stdin.write(s + '\n'); } catch (e) {} };
function play() {
  if (game) return;
  const motor = useMotor();
  const dc = dolphinCommand();
  if (!motor && !dc.ok) throw new Error('No encuentro Dolphin: ' + paths.dolphinExe);
  const text = ['es', 'en', 'fr', 'de'].includes(cfg.text) ? cfg.text : 'es';
  const voice = cfg.voice === 'en' ? 'en' : 'ja';
  const gameDir = text === 'es' ? paths.gameES : paths.gameOrig;
  const dol = path.join(gameDir, 'DATA', 'sys', 'main.dol');
  if (!exists(path.join(gameDir, 'DATA', 'sys', 'boot.bin'))) throw new Error('No encuentro el juego en ' + gameDir);
  buildDol(text, voice, dol);
  applyTexturePack();
  if (motor) return playMotor(dol, text, voice);
  writeControls();
  writeGameSettings();
  const res = String(cfg.res || '1080');
  const ir = { 720: 2, 1080: 3, 1440: 4, 2160: 6 }[res] || 3;
  const [ww, wh] = res === '720' ? [1280, 720] : [1920, 1080];
  const wide = (cfg.aspect || '169') !== '43';
  const aa = parseInt(cfg.aa || '4', 10);
  const af = { 1: 0, 4: 2, 16: 4 }[cfg.af || '16'];
  const C = (key, v) => ['-C', key + '=' + v];
  const args = [
    ...dc.pre,
    ...(IS_WIN ? [] : ['-u', paths.dolphinUser]),
    '-b', '-e', dol,
    ...C('Dolphin.Display.Fullscreen', (cfg.mode || 'full') === 'full' ? 'True' : 'False'),
    ...C('Dolphin.Display.RenderToMain', 'False'),
    ...C('Dolphin.Display.RenderWindowWidth', ww),
    ...C('Dolphin.Display.RenderWindowHeight', wh),
    ...C('Dolphin.Display.RenderWindowAutoSize', 'False'),
    ...C('Dolphin.Interface.ConfirmStop', 'False'),
    ...C('SYSCONF.IPL.LNG', 1),
    ...C('SYSCONF.IPL.AR', wide ? 'True' : 'False'),
    ...C('Dolphin.Core.EnableCheats', 'False')
  ];
  setIni(userPath('Config', 'GFX.ini'), {
    Settings: { InternalResolution: ir, MSAA: aa > 1 ? '0x' + aa.toString(16).padStart(8, '0') : '0x00000001', SSAA: 'False',
      AspectRatio: (cfg.aspect || '169') === 'auto' ? 0 : wide ? 1 : 2, ShaderCompilationMode: 2, WaitForShadersBeforeStarting: 'False', BorderlessFullscreen: 'True',
      DumpTextures: 'False', HiresTextures: 'True' },   // paquete de botones Xbox en User\Load\Textures\R2GP99   // el volcado de texturas solo se usó para preparar el paquete de botones
    Enhancements: { MaxAnisotropy: af },
    Hardware: { VSync: (cfg.vsync || 'on') === 'on' ? 'True' : 'False' }
  });
  setIni(userPath('Config', 'Dolphin.ini'), {
    DSP: { Volume: cfg.audioCtl === 'no' ? parseInt(cfg.gvol || '100', 10) : 100 },
    Interface: { PauseOnFocusLost: IS_WIN ? 'True' : 'False', ConfirmStop: 'False', LockCursor: 'True', CursorVisibility: 0, OnScreenDisplayMessages: 'False' },
    Input: { BackgroundInput: IS_WIN ? 'False' : 'True' },
    Core: { CPUThread: 'True', DSPHLE: 'True', EnableCheats: 'False' }
  });
  setIni(userPath('Config', 'RetroAchievements.ini'), { Achievements: { Enabled: 'False' } });   // los evalúa el launcher
  log('Iniciando: ' + dc.cmd + ' ' + args.join(' '));
  win.setAlwaysOnTop(true, 'screen-saver');
  game = spawn(dc.cmd, args, { cwd: IS_WIN ? path.dirname(paths.dolphinExe) : ROOT, stdio: 'ignore',
    env: IS_WIN ? process.env : Object.assign({}, process.env, { QT_QPA_PLATFORM: 'xcb' }) });
  gameStart = Date.now();
  const myGameRef = game;
  startBridge();
  bridgeCmd('GAMEPID ' + game.pid);
  setTimeout(() => { if (game === myGameRef) startAch(myGameRef.pid); }, 4000);
  setGameVolume(cfg.gvol || '100');
  const myGame = game;
  waitGameWindow(game.pid).then(() => {
    if (cfg.mode === 'border') {
      bridgeCmd('BORDERLESS ' + myGame.pid + ' ' + ww + ' ' + wh);
      setTimeout(() => { if (game === myGame) bridgeCmd('BORDERLESS ' + myGame.pid + ' ' + ww + ' ' + wh); }, 1500);
    }
    // transición: la interfaz se funde a negro (el juego también arranca en negro) y luego se oculta la ventana
    setTimeout(() => { if (game === myGame) { send('gameReady'); hideTimer = setTimeout(hideForGame, 2500); } }, 300);
  });
  game.on('error', (e) => { log('Error al iniciar Dolphin: ' + e.message); game = null; win.setAlwaysOnTop(false); win.show(); send('error', e.message); });
  gameIsMotor = false;
  game.on('exit', (code) => onGameExit(code, 'Dolphin'));
  send('gameStarted', { text, voice });
}

function onGameExit(code, who) {
  {
    const secs = Math.round((Date.now() - gameStart) / 1000);
    log(who + ' cerrado (codigo ' + code + ', ' + secs + ' s)');
    if (secs > 20) { cfg.playtime = (parseInt(cfg.playtime || 0, 10) || 0) + secs; saveCfg(); }
    game = null;
    stopBridge(); stopAch();
    if (!win || win.isDestroyed()) return;               // el launcher se está cerrando: nada que mostrar
    if (exitOpen) { exitOpen = false; if (exitWin && !exitWin.isDestroyed()) exitWin.hide(); sendExit('hide'); }
    // vuelta: la ventana reaparece en negro (su último fotograma ya era negro) y la interfaz se aclara poco a poco
    send('prepReturn');
    setTimeout(() => {
      if (!win || win.isDestroyed()) return;
      win.show(); if (cfg.launcherFull !== 'off') win.setFullScreen(true); win.focus();
      setTimeout(() => { if (win && !win.isDestroyed()) win.setAlwaysOnTop(false); }, 400);
      setTimeout(() => send('gameClosed', { playtime: cfg.playtime || 0 }), 140);
    }, 60);
  }
}

// ---------------- motor propio: fd_motor.exe + core de Dolphin (libretro)
// Controles del motor: mismas asignaciones que Ajustes → Controles, traducidas a teclas de Windows (VK) y botones XInput.
const DI2VK = { SPACE: 32, RETURN: 13, NUMPADENTER: 13, TAB: 9, BACK: 8, LSHIFT: 160, RSHIFT: 161, LCONTROL: 162, RCONTROL: 163,
  LMENU: 164, RMENU: 165, CAPITAL: 20, UP: 38, DOWN: 40, LEFT: 37, RIGHT: 39, MINUS: 189, EQUALS: 187, LBRACKET: 219, RBRACKET: 221,
  SEMICOLON: 186, APOSTROPHE: 222, GRAVE: 192, BACKSLASH: 220, OEM_102: 226, COMMA: 188, PERIOD: 190, SLASH: 191, INSERT: 45,
  DELETE: 46, HOME: 36, END: 35, PRIOR: 33, NEXT: 34, ADD: 107, SUBTRACT: 109, MULTIPLY: 106, DIVIDE: 111, DECIMAL: 110,
  'Click 0': 1, 'Click 1': 2, 'Click 2': 4, 'Click 3': 5, 'Click 4': 6 };
const diToVk = (n) => {
  if (!n) return 0;
  if (DI2VK[n]) return DI2VK[n];
  if (/^[A-Z0-9]$/.test(n)) return n.charCodeAt(0);
  let m = /^NUMPAD(\d)$/.exec(n); if (m) return 96 + +m[1];
  m = /^F(\d{1,2})$/.exec(n); if (m) return 111 + +m[1];
  return 0;
};
const PAD2MOTOR = { 'Button A': 'A', 'Button B': 'B', 'Button X': 'X', 'Button Y': 'Y', 'Shoulder L': 'LB', 'Shoulder R': 'RB',
  'Trigger L': 'LT', 'Trigger R': 'RT', 'Thumb L': 'L3', 'Thumb R': 'R3', Start: 'START', Back: 'BACK',
  'Pad N': 'DU', 'Pad S': 'DD', 'Pad W': 'DL', 'Pad E': 'DR' };
function writeMotorControls() {
  const pb = Object.assign({}, PAD_DEF, cfg.padBinds || {}), kbb = Object.assign({}, KB_DEF, cfg.kbBinds || {});
  for (const k of Object.keys(kbb)) if (kbb[k] === 'ESCAPE') kbb[k] = KB_DEF[k];   // Esc = pausa del launcher
  const usedKb = new Set(Object.values(kbb)), usedPad = new Set(Object.values(pb));
  const acts = ['a', 'b', 'x', 'y', 'r', 'l', 'zl', 'zr', 'plus', 'minus', 'du', 'dd', 'dl', 'dr'];
  const lines = ['# generado por el launcher (Ajustes -> Controles). K = tecla/botón del ratón (VK), P = botón del mando, W = rueda',
    'mode = ' + (cfg.ctl === 'pad' ? 'pad' : cfg.ctl === 'kb' ? 'kb' : 'auto'),
    'pad_invert = ' + (cfg.padInvert === 'on' ? 'on' : 'off'), 'mouse_invert = ' + (cfg.invert === 'on' ? 'on' : 'off'),
    'mouse_sens = ' + (parseInt(cfg.msens || '100', 10) || 100), 'fps60 = ' + (cfg.fps60 === 'on' ? 'on' : 'off')];
  for (const a of acts) {
    const tok = [];
    const pads = [pb[a], PAD_EXTRA[a] && !usedPad.has(PAD_EXTRA[a]) ? PAD_EXTRA[a] : null];
    for (const n of pads) if (n && PAD2MOTOR[n]) tok.push('P' + PAD2MOTOR[n]);
    const keys = [kbb[a], ...((KB_EXTRA[a] || []).filter((x) => !usedKb.has(x)))];
    for (const n of keys) { const vk = diToVk(n); if (vk) tok.push('K' + vk); }
    if (a === 'zl') tok.push('W');                      // rueda del ratón = centrar cámara
    lines.push(a + ' = ' + tok.join(' '));
  }
  for (const a of ['ls_up', 'ls_down', 'ls_left', 'ls_right', 'rs_up', 'rs_down', 'rs_left', 'rs_right']) {
    const vk = diToVk(kbb[a]); lines.push(a + ' = ' + (vk ? 'K' + vk : ''));
  }
  const f = path.join(DATA, 'motor_controles.txt');
  fs.writeFileSync(f, lines.join('\n') + '\n');
  return f;
}
// Botones en pantalla según el control: data\texturas\mando (Xbox) o data\texturas\teclado (ratón/teclas).
// El juego carga las texturas al arrancar, así que se copia el juego que toca a User\Load\Textures\R2GP99.
function applyTexturePack() {
  const dev = ctlDevice() === 'kb' ? 'teclado' : 'mando';
  const src = path.join(DATA, 'texturas', dev), dst = userPath('Load', 'Textures', 'R2GP99');
  try {
    if (!fs.existsSync(src)) return;
    fs.mkdirSync(dst, { recursive: true });
    let n = 0;
    for (const f of fs.readdirSync(src)) if (f.endsWith('.png')) { fs.copyFileSync(path.join(src, f), path.join(dst, f)); n++; }
    log('Texturas de botones: ' + dev + ' (' + n + ')');
  } catch (e) { log('Texturas de botones: error ' + e.message); }
}
function playMotor(dol, text, voice) {
  const res = String(cfg.res || '1080');
  const efb = { 720: 2, 1080: 3, 1440: 4, 2160: 6 }[res] || 3;
  const [ww, wh] = res === '720' ? [1280, 720] : [1920, 1080];
  const wide = (cfg.aspect || '169') !== '43';
  const aa = { 1: 0, 2: 1, 4: 2, 8: 3 }[parseInt(cfg.aa || '4', 10)] ?? 2;
  const af = { 1: 0, 4: 2, 16: 4 }[cfg.af || '16'] ?? 4;
  const opts = [
    ['dolphin_renderer', 'Hardware'], ['dolphin_efb_scale', efb], ['dolphin_ir_mode', 2],
    ['dolphin_widescreen', wide ? 'enabled' : 'disabled'], ['dolphin_pal60', 'enabled'], ['dolphin_progressive_scan', 'enabled'],
    ['dolphin_language', 1], ['dolphin_load_custom_textures', 'enabled'], ['dolphin_cheats_enabled', 'disabled'],
    ['dolphin_osd_enabled', 'disabled'],
    // Shaders: el core no puede compilar en segundo plano con OpenGL en Windows (sin contextos compartidos WGL), así que todo
    // se compila en el hilo principal. Con ubershaders (1/2) precompilaba cientos de shaders enormes al arrancar (~45 s en negro).
    // 'Síncrono' (0): solo shaders específicos, rápidos de compilar y guardados en la caché de disco.
    ['dolphin_shader_compilation_mode', 0], ['dolphin_wait_for_shaders', 'disabled'],
    ['dolphin_anti_aliasing', aa], ['dolphin_max_anisotropy', af], ['dolphin_main_cpu_thread', cfg.fps60 === 'on' ? 'disabled' : 'enabled'],
    ['dolphin_save_load_settings', 'disabled'], ['dolphin_sensor_bar_position', 0], ['dolphin_enable_rumble', 'enabled']
  ];
  // 60 FPS: con doble núcleo el hilo de la GPU se queda atrás y lee las matrices de los huesos cuando la CPU ya las
  // reescribió para el siguiente fotograma -> Seto "convulsiona" (poses de otras animaciones). Un solo núcleo lo sincroniza.
  const optFile = path.join(DATA, 'motor_opciones.txt');
  fs.writeFileSync(optFile, '# generado por el launcher en cada inicio\n' + opts.map(([k, v]) => k + ' = ' + v).join('\n') + '\n');
  const args = ['--core', paths.coreDll, '--game', dol, '--system', paths.coreSystem,
    '--save', path.dirname(paths.dolphinUser),           // User = Dolphin-x64\User: mismas partidas y texturas que Dolphin
    '--opts', optFile, '--controls', writeMotorControls(), '--log', path.join(DATA, 'motor.log'),
    '--fullscreen', { full: '1', border: '2', win: '0' }[cfg.mode || 'full'] || '1', '--width', String(ww), '--height', String(wh),
    '--vsync', (cfg.vsync || 'on') === 'on' ? '1' : '0',
    '--volume', String(parseInt(cfg.gvol || '100', 10) || 0), '--title', 'Fragile Dreams: Farewell Ruins of the Moon'];
  log('Iniciando motor: ' + paths.motorExe + ' ' + args.join(' '));
  win.setAlwaysOnTop(true, 'screen-saver');
  game = spawn(paths.motorExe, args, { cwd: path.dirname(paths.motorExe), stdio: ['pipe', 'pipe', 'pipe'] });
  gameIsMotor = true;
  gameStart = Date.now();
  const myGame = game;
  startBridge();
  bridgeCmd('GAMEPID ' + game.pid);
  setTimeout(() => { if (game === myGame) startAch(myGame.pid); }, 4000);
  let ready = false;
  const onReady = () => {
    if (ready || game !== myGame) return; ready = true;
    setTimeout(() => { if (game === myGame) { send('gameReady'); hideTimer = setTimeout(hideForGame, 2500); } }, 300);
  };
  setTimeout(onReady, 90000);   // por si el motor no avisara
  let buf = '';
  game.stdout.on('data', (d) => {
    buf += d.toString(); let i;
    while ((i = buf.indexOf('\n')) >= 0) {
      const line = buf.slice(0, i).trim(); buf = buf.slice(i + 1);
      if (!line) continue;
      if (line === 'STARTED') onReady();          // el juego ya dibuja: ahora sí se funde el launcher
      else if (line === 'READY') log('motor listo, esperando la primera imagen');
      else if (line.startsWith('COMBO')) openExit(line === 'COMBO KB' ? 'kb' : 'pad');
      else if (line.startsWith('ERROR')) { log('motor: ' + line); send('error', line.slice(6)); }
      else if (line.startsWith('DEVICE ')) { const d = line.slice(7); if (d === 'kb' || d === 'pad') { cfg.lastDevice = d; saveCfg(); } }
      else log('motor: ' + line);
    }
  });
  game.stderr.on('data', (d) => log('motor(err): ' + d.toString().trim()));
  game.on('error', (e) => { log('Error al iniciar el motor: ' + e.message); game = null; gameIsMotor = false; win.setAlwaysOnTop(false); win.show(); send('error', e.message); });
  game.on('exit', (code) => { gameIsMotor = false; onGameExit(code, 'Motor'); });
  send('gameStarted', { text, voice });
}

function toggleFull(force) {
  if (!win) return;
  const on = typeof force === 'boolean' ? force : !win.isFullScreen();
  win.setFullScreen(on); cfg.launcherFull = on ? 'on' : 'off'; saveCfg();
}
const keep = () => ({ launcherFull: cfg.launcherFull, audioCtl: cfg.audioCtl, playtime: cfg.playtime, padNames: cfg.padNames, lastDevice: cfg.lastDevice });

ipcMain.on('msg', (_e, raw) => {
  let m; try { m = JSON.parse(raw); } catch (e) { return; }
  try {
    switch (m.cmd) {
      case 'init': send('state', state()); break;
      case 'save': if (m.config) { cfg = Object.assign({}, m.config, keep()); saveCfg(); } break;
      case 'play': if (m.config) { cfg = Object.assign({}, m.config, keep()); saveCfg(); } play(); break;
      case 'minimize': win.minimize(); break;
      case 'toggleFull': toggleFull(); break;
      case 'close': app.quit(); break;
      case 'exitYes': closeExit(true); break;
      case 'exitNo': closeExit(false); break;
      case 'gvol': cfg.gvol = String(Math.max(0, Math.min(100, parseInt(m.value, 10) || 0))); saveCfg(); setGameVolume(cfg.gvol); send('cfgPatch', { gvol: cfg.gvol }); break;
      case 'raLogin':
        raDownload(String(m.user || ''), String(m.pass || ''))
          .then(() => { send('raDone', { ok: true }); send('state', state()); })
          .catch((e) => { log('RetroAchievements: ' + e.message); send('raDone', { ok: false, error: e.message }); });
        break;
      case 'toastDone': toastBusy = false; if (toastQueue.length) nextToast(); else if (toastWin && !toastWin.isDestroyed()) toastWin.hide(); break;
      case 'toastTest': toastQueue.push(Object.assign({ points: 5, badge: '', lang: cfg.ui || 'es', sound: cfg.sfx !== 'off' }, m.data || {})); nextToast(); break;
      case 'hideLauncher': hideForGame(); break;
      case 'device': if (m.value === 'kb' || m.value === 'pad') { cfg.lastDevice = m.value; saveCfg(); } break;
    }
  } catch (e) { log('ERROR ' + m.cmd + ': ' + e.stack); send('error', e.message); }
});

function createWindow() {
  const wa = screen.getPrimaryDisplay().workAreaSize;
  const fit = Math.min(1, (wa.width * 0.9) / 1280, (wa.height * 0.9) / 720);
  if (process.argv.includes('--windowed')) { cfg.launcherFull = 'off'; saveCfg(); }
  win = new BrowserWindow({
    width: Math.round(1280 * fit), height: Math.round(720 * fit), center: true,
    frame: false, resizable: true, maximizable: false, fullscreenable: true, minWidth: 640, minHeight: 360,
    backgroundColor: '#050a18', show: false, title: APP_NAME,
    icon: path.join(UI, 'assets', IS_WIN ? 'icon.ico' : 'icon.png'),
    webPreferences: { preload: path.join(__dirname, 'preload.js'), contextIsolation: true, nodeIntegration: false, sandbox: true, backgroundThrottling: false }
  });
  win.setMenu(null);
  win.loadURL('fd://ui/index.html');
  win.setAspectRatio(16 / 9);
  win.once('ready-to-show', () => { win.show(); if (cfg.launcherFull !== 'off') win.setFullScreen(true); });
  win.on('enter-full-screen', () => send('fullscreen', true));
  win.on('leave-full-screen', () => send('fullscreen', false));
  win.webContents.on('before-input-event', (e, input) => {
    if (input.type !== 'keyDown') return;
    if (input.key === 'F11' || (input.alt && input.key === 'Enter')) { e.preventDefault(); toggleFull(); }
  });
}

if (!app.requestSingleInstanceLock()) app.quit();
else {
  app.on('second-instance', () => { if (win) { win.show(); win.focus(); } });
  app.whenReady().then(async () => {
    fs.mkdirSync(DATA, { recursive: true });
    try { await session.defaultSession.clearCache(); } catch (e) {}
    protocol.handle('fd', (req) => {
      const rel = decodeURIComponent(new URL(req.url).pathname).replace(/^\/+/, '');
      const file = path.resolve(UI, rel);
      if (!file.startsWith(path.resolve(UI))) return new Response('Prohibido', { status: 403 });
      return net.fetch(pathToFileURL(file).toString());
    });
    createWindow();
    createExitWindow();
    createToastWindow();
    // Alt+F4 / cerrar la ventana principal = salir de verdad (las ventanas ocultas de pausa y logros mantenían viva la app)
    win.on('closed', () => app.quit());
    log('Launcher iniciado. Dolphin: ' + paths.dolphinExe);
  });
  app.on('before-quit', () => {
    // cerrar también el juego si sigue abierto (motor: QUIT y, por si acaso, kill; Dolphin: kill)
    // (el motor también se cierra solo si el launcher desaparece: su entrada estándar se corta)
    if (game) { const g = game; try { if (gameIsMotor) motorCmd('QUIT'); else g.kill(); } catch (e) {} }
    stopBridge(); stopAch();
    for (const w of [exitWin, toastWin]) { try { if (w && !w.isDestroyed()) w.destroy(); } catch (e) {} }
  });
  app.on('window-all-closed', () => app.quit());
}

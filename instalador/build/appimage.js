// Crea un AppImage (runtime tipo 2 + squashfs 4.0 con zlib) sin herramientas de Linux: funciona en Windows.
// Uso (con Node o con Electron como Node):  appimage.js <spec.json>
//   spec = { runtime, out, mtime?, trees: [{ src, dst, exclude?: [rutas relativas], exec?: [rutas relativas] }],
//            files: [{ src | text, dst, mode? }] }
// Rutas "dst" con "/" relativas a la raíz del AppImage.
'use strict';
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const BS = 131072, BLOG = 17, META = 8192;
const NOFRAG = 0xffffffff;

// ---------------------------------------------------------------- árbol
function makeTree(spec) {
  const root = { name: '', dir: true, kids: new Map(), mode: 0o755 };
  const mkdirp = (rel) => {
    let n = root;
    for (const part of rel.split('/').filter(Boolean)) {
      if (!n.kids.has(part)) n.kids.set(part, { name: part, dir: true, kids: new Map(), mode: 0o755 });
      n = n.kids.get(part);
      if (!n.dir) throw new Error('conflicto: ' + rel);
    }
    return n;
  };
  const addFile = (rel, f) => {
    const parts = rel.split('/').filter(Boolean), name = parts.pop();
    const d = mkdirp(parts.join('/'));
    d.kids.set(name, Object.assign({ name, dir: false }, f));
  };
  for (const t of [].concat(spec.trees || [])) {
    const ex = new Set([].concat(t.exclude || []).map((s) => s.replace(/\\/g, '/').toLowerCase()));
    const exe = new Set([].concat(t.exec || []).map((s) => s.replace(/\\/g, '/')));
    const walk = (abs, rel) => {
      for (const e of fs.readdirSync(abs, { withFileTypes: true })) {
        const r = rel ? rel + '/' + e.name : e.name;
        if (ex.has(r.toLowerCase()) || /^(desktop\.ini|thumbs\.db)$/i.test(e.name)) continue;
        const p = path.join(abs, e.name);
        if (e.isDirectory()) { mkdirp(joinRel(t.dst, r)); walk(p, r); }
        else if (e.isFile() || e.isSymbolicLink()) {
          const st = fs.statSync(p);
          if (!st.isFile()) continue;
          addFile(joinRel(t.dst, r), { src: p, size: st.size, mode: exe.has(r) ? 0o755 : 0o644 });
        }
      }
    };
    mkdirp(t.dst || '');
    walk(t.src, '');
  }
  for (const f of [].concat(spec.files || [])) {
    if (f.text != null) { const data = Buffer.from(f.text, 'utf8'); addFile(f.dst, { data, size: data.length, mode: f.mode || 0o644 }); }
    else { const st = fs.statSync(f.src); addFile(f.dst, { src: f.src, size: st.size, mode: f.mode || 0o644 }); }
  }
  return root;
}
const joinRel = (a, b) => [a, b].filter(Boolean).join('/');

// ---------------------------------------------------------------- tablas de metadatos (bloques de 8 KiB)
class Meta {
  constructor() { this.blocks = []; this.size = 0; this.buf = Buffer.alloc(0); }
  pos() { return { block: this.size, offset: this.buf.length }; }   // posición donde caerá lo próximo que se escriba
  write(b) {
    this.buf = Buffer.concat([this.buf, b]);
    while (this.buf.length >= META) this.flush(META);
  }
  flush(n) {
    const raw = this.buf.subarray(0, n); this.buf = this.buf.subarray(n);
    const z = zlib.deflateSync(raw, { level: 9 });
    const hdr = Buffer.alloc(2);
    let body;
    if (z.length < raw.length) { hdr.writeUInt16LE(z.length); body = z; } else { hdr.writeUInt16LE(raw.length | 0x8000); body = Buffer.from(raw); }
    this.blocks.push(hdr, body); this.size += 2 + body.length;
  }
  end() { if (this.buf.length) this.flush(this.buf.length); return Buffer.concat(this.blocks); }
}

// ---------------------------------------------------------------- escritura
function build(spec, onProgress) {
  const root = makeTree(spec);
  const mtime = spec.mtime || Math.floor(Date.now() / 1000);
  // números de inodo en post-orden (hijos antes que el directorio)
  let ino = 0; const order = [];
  const sorted = (d) => [...d.kids.values()].sort((a, b) => Buffer.compare(Buffer.from(a.name), Buffer.from(b.name)));
  const number = (d) => {
    for (const k of sorted(d)) { if (k.dir) number(k); else { k.ino = ++ino; order.push(k); } }
    d.ino = ++ino;
  };
  number(root);
  const total = ino;
  let totalBytes = 0; for (const f of order) totalBytes += f.size;

  const runtime = fs.readFileSync(spec.runtime);
  const tmp = spec.out + '.tmp';
  const fd = fs.openSync(tmp, 'w');
  let pos = 96;   // el superbloque se escribe al final
  const put = (b) => { fs.writeSync(fd, b, 0, b.length, runtime.length + pos); pos += b.length; };
  fs.writeSync(fd, runtime, 0, runtime.length, 0);

  // 1) datos de los archivos
  let done = 0, lastPct = -1;
  const chunk = Buffer.alloc(BS);
  for (const f of order) {
    f.start = pos; f.blocks = [];
    const emit = (raw) => {
      const z = zlib.deflateSync(raw, { level: 6 });
      if (z.length < raw.length) { put(z); f.blocks.push(z.length); } else { put(Buffer.from(raw)); f.blocks.push(raw.length | 0x1000000); }
      done += raw.length;
      const pct = totalBytes ? Math.floor(done * 100 / totalBytes) : 100;
      if (onProgress && pct !== lastPct) { lastPct = pct; onProgress(pct); }
    };
    if (f.data) { for (let o = 0; o < f.data.length; o += BS) emit(f.data.subarray(o, o + BS)); }
    else if (f.size) {
      const sfd = fs.openSync(f.src, 'r');
      try { for (let o = 0; o < f.size; o += BS) { const n = fs.readSync(sfd, chunk, 0, Math.min(BS, f.size - o), o); emit(chunk.subarray(0, n)); } }
      finally { fs.closeSync(sfd); }
    }
    if (!f.size) f.start = 0;
  }

  // 2) inodos y directorios
  const inodes = new Meta(), dirs = new Meta();
  const ref = (p) => p.block * 65536 + p.offset;
  const head = (type, mode, num) => { const b = Buffer.alloc(16); b.writeUInt16LE(type, 0); b.writeUInt16LE(mode & 0o7777, 2); b.writeUInt16LE(0, 4); b.writeUInt16LE(0, 6); b.writeUInt32LE(mtime, 8); b.writeUInt32LE(num, 12); return b; };
  const writeFile = (f) => {
    const p = inodes.pos(); f.ref = p;
    const nb = f.blocks.length, big = f.start + 0 > 0xffffffff || f.size > 0xffffffff;
    let b;
    if (!big) {
      b = Buffer.alloc(16 + 16 + nb * 4); head(2, f.mode, f.ino).copy(b);
      b.writeUInt32LE(f.start >>> 0, 16); b.writeUInt32LE(NOFRAG, 20); b.writeUInt32LE(0, 24); b.writeUInt32LE(f.size, 28);
      f.blocks.forEach((s, i) => b.writeUInt32LE(s >>> 0, 32 + i * 4));
    } else {
      b = Buffer.alloc(16 + 40 + nb * 4); head(9, f.mode, f.ino).copy(b);
      b.writeBigUInt64LE(BigInt(f.start), 16); b.writeBigUInt64LE(BigInt(f.size), 24); b.writeBigUInt64LE(0n, 32);
      b.writeUInt32LE(1, 40); b.writeUInt32LE(NOFRAG, 44); b.writeUInt32LE(0, 48); b.writeUInt32LE(0xffffffff, 52);
      f.blocks.forEach((s, i) => b.writeUInt32LE(s >>> 0, 56 + i * 4));
    }
    inodes.write(b);
  };
  const writeDir = (d, parentIno) => {
    const kids = sorted(d);
    for (const k of kids) { if (k.dir) writeDir(k, d.ino); else writeFile(k); }
    // listado: cabeceras de hasta 256 entradas que compartan bloque de inodos y rango de números
    const lp = dirs.pos(); let listing = 0;
    let i = 0;
    while (i < kids.length) {
      const blk = kids[i].ref.block, base = kids[i].ino;
      let j = i;
      while (j < kids.length && j - i < 256 && kids[j].ref.block === blk && Math.abs(kids[j].ino - base) < 32767) j++;
      const h = Buffer.alloc(12); h.writeUInt32LE(j - i - 1, 0); h.writeUInt32LE(blk, 4); h.writeUInt32LE(base, 8);
      dirs.write(h); listing += 12;
      for (let k = i; k < j; k++) {
        const c = kids[k], nm = Buffer.from(c.name, 'utf8');
        const e = Buffer.alloc(8); e.writeUInt16LE(c.ref.offset, 0); e.writeInt16LE(c.ino - base, 2); e.writeUInt16LE(c.dir ? 1 : 2, 4); e.writeUInt16LE(nm.length - 1, 6);
        dirs.write(Buffer.concat([e, nm])); listing += 8 + nm.length;
      }
      i = j;
    }
    const nlink = 2 + kids.filter((k) => k.dir).length;
    const p = inodes.pos(); d.ref = p;
    const b = Buffer.alloc(16 + 24); head(8, d.mode, d.ino).copy(b);
    b.writeUInt32LE(nlink, 16); b.writeUInt32LE(listing + 3, 20); b.writeUInt32LE(lp.block, 24); b.writeUInt32LE(parentIno, 28);
    b.writeUInt16LE(0, 32); b.writeUInt16LE(lp.offset, 34); b.writeUInt32LE(0xffffffff, 36);
    inodes.write(b);
  };
  writeDir(root, total + 1);
  const itab = inodes.end(), dtab = dirs.end();
  const inodeStart = pos; put(itab);
  const dirStart = pos; put(dtab);
  // tabla de ids (uid/gid 0)
  const idm = new Meta(); idm.write(Buffer.alloc(4)); const idBlock = idm.end();
  const idBlockPos = pos; put(idBlock);
  const idTab = pos; const ptr = Buffer.alloc(8); ptr.writeBigUInt64LE(BigInt(idBlockPos)); put(ptr);
  const used = pos;
  // relleno a 4 KiB
  const pad = (4096 - (used % 4096)) % 4096; if (pad) put(Buffer.alloc(pad));
  // superbloque
  const sb = Buffer.alloc(96);
  sb.writeUInt32LE(0x73717368, 0); sb.writeUInt32LE(total, 4); sb.writeUInt32LE(mtime, 8); sb.writeUInt32LE(BS, 12);
  sb.writeUInt32LE(0, 16); sb.writeUInt16LE(1, 20); sb.writeUInt16LE(BLOG, 22);
  sb.writeUInt16LE(0x0010 | 0x0200, 24);   // sin fragmentos, sin xattrs
  sb.writeUInt16LE(1, 26); sb.writeUInt16LE(4, 28); sb.writeUInt16LE(0, 30);
  sb.writeBigUInt64LE(BigInt(ref(root.ref)), 32); sb.writeBigUInt64LE(BigInt(used), 40);
  sb.writeBigUInt64LE(BigInt(idTab), 48); sb.writeBigUInt64LE(0xffffffffffffffffn, 56);
  sb.writeBigUInt64LE(BigInt(inodeStart), 64); sb.writeBigUInt64LE(BigInt(dirStart), 72);
  sb.writeBigUInt64LE(0xffffffffffffffffn, 80); sb.writeBigUInt64LE(0xffffffffffffffffn, 88);
  fs.writeSync(fd, sb, 0, 96, runtime.length);
  fs.closeSync(fd);
  try { fs.rmSync(spec.out, { force: true }); } catch (e) {}
  fs.renameSync(tmp, spec.out);
  try { fs.chmodSync(spec.out, 0o755); } catch (e) {}
  return { files: order.length, inodes: total, bytes: runtime.length + pos };
}
module.exports = { build };
if (require.main === module) {
  const spec = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
  let last = -1;
  const r = build(spec, (p) => { if (p !== last && p % 5 === 0) { last = p; process.stdout.write('  ' + p + '%\n'); } });
  console.log('AppImage: ' + spec.out + '  (' + r.files + ' archivos, ' + (r.bytes / 1048576).toFixed(1) + ' MB)');
}

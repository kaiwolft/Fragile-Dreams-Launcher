# Linterna en tercera persona: modo "se queda donde la dejas", suave y sin saltos (parche sobre la rutina 2 de Vague Rant)
import json,struct,subprocess,hashlib
CAVE=0x8025E590; R2=CAVE+112; NEW=CAVE+632
HELPER=R2+0x104; EPI=R2+0xF4; HOOK=R2+0x4C
SPEED=0.032   # avance por fotograma con el stick a fondo (curva cuadrática)
DZ=0.12       # zona muerta del stick
def asm(lines):
    src='\n'.join(lines)+'\n'
    out=subprocess.run(['llvm-mc','-triple=powerpc-unknown-unknown','-show-encoding'],input=src,capture_output=True,text=True)
    assert not out.stderr.strip(),out.stderr
    words=[]
    for l in out.stdout.splitlines():
        if 'encoding:' in l:
            bs=[int(x,16) for x in l.split('encoding: [')[1].rstrip(']').split(',')]; words.append(struct.unpack('>I',bytes(bs))[0])
    return words
def b(frm,to,link=False):
    d=to-frm; assert -0x2000000<=d<0x2000000; return 0x48000000|(d&0x3FFFFFC)|(1 if link else 0)
code=[]   # (palabra) o ('b'/'bl', destino)
def A(*l): code.extend(asm(list(l)))
code.append(('bl', 'CONST_END'))
code.append(struct.unpack('>I',struct.pack('>f',SPEED))[0])   # +0 velocidad
code.append(struct.unpack('>I',struct.pack('>f',DZ))[0])      # +4 zona muerta
code.append(0); code.append(0)                                # +8 / +12 posición guardada (x, y): el juego reescribe la suya
code.append('CONST_END')
A('mflr 6','li 0, 2','stb 0, 94(3)',   # puntero siempre válido: sin saltos entre "apuntar" y "al frente"
  'andi. 0, 4, 8192')                  # Z (centrar cámara: LB / R3) también centra la linterna
code.append(('beq', 'MOVE'))
A('lfs 0, 0(6)','fsub 0, 0, 0','stfs 0, 8(6)','stfs 0, 12(6)')
code.append(('b', 'STORE'))
code.append('MOVE')
A('lfs 3, 0(6)','lfs 4, 4(6)')
# eje X
A('lfs 1, 116(3)','fabs 2, 1','fcmpu 0, 2, 4')
code.append(('bge','X_OK')); A('fsub 1, 1, 1'); code.append('X_OK')
A('fmul 1, 1, 2','lfs 0, 8(6)'); code.append(('bl', HELPER)); A('stfs 0, 8(6)')
# eje Y (invertido como en el original)
A('lfs 1, 120(3)','fneg 1, 1','fabs 2, 1','fcmpu 0, 2, 4')
code.append(('bge','Y_OK')); A('fsub 1, 1, 1'); code.append('Y_OK')
A('fmul 1, 1, 2','lfs 0, 12(6)'); code.append(('bl', HELPER)); A('stfs 0, 12(6)')
code.append('STORE')
A('lfs 0, 8(6)','stfs 0, 32(3)','lfs 0, 12(6)','stfs 0, 36(3)')
code.append(('b', EPI))
# resolver etiquetas
addr=NEW; labels={}; seq=[]
for c in code:
    if isinstance(c,str): labels[c]=addr
    else: seq.append((addr,c)); addr+=4
words=[]
for a,c in seq:
    if isinstance(c,int): words.append(c); continue
    op,t=c; t=labels.get(t,t)
    if op=='b': words.append(b(a,t))
    elif op=='bl': words.append(b(a,t,True))
    elif op in ('beq','bge'):
        d=t-a; assert -0x8000<=d<0x8000
        bo,bi={'beq':(12,2),'bge':(4,0)}[op]   # beq cr0 / bge cr0 (no menor)
        words.append((16<<26)|(bo<<21)|(bi<<16)|(d&0xFFFC))
newhex=''.join('%08X'%w for w in words)
j=json.load(open('/mnt/user-data/outputs/fd/launcher/data/parche_mando_eur.json'))
cave=bytearray(bytes.fromhex(j['cave']))
assert cave[0x70+0x4C:0x70+0x50].hex()=='38000002'
cave[0x70+0x4C:0x70+0x50]=struct.pack('>I',b(HOOK,NEW))
cave+=bytes.fromhex(newhex)
assert len(cave)<=1888
dol=open('/mnt/user-data/uploads/Proyecto Fragile Dreams Farewell Ruins of the Moon/juego/dol/original.dol','rb').read()
o=CAVE-0x80004060
assert hashlib.md5(dol[o:o+632]).hexdigest()==j['cave_expect_md5']
j['cave']=cave.hex().upper(); j['cave_expect_md5']=hashlib.md5(dol[o:o+len(cave)]).hexdigest()
j['nota_linterna']='v5: tercera persona relativa (se queda), curva cuadratica, zona muerta %.2f, velocidad %.3f, Z recentra'%(DZ,SPEED)
json.dump(j,open('/mnt/user-data/outputs/fd/codigos/parche_mando_eur_v5.json','w'),indent=1)
print('nuevo bloque',len(words)*4,'bytes; cueva total',len(cave))
open('newblk.bin','wb').write(bytes.fromhex(newhex))

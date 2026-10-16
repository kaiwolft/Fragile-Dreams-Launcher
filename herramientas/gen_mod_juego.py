# Parche de juego (EUR): movimiento 360 + cámara con el stick derecho (lo alimenta el motor por memoria compartida)
import json,struct,subprocess,hashlib,sys
sys.path.insert(0,'../strap')
from dol import Dol
CAVE=0x8025E900          # zona muerta libre (el parche del mando v4 llega hasta 0x8025E808)
SH=0x8025ECB0            # 64 bytes de memoria compartida motor <-> juego (fin de la zona muerta 0x8025ECF0)
HOOK_FP=0x8000B06C       # cámara 1ª persona: justo antes de limitar el pitch y aplicar la rotación
HOOK_TP=0x8000B240       # cámara 3ª persona: al entrar, antes de leer la posición del ojo
HOOK_PTR=0x80110CAC      # tras KPADRead: +56 = puntero 'fuera de pantalla' (mando); SH-16 = el ratón pone la posición del cursor
HOOK_HM=0x800EE8C4       # muestreo de animación (Hermite): a 60 FPS se evalúan medios fotogramas y las curvas de rotación
                         # (Euler, horneadas a 30) saltan ±pi/2pi entre claves -> se interpolaba 'por el camino largo' (Seto convulsionaba)
TURN=0x800DC814          # ¿girar hacia el stick o caminar hacia atrás? -> siempre girar
f=lambda x: struct.unpack('>I',struct.pack('>f',x))[0]
hi=(SH+0x8000)>>16; lo=SH&0xFFFF
def asm(src):
    p=subprocess.run(['llvm-mc','-triple=powerpc-unknown-unknown','-filetype=obj','-o','c.o'],input=src,capture_output=True,text=True)
    assert p.returncode==0 and not p.stderr.strip(), p.stderr
    subprocess.run(['llvm-objcopy','-O','binary','--only-section=.text','c.o','c.bin'],check=True)
    b=open('c.bin','rb').read(); return [struct.unpack('>I',b[i:i+4])[0] for i in range(0,len(b),4)]
src=f'''
fp:
  lis 12, {hi}
  addi 12, 12, {lo-0x10000 if lo>=0x8000 else lo}
  lwz 0, 8(12)
  cmpwi 0, 0
  beq 1f
  lwz 11, 12(12)
  addi 11, 11, 1
  stw 11, 12(12)
  lfs 1, 80(1)
  lfs 0, 4(12)
  lfs 2, 20(12)
  fmadds 1, 0, 2, 1
  stfs 1, 80(1)
  lfs 1, 84(1)
  lfs 0, 0(12)
  lfs 2, 16(12)
  fnmsubs 1, 0, 2, 1
  stfs 1, 84(1)
1:
  lfs 1, 80(1)
  .long 0xFEED0001
tp:
  lis 12, {hi}
  addi 12, 12, {lo-0x10000 if lo>=0x8000 else lo}
  lwz 0, 8(12)
  cmpwi 0, 0
  beq 9f
  lwz 11, 12(12)
  addi 11, 11, 1
  stw 11, 12(12)
  lfs 0, 0(12)
  lfs 2, 16(12)
  fmuls 2, 0, 2
  lfs 3, 4(4)
  lfs 4, 0(3)
  fsubs 3, 3, 4
  lfs 5, 12(4)
  lfs 6, 8(3)
  fsubs 5, 5, 6
  fmuls 7, 2, 2
  lfs 8, 24(12)
  lfs 9, 28(12)
  fnmsubs 7, 7, 8, 9
  fmuls 10, 3, 7
  fnmsubs 10, 5, 2, 10
  fmuls 11, 5, 7
  fmadds 11, 3, 2, 11
  fadds 10, 10, 4
  fadds 11, 11, 6
  stfs 10, 4(4)
  stfs 11, 12(4)
  lfs 0, 4(12)
  lfs 2, 36(12)
  fmuls 2, 0, 2
  lfs 13, 32(12)
  fsubs 1, 13, 2
  lfs 3, 40(12)
  fcmpo 0, 1, 3
  bge 2f
  fmr 1, 3
2:
  lfs 3, 44(12)
  fcmpo 0, 1, 3
  ble 3f
  fmr 1, 3
3:
  fsubs 2, 1, 13
  stfs 1, 32(12)
  lfs 3, 48(12)
  fadds 3, 3, 1
  stfs 3, 4108(3)
  lfs 3, 52(12)
  fadds 3, 3, 1
  stfs 3, 4112(3)
  lfs 3, 4264(3)
  fadds 3, 3, 2
  stfs 3, 4264(3)
9:
  lwz 6, 4056(3)
  .long 0xFEED0002
ptr:
  lis 12, {hi}
  addi 12, 12, {lo-0x10000 if lo>=0x8000 else lo}
  lwz 0, 56(12)
  cmpwi 0, 0
  beq 5f
  li 0, 0
  stw 0, 420(30)
  stb 0, 526(30)
5:
  lwz 0, -16(12)
  cmpwi 0, 0
  beq 6f
  lfs 0, -12(12)
  stfs 0, 464(30)
  lfs 0, -8(12)
  stfs 0, 468(30)
  li 0, 2
  stb 0, 526(30)
  stw 0, 420(30)
6:
  lfs 3, -24640(2)
  .long 0xFEED0003
hm:
  fmadds 1, 8, 2, 0
  lbz 0, 10(31)
  cmpwi 0, 1
  bne 9f
  fsubs 3, 29, 31
  fabs 4, 3
  lis 0, 0x3FC0
  stw 0, 24(1)
  lfs 5, 24(1)
  fcmpo 0, 4, 5
  ble 3f
  lis 0, 0x3F00
  stw 0, 24(1)
  lfs 5, 24(1)
  fmr 1, 31
  fcmpo 0, 9, 5
  blt 9f
  fmr 1, 29
  b 9f
3:
  fmadds 1, 3, 9, 31
9:
  .long 0xFEED0004
'''
w=asm(src)
def b(frm,to): d=to-frm; assert -0x2000000<=d<0x2000000; return 0x48000000|(d&0x3FFFFFC)
fp_at=CAVE; tp_at=CAVE+4*(w.index(0xFEED0001)+1); ptr_at=CAVE+4*(w.index(0xFEED0002)+1); hm_at=CAVE+4*(w.index(0xFEED0003)+1)
for i,x in enumerate(w):
    if x==0xFEED0001: w[i]=b(CAVE+4*i,HOOK_FP+4)
    if x==0xFEED0002: w[i]=b(CAVE+4*i,HOOK_TP+4)
    if x==0xFEED0003: w[i]=b(CAVE+4*i,HOOK_PTR+4)
    if x==0xFEED0004: w[i]=b(CAVE+4*i,HOOK_HM+4)
assert CAVE+4*len(w)<=SH-16
shm=[0,0,0,0, f(0.050),f(0.035),f(0.5),f(1.0), 0,f(0.080),f(-1.0),f(1.6), f(1.5),f(0.9),0,0]
d=Dol('../strap/original.dol')
pad=(SH-CAVE)//4-len(w)
blob=w+[d.w(CAVE+4*(len(w)+k)) for k in range(pad-4)]+[0,0,0,0]+shm   # SH-16: ratón -> puntero del juego (activar, x, y)     # el hueco intermedio queda como estaba
assert len(blob)*4==SH+64-CAVE
orig=b''.join(struct.pack('>I',d.w(CAVE+4*k)) for k in range(len(blob)))
for name in ('../strap/original.dol','../strap/espanol.dol'):
    dd=Dol(name); assert b''.join(struct.pack('>I',dd.w(CAVE+4*k)) for k in range(len(blob)))==orig, name
# la linterna ya no arrastra el cuerpo: si el giro del torso pasaba del límite, el juego giraba a Seto hacia la luz
# (se quita la llamada SetRot en las dos ramas de 0x800DC900; el torso/cabeza siguen limitados)
# a 60 FPS (intervalo 1) el juego se saltaba el 'cierre de fotograma' de los actores (0x800C4B30: pasa las banderas
# +14664 a +14668, las limpia y llama vtable[7]); las banderas nunca se limpiaban y Seto quedaba bloqueado -> llamarlo siempre
writes=[(0x8001BAAC,0x60000000,0x41820008),(HOOK_HM,b(HOOK_HM,hm_at),0xEC2800BA),(HOOK_PTR,b(HOOK_PTR,ptr_at),0xC0629FC0),(HOOK_FP,b(HOOK_FP,fp_at),0xC0210050),(HOOK_TP,b(HOOK_TP,tp_at),0x80C30FD8),(TURN,0x38600001,0x48000025),(0x800DCAB0,0x60000000,0x4E800421),(0x800DCBC4,0x60000000,0x4E800421),
        (0x800DCABC,0x60000000,0x4BFCD1FD),(0x800DCBD0,0x60000000,0x4BFCD0E9)]   # ni la animación de pasos de giro (bandera 1024)
for a,v,e in writes: assert d.w(a)==e,(hex(a),hex(d.w(a)))
out={'nombre':'movimiento 360 + camara con stick derecho (v10)','shared_addr':'%08X'%SH,
     'cave_addr':'%08X'%CAVE,'cave_expect_md5':hashlib.md5(orig).hexdigest(),'cave':''.join('%08X'%x for x in blob),
     'writes':[{'addr':'%08X'%a,'value':'%08X'%v,'expect':'%08X'%e} for a,v,e in writes]}
json.dump(out,open('parche_juego_eur.json','w'),indent=1)
print(len(w),'instr; fp',hex(fp_at),'tp',hex(tp_at)); print(json.dumps(out['writes']))

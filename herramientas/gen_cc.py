# Convierte el codigo Gecko EUR de Vague Rant en un parche estatico para main.dol
import json,struct,sys,re
src=open('/mnt/user-data/outputs/fd/codigos/classic_controller_vague_rant.txt').read().split('[EUR]')[1]
L=[l.split() for l in src.splitlines() if re.match(r'^[0-9A-F]{8} [0-9A-F]{8}$',l)]
CAVE=0x8025E590
writes=[]; cave=[]; i=0
def b(frm,to): 
    d=to-frm; assert -0x2000000<=d<0x2000000; return 0x48000000|(d&0x3FFFFFC)
while i<len(L):
    a,v=L[i]
    t=a[:2]; addr=0x80000000|(int(a,16)&0x1FFFFFF)
    if t=='04': writes.append((addr,int(v,16),None)); i+=1
    elif t=='C2':
        n=int(v,16); words=[]
        for k in range(n): words+= [int(L[i+1+k][0],16),int(L[i+1+k][1],16)]
        assert words[-1]==0, 'C2 sin hueco final'
        start=CAVE+4*len(cave)
        words[-1]=b(start+4*(len(words)-1), addr+4)
        cave+=words
        writes.append((addr,b(addr,start),None))
        i+=1+n
    else: raise Exception(t)
# Las rutinas se escriben DENTRO del texto del juego, sobre funciones que nada llama ni referencia
# (0x8025E590-0x8025ECF0, 1888 bytes de funciones muertas: sin bl, sin punteros en datos, sin lis/addi)
print(len(writes),'writes', len(cave)*4,'bytes cave')
# expected originals from the base dol
dol=open('/mnt/user-data/uploads/Proyecto Fragile Dreams Farewell Ruins of the Moon/para_claude/US/DATA/sys/main.dol','rb').read()
dead=dol[CAVE-0x80004060:CAVE-0x80004060+4*len(cave)].hex().upper()
out={'cave_addr':'%08X'%CAVE,'cave_expect_md5':__import__('hashlib').md5(bytes.fromhex(dead)).hexdigest(),'cave':''.join('%08X'%w for w in cave),
     'writes':[{'addr':'%08X'%a,'value':'%08X'%v,'expect':dol[a-0x80004060:a-0x80004056+0][:4].hex().upper() if False else dol[a-0x80004060:a-0x80004060+4].hex().upper()} for a,v,_ in writes]}
json.dump(out,open('/mnt/user-data/outputs/fd/codigos/parche_mando_eur.json','w'),indent=1)
print(json.dumps(out['writes'],indent=0))
